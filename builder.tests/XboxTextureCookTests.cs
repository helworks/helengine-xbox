using helengine;
using helengine.editor;
using helengine.xbox.builder;

namespace helengine.xbox.builder.tests;

/// <summary>Exercises catalog capabilities, strict settings, resizing, palette preparation and native serialization.</summary>
public sealed class XboxTextureCookTests {
    /// <summary>Supplies every advertised native hardware format, including lossy, palette and DXT formats.</summary>
    public static IEnumerable<object[]> NativeFormats => XboxNativeTextureFormatCatalog.Formats.Select(format => new object[] { format.Id });

    /// <summary>Ensures both texture kinds publish precisely the catalog combinations and preserve atlas resolution by default.</summary>
    [Fact]
    public void CapabilitiesExposeAllFormatsAndFullResolutionAtlasDefaults() {
        var definition = XboxPlatformDefinitionFactory.Create();
        Assert.Equal(42, XboxNativeTextureFormatCatalog.Formats.Length);
        Assert.Equal(2, definition.AssetCookCapabilities.Length);
        foreach (var capability in definition.AssetCookCapabilities) {
            Assert.Equal("BuilderOwned", capability.OwnershipKind.ToString());
            Assert.Equal("runtime-texture", capability.TargetArtifactKind);
            var defaults = XboxTextureCookSettingsSerializer.Deserialize(capability.DefaultSerializedPlatformSettings);
            Assert.Equal(4096, defaults.MaxResolution);
            Assert.Equal("Xbox.Linear.A8R8G8B8", defaults.ColorFormatId);
            Assert.Equal(TextureAssetAlphaPrecision.A8, defaults.AlphaPrecision);
            Assert.Equal(XboxNativeTextureFormatCatalog.Formats.Select(format => format.Id), capability.TextureFormatCapabilities.SupportedColorFormatIds.Take(42));
            Assert.Equal(53, capability.TextureFormatCapabilities.SupportedColorFormatIds.Length);
            foreach (var format in XboxNativeTextureFormatCatalog.Formats) {
                Assert.Equal(format.SupportedAlphaPrecisions,
                    capability.TextureFormatCapabilities.SupportedCombinations.Where(item => item.ColorFormatId == format.Id).Select(item => item.AlphaPrecision));
            }
        }
        Assert.Contains(definition.AssetCookCapabilities, item => item.SourceAssetKind == "font-atlas-texture");
    }

