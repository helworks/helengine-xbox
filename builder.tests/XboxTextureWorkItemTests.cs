using helengine;
using helengine.baseplatform.Manifest;
using helengine.xbox.builder;

namespace helengine.xbox.builder.tests;

/// <summary>Checks batch preflight protects existing outputs and stages cooked textures and font atlases.</summary>
public sealed class XboxTextureWorkItemTests : IDisposable {
    /// <summary>Loads cooked texture envelopes with the exact runtime target policy.</summary>
    [Theory]
    [InlineData(16)]
    [InlineData(32)]
    [InlineData(64)]
    public void TexturePayloadLoadsWithTargetNumericPolicy(int bits) {
        var policy = new NumericPrecisionPolicy(bits, bits);
        var paths = new XboxPlatformCookWorkItemExecutor().Execute([Item("textures/policy.heasset")], [], Package, policy, CancellationToken.None);
        using var stream = File.OpenRead(Path.Combine(Package, Assert.Single(paths)));
        using TextureAsset cooked = Assert.IsType<TextureAsset>(PackagedAssetBinarySerializer.Deserialize(stream, policy));
        Assert.Equal(4, cooked.Width);
    }

    /// <summary>Uses one fixed test root under the explicit dotnet artifacts directory.</summary>
    readonly string Root = Path.Combine(AppContext.BaseDirectory, "texture-work-items");
    /// <summary>Gets the contained package-source tree consumed by staging.</summary>
    string Package => Path.Combine(Root, "package-source");
    /// <summary>Gets the engine texture source retained untouched by the cook.</summary>
    string Source => Path.Combine(Root, "source.heasset");

    /// <summary>Creates a source texture and an existing package output to detect partial writes.</summary>
    public XboxTextureWorkItemTests() {
        Directory.CreateDirectory(Package);
        using TextureAsset source = XboxTextureCookTests.Source(4, 2);
        File.WriteAllBytes(Source, helengine.files.AssetSerializer.SerializeToBytes(source));
        File.WriteAllText(Path.Combine(Package, "previous.txt"), "previous");
    }

