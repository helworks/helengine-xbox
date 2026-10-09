using helengine;
using helengine.xbox.builder;

namespace helengine.xbox.builder.tests;

/// <summary>Records staged atlas data at the native-build boundary without executing a console toolchain.</summary>
internal sealed class TextureRecordingNativeBuildExecutor : IXboxNativeBuildExecutor {
    /// <summary>Counts only native invocations that the public builder reaches after staging.</summary>
    public int Calls { get; set; }
    /// <summary>Records the format actually read from staged content, rather than the requested setting.</summary>
    public TextureAssetColorFormat StagedTextureFormat { get; set; }
    /// <summary>Reads staged texture bytes and creates the output marker expected by the build report.</summary>
    public void Build(string generatedCoreCppRootPath, string stagedContentRootPath, string outputRootPath, string nativeObjectCacheRootPath, CancellationToken cancellationToken) {
        cancellationToken.ThrowIfCancellationRequested();
        Calls++;
        using TextureAsset texture = (TextureAsset)helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(Path.Combine(stagedContentRootPath, "fonts", "atlas.heasset")));
        StagedTextureFormat = texture.ColorFormat;
        File.WriteAllText(Path.Combine(outputRootPath, "game.iso"), "test-native-boundary-marker");
    }
}
