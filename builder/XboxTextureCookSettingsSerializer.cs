using System.Text.Json;
using helengine.editor;

namespace helengine.xbox.builder;

/// <summary>Serializes and validates native Xbox texture settings carried by editor cook work items.</summary>
public static class XboxTextureCookSettingsSerializer {
    /// <summary>Creates defaults that retain full-resolution font atlases and eight-bit alpha.</summary>
    public static TextureAssetProcessorSettings CreateDefaults() => new() {
        MaxResolution = 4096,
        ColorFormatId = "Xbox.Linear.A8R8G8B8",
        AlphaPrecision = TextureAssetAlphaPrecision.A8,
        IndexingMethodId = string.Empty
    };

    /// <summary>Writes the platform-owned format id without routing it through the generic format enum.</summary>
    public static string Serialize(TextureAssetProcessorSettings settings) {
        Validate(settings);
        return JsonSerializer.Serialize(new Dictionary<string, object> {
            ["maxResolution"] = settings.MaxResolution,
            ["colorFormat"] = settings.ColorFormatId,
            ["alphaPrecision"] = settings.AlphaPrecision.ToString(),
            ["indexingMethod"] = settings.IndexingMethodId ?? string.Empty
        });
    }

    /// <summary>Rejects missing, duplicate, unknown, or unsupported settings before cooking any pixels.</summary>
    public static TextureAssetProcessorSettings Deserialize(string serializedSettings) {
        if (string.IsNullOrWhiteSpace(serializedSettings)) throw new ArgumentException("Xbox texture settings are required.", nameof(serializedSettings));
        using JsonDocument document = JsonDocument.Parse(serializedSettings);
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object) throw new InvalidOperationException("Xbox texture settings must be an object.");
        HashSet<string> keys = new(StringComparer.Ordinal);
        foreach (JsonProperty property in root.EnumerateObject()) {
            if (!keys.Add(property.Name)) throw new InvalidOperationException($"Duplicate Xbox texture setting '{property.Name}'.");
            if (property.Name != "maxResolution" && property.Name != "colorFormat" && property.Name != "alphaPrecision" && property.Name != "indexingMethod") {
                throw new InvalidOperationException($"Unknown Xbox texture setting '{property.Name}'.");
            }
        }
        if (!root.TryGetProperty("maxResolution", out JsonElement maximum) || !root.TryGetProperty("colorFormat", out JsonElement format) || !root.TryGetProperty("alphaPrecision", out JsonElement alpha)) {
            throw new InvalidOperationException("Xbox texture settings require maxResolution, colorFormat, and alphaPrecision.");
        }
        string alphaName = alpha.GetString();
        if (!Enum.TryParse(alphaName, false, out TextureAssetAlphaPrecision precision) || precision.ToString() != alphaName) {
            throw new InvalidOperationException($"Unsupported Xbox alpha precision '{alphaName}'.");
        }
        TextureAssetProcessorSettings settings = new() {
            MaxResolution = maximum.GetInt32(),
            ColorFormatId = format.GetString(),
            AlphaPrecision = precision,
            IndexingMethodId = root.TryGetProperty("indexingMethod", out JsonElement indexing) ? indexing.GetString() : string.Empty
        };
        Validate(settings);
        return settings;
    }

    /// <summary>Checks catalog format/alpha compatibility and restricts palette indexing to the implemented quantizer.</summary>
    public static void Validate(TextureAssetProcessorSettings settings) {
        ArgumentNullException.ThrowIfNull(settings);
        if (settings.MaxResolution < 0 || settings.MaxResolution > 4096) throw new InvalidOperationException("Xbox maxResolution must be zero (uncapped) or between 1 and 4096.");
        bool isPaletted;
        if (TextureAssetProcessorSettings.TryResolveGenericColorFormat(settings.ColorFormatId, out TextureAssetColorFormat genericFormat)) {
            if (!TextureAssetPixelCodec.IsAlphaPrecisionSupported(genericFormat, settings.AlphaPrecision)) throw new InvalidOperationException($"Xbox generic format '{genericFormat}' does not support '{settings.AlphaPrecision}' alpha.");
            isPaletted = genericFormat == TextureAssetColorFormat.Indexed4 || genericFormat == TextureAssetColorFormat.Indexed8;
        } else if (XboxNativeTextureFormatCatalog.TryGetFormat(settings.ColorFormatId, out XboxNativeTextureFormatDefinition format)) {
            if (!format.SupportsAlpha(settings.AlphaPrecision)) throw new InvalidOperationException($"Xbox format '{format.Id}' does not support '{settings.AlphaPrecision}' alpha.");
            isPaletted = format.IsPaletted;
        } else {
            throw new InvalidOperationException($"Unsupported Xbox texture format '{settings.ColorFormatId}'.");
        }
        string indexing = settings.IndexingMethodId ?? string.Empty;
        if (isPaletted) {
            if (indexing.Length != 0 && indexing != TextureAssetIndexingMethod.QuantizedIndexed.ToString()) throw new InvalidOperationException($"Unsupported Xbox indexing method '{indexing}'.");
        } else if (indexing.Length != 0) {
            throw new InvalidOperationException("Xbox indexingMethod is only supported for a palette format.");
        }
    }
}