    /// <summary>Checks both source kinds reach native payloads and then the safe disc stager.</summary>
    [Fact]
    public void TextureAndFontAtlasWorkItemsCookBeforeStaging() {
        var outputs = new XboxPlatformCookWorkItemExecutor().Execute(
            [Item("textures/image.heasset"), Item("fonts/atlas.heasset", "font-atlas-texture")], [], Package, CancellationToken.None);
        string stage = Path.Combine(Root, "disc");
        Assert.Equal(outputs, new XboxContentStager().Stage([], outputs, Package, stage, CancellationToken.None));
        foreach (string path in outputs) {
            using TextureAsset cooked = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(Path.Combine(stage, path))));
            Assert.Equal(TextureAssetColorFormat.XboxNative, cooked.ColorFormat);
            Assert.Equal(path, cooked.Id);
            Assert.NotEqual(0ul, cooked.RuntimeAssetId);
            Assert.Equal(4 * 2 * 4, TextureAssetPixelCodec.DecodeToRgba32(cooked).Length);
        }
    }

    /// <summary>Rejects invalid later output paths before an earlier valid work item can write.</summary>
    [Theory]
    [InlineData("../outside.heasset")]
    [InlineData("C:/outside.heasset")]
    [InlineData("image.heasset:stream")]
    public void TraversalOrRootedOutputPreflightWritesNothing(string path) {
        Assert.Throws<InvalidOperationException>(() => new XboxPlatformCookWorkItemExecutor().Execute([Item("first.heasset"), Item(path)], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "first.heasset")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Checks builder outputs and manifest artifacts are mutually exclusive after normalization.</summary>
    [Fact]
    public void ManifestCollisionFailsBeforeCooking() {
        Assert.Throws<InvalidOperationException>(() => new XboxPlatformCookWorkItemExecutor().Execute([Item("TEXTURES/image.heasset")],
            [new PlatformBuildArtifact("textures/image.heasset", "hash", "texture", "default")], Package, CancellationToken.None));
        Assert.False(Directory.Exists(Path.Combine(Package, "TEXTURES")));
    }

    /// <summary>Checks a later decode failure cannot publish the earlier successfully cooked payload.</summary>
    [Fact]
    public void LaterInvalidSourcePreservesPackageOutputs() {
        string bad = Path.Combine(Root, "bad-source.heasset");
        File.WriteAllBytes(bad, [1, 2, 3]);
        Assert.ThrowsAny<Exception>(() => new XboxPlatformCookWorkItemExecutor().Execute([Item("first.heasset"), Item("second.heasset", source: bad)], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "first.heasset")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Rejects a file/directory output collision before any prepared payload is published.</summary>
    [Fact]
    public void ParentChildOutputsFailPreflight() {
        Assert.Throws<InvalidOperationException>(() => new XboxPlatformCookWorkItemExecutor().Execute([Item("texture"), Item("texture/child.heasset")], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "texture")));
    }

    /// <summary>Rejects existing file ancestors without modifying unrelated prior outputs.</summary>
    [Fact]
    public void ExistingFileAncestorFailsPreflight() {
        Assert.Throws<InvalidOperationException>(() => new XboxPlatformCookWorkItemExecutor().Execute([Item("first.heasset"), Item("previous.txt/child.heasset")], [], Package, CancellationToken.None));
        Assert.False(File.Exists(Path.Combine(Package, "first.heasset")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Stages legacy atlas settings without replacing their authored generic format with the new default.</summary>
    [Fact]
    public void LegacyFontAtlasSettingsRemainGenericThroughStaging() {
        var settings = XboxTextureCookSettingsSerializer.CreateDefaults();
        settings.ColorFormatId = "Indexed4";
        settings.IndexingMethodId = "QuantizedIndexed";
        var item = new PlatformCookWorkItem("atlas", Source, "font-atlas-texture", "xbox", "runtime-texture", "fonts/legacy.heasset", "fonts/legacy", "sourcehash", "settingshash", XboxTextureCookSettingsSerializer.Serialize(settings), []);
        var paths = new XboxPlatformCookWorkItemExecutor().Execute([item], [], Package, CancellationToken.None);
        string stage = Path.Combine(Root, "disc");
        new XboxContentStager().Stage([], paths, Package, stage, CancellationToken.None);
        using TextureAsset cooked = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(Path.Combine(stage, paths[0]))));
        Assert.Equal(TextureAssetColorFormat.Indexed4, cooked.ColorFormat);
        using TextureAsset original = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(Source)));
        Assert.Equal(original.Colors, TextureAssetPixelCodec.DecodeToRgba32(cooked));
    }

    /// <summary>Checks unknown cook kinds, platforms and settings cannot produce output.</summary>
    [Theory]
    [InlineData("material", "xbox", "runtime-texture")]
    [InlineData("texture", "n64", "runtime-texture")]
    [InlineData("texture", "xbox", "runtime-material")]
    public void UnsupportedWorkItemContractsFail(string kind, string platform, string artifact) {
        var item = new PlatformCookWorkItem("item", Source, kind, platform, artifact, "output.heasset", "output", "sourcehash", "settingshash", DefaultSettings(), []);
        Assert.Throws<InvalidOperationException>(() => new XboxPlatformCookWorkItemExecutor().Execute([item], [], Package, CancellationToken.None));
    }

    /// <summary>Checks cancelled batches preserve outputs even when there are no cook work items.</summary>
    [Fact]
    public void PrecancelledBatchPreservesPackage() {
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();
        Assert.Throws<OperationCanceledException>(() => new XboxPlatformCookWorkItemExecutor().Execute([], [], Package, cancellation.Token));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(Package, "previous.txt")));
    }

    /// <summary>Removes only this test's verified contained fixture directory.</summary>
    public void Dispose() {
        string full = Path.GetFullPath(Root);
        if (!full.StartsWith(Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory)) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Fixture root escaped test output.");
        if (Directory.Exists(full)) Directory.Delete(full, true);
    }

    /// <summary>Creates one editor-shaped texture work item with native Xbox defaults.</summary>
    PlatformCookWorkItem Item(string path, string kind = "texture", string source = null) => new(path, source ?? Source, kind, "xbox", "runtime-texture", path, path, "sourcehash", "settingshash", DefaultSettings(), []);

    /// <summary>Serializes the same defaults published by the platform capability.</summary>
    static string DefaultSettings() => XboxTextureCookSettingsSerializer.Serialize(XboxTextureCookSettingsSerializer.CreateDefaults());
}
