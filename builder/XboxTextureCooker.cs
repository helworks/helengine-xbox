using helengine.editor;

namespace helengine.xbox.builder;

/// <summary>Resizes a private RGBA copy before encoding the requested NV2A texture payload.</summary>
public sealed class XboxTextureCooker {
    /// <summary>Shares the editor's aspect-preserving resize implementation with platform cooks.</summary>
    readonly TextureAssetProcessor Processor = new();

    /// <summary>Builds the RGBA palette required by Xbox P8 without changing the source texture.</summary>
    readonly TextureAssetIndexedQuantizer Quantizer = new();

    /// <summary>Produces a new native texture while retaining authored identity and leaving source bytes intact.</summary>
    public TextureAsset Cook(TextureAsset source, TextureAssetProcessorSettings settings) {
        ArgumentNullException.ThrowIfNull(source);
        XboxTextureCookSettingsSerializer.Validate(settings);
        TextureAsset rgba = new() {
            Width = source.Width, Height = source.Height,
            ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8,
            Colors = TextureAssetPixelCodec.DecodeToRgba32(source), PaletteColors = Array.Empty<byte>()
        };
        CopyIdentity(source, rgba);
        TextureAsset resized = null;
        TextureAsset indexed = null;
        TextureAsset result = null;
        try {
            resized = Processor.Apply(rgba, new TextureAssetProcessorSettings {
                MaxResolution = settings.MaxResolution, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8
            });
            if (resized.Width > 4096 || resized.Height > 4096) throw new InvalidOperationException("Xbox cooked texture dimensions cannot exceed 4096 pixels.");
            if (TextureAssetProcessorSettings.TryResolveGenericColorFormat(settings.ColorFormatId, out TextureAssetColorFormat genericFormat)) {
                result = Processor.Apply(resized, new TextureAssetProcessorSettings {
                    MaxResolution = 0, ColorFormat = genericFormat, AlphaPrecision = settings.AlphaPrecision, IndexingMethodId = settings.IndexingMethodId
                });
                CopyIdentity(source, result);
                return result;
            }
            XboxNativeTextureFormatCatalog.TryGetFormat(settings.ColorFormatId, out XboxNativeTextureFormatDefinition format);
            TextureAsset encodingSource = resized;
            if (format.IsPaletted) {
                indexed = Quantizer.Quantize(resized, 256, TextureAssetColorFormat.Indexed8, settings.AlphaPrecision);
                CopyIdentity(source, indexed);
                encodingSource = indexed;
            }
            result = XboxNativeTextureCodec.Encode(encodingSource, settings.ColorFormatId, settings.AlphaPrecision);
            CopyIdentity(source, result);
            return result;
        } finally {
            indexed?.Dispose();
            if (resized != null && !ReferenceEquals(resized, rgba) && !ReferenceEquals(resized, result)) resized.Dispose();
            if (!ReferenceEquals(rgba, result)) rgba.Dispose();
        }
    }

    /// <summary>Copies identity metadata while keeping the mutable alias-array container independent.</summary>
    internal static void CopyIdentity(TextureAsset source, TextureAsset destination) {
        destination.Id = source.Id;
        destination.RuntimeAssetId = source.RuntimeAssetId;
        destination.AuthoringAssetId = source.AuthoringAssetId;
        destination.FormerAuthoringAssetIds = source.FormerAuthoringAssetIds == null ? Array.Empty<string>() : (string[])source.FormerAuthoringAssetIds.Clone();
        destination.IsEngineOwned = source.IsEngineOwned;
    }
}