    /// <summary>Verifies each advertised format cooks and survives HELE serialization with matching native layout and identity.</summary>
    [Theory]
    [MemberData(nameof(NativeFormats))]
    public void EveryNativeFormatCooksAndSerializesWithoutMutatingSource(string formatId) {
        using TextureAsset source = Source(5, 3);
        byte[] sourceBytes = (byte[])source.Colors.Clone();
        XboxNativeTextureFormatCatalog.TryGetFormat(formatId, out var format);
        var settings = XboxTextureCookSettingsSerializer.CreateDefaults();
        settings.ColorFormatId = formatId;
        settings.AlphaPrecision = format.DefaultAlphaPrecision;
        using TextureAsset cooked = new XboxTextureCooker().Cook(source, settings);
        Assert.Equal(TextureAssetColorFormat.XboxNative, cooked.ColorFormat);
        Assert.Equal(source.Id, cooked.Id);
        Assert.Equal(source.RuntimeAssetId, cooked.RuntimeAssetId);
        Assert.Equal(source.AuthoringAssetId, cooked.AuthoringAssetId);
        Assert.Equal(source.FormerAuthoringAssetIds, cooked.FormerAuthoringAssetIds);
        Assert.NotSame(source.FormerAuthoringAssetIds, cooked.FormerAuthoringAssetIds);
        Assert.Equal(sourceBytes, source.Colors);
        using TextureAsset read = Assert.IsType<TextureAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(helengine.files.AssetSerializer.SerializeToBytes(cooked)));
        Assert.Equal(cooked.Colors, read.Colors);
        Assert.Equal(cooked.PaletteColors, read.PaletteColors);
        var layout = XboxNativeTextureCodec.ReadLayout(read);
        Assert.Equal(format.HardwareFormat, layout.Format.HardwareFormat);
        Assert.Equal(5 * 3 * 4, TextureAssetPixelCodec.DecodeToRgba32(read).Length);
    }

    /// <summary>Checks resize happens in logical RGBA space before pitched native storage is allocated.</summary>
    [Fact]
    public void ResizePreservesAspectAndSourceBeforeNativeEncoding() {
        using TextureAsset source = Source(8, 4);
        var settings = XboxTextureCookSettingsSerializer.CreateDefaults();
        settings.MaxResolution = 4;
        using TextureAsset cooked = new XboxTextureCooker().Cook(source, settings);
        Assert.Equal((ushort)4, cooked.Width);
        Assert.Equal((ushort)2, cooked.Height);
        Assert.Equal((ushort)8, source.Width);
        byte[] decoded = XboxNativeTextureCodec.Decode(cooked);
        Assert.Equal(source.Colors.AsSpan(0, 4).ToArray(), decoded.AsSpan(0, 4).ToArray());
        Assert.Equal(64, XboxNativeTextureCodec.ReadLayout(cooked).PitchBytes);
    }

    /// <summary>Checks generated atlas dimensions below the default limit are retained exactly.</summary>
    [Fact]
    public void FontAtlasDefaultKeepsAuthoredDimensions() {
        using TextureAsset source = Source(512, 256);
        using TextureAsset cooked = new XboxTextureCooker().Cook(source, XboxTextureCookSettingsSerializer.CreateDefaults());
        Assert.Equal(source.Width, cooked.Width);
        Assert.Equal(source.Height, cooked.Height);
    }

    /// <summary>Rejects malformed and unsupported settings rather than falling back to the default format.</summary>
    [Theory]
    [InlineData("{}")]
    [InlineData("[]")]
    [InlineData("{\"maxResolution\":4096,\"colorFormat\":\"GxRgb5A3\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":-1,\"colorFormat\":\"Xbox.Linear.A8R8G8B8\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":4097,\"colorFormat\":\"Xbox.Linear.A8R8G8B8\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":4096,\"colorFormat\":\"Xbox.Linear.A8R8G8B8\",\"alphaPrecision\":\"255\"}")]
    [InlineData("{\"maxResolution\":4096,\"colorFormat\":\"Xbox.Linear.A8R8G8B8\",\"alphaPrecision\":\"A8\",\"extra\":true}")]
    [InlineData("{\"maxResolution\":4096,\"maxResolution\":32,\"colorFormat\":\"Xbox.Linear.A8R8G8B8\",\"alphaPrecision\":\"A8\"}")]
    [InlineData("{\"maxResolution\":4096,\"colorFormat\":\"Xbox.Linear.A8R8G8B8\",\"alphaPrecision\":\"A8\",\"indexingMethod\":\"QuantizedIndexed\"}")]
    [InlineData("{\"maxResolution\":4096,\"colorFormat\":\"Xbox.Swizzled.I8_A8R8G8B8\",\"alphaPrecision\":\"A8\",\"indexingMethod\":\"Unknown\"}")]
    public void InvalidSettingsFail(string json) => Assert.Throws<InvalidOperationException>(() => XboxTextureCookSettingsSerializer.Deserialize(json));

    /// <summary>Checks the palette policy resolves the implemented quantizer and preserves a small exact color set.</summary>
    [Fact]
    public void PaletteEncodingUsesQuantizedRgbaPalette() {
        using TextureAsset source = Source(2, 1);
        var settings = XboxTextureCookSettingsSerializer.CreateDefaults();
        settings.ColorFormatId = "Xbox.Swizzled.I8_A8R8G8B8";
        settings.IndexingMethodId = TextureAssetIndexingMethod.QuantizedIndexed.ToString();
        using TextureAsset cooked = new XboxTextureCooker().Cook(source, settings);
        Assert.Equal(source.Colors, XboxNativeTextureCodec.Decode(cooked));
        Assert.Equal(1024, cooked.PaletteColors.Length);
        Assert.Equal(256, XboxNativeTextureCodec.ReadLayout(cooked).PaletteEntryCount);
    }

    /// <summary>Preserves authored generic settings and uncapped resizing instead of silently converting them to a native default.</summary>
    [Theory]
    [InlineData("Rgba32")]
    [InlineData("Indexed4")]
    [InlineData("Indexed8")]
    public void AuthoredGenericFormatsRemainSupported(string formatId) {
        using TextureAsset source = Source(4, 2);
        byte[] bytes = (byte[])source.Colors.Clone();
        var settings = XboxTextureCookSettingsSerializer.CreateDefaults();
        settings.MaxResolution = 0;
        settings.ColorFormatId = formatId;
        if (formatId != "Rgba32") settings.IndexingMethodId = "QuantizedIndexed";
        var parsed = XboxTextureCookSettingsSerializer.Deserialize(XboxTextureCookSettingsSerializer.Serialize(settings));
        using TextureAsset cooked = new XboxTextureCooker().Cook(source, parsed);
        Assert.Equal(formatId, cooked.ColorFormat.ToString());
        Assert.Equal(bytes, TextureAssetPixelCodec.DecodeToRgba32(cooked));
        Assert.Equal(bytes, source.Colors);
        Assert.NotSame(source, cooked);
    }

    /// <summary>Constructs varied RGBA pixels with stable authored and runtime identity for lifecycle assertions.</summary>
    internal static TextureAsset Source(ushort width, ushort height) {
        byte[] colors = new byte[width * height * 4];
        for (int offset = 0; offset < colors.Length; offset += 4) {
            colors[offset] = (byte)(offset * 17);
            colors[offset + 1] = (byte)(255 - offset);
            colors[offset + 2] = (byte)(offset * 7);
            colors[offset + 3] = 255;
        }
        return new TextureAsset { Id = "textures/source", RuntimeAssetId = 123, AuthoringAssetId = "authored-id", FormerAuthoringAssetIds = ["former-id"], Width = width, Height = height, Colors = colors, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8 };
    }
}
