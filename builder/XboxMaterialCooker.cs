using System.Globalization;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.xbox.builder;

/// <summary>Cooks the single opaque unlit Xbox material schema.</summary>
public sealed class XboxMaterialCooker {
    public PlatformMaterialCookResult Cook(PlatformMaterialCookRequest request) {
        if (request == null) throw new ArgumentNullException(nameof(request));
        if (!string.Equals(request.TargetPlatformId, "xbox", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Xbox cannot cook materials for target platform '{request.TargetPlatformId}'.");
        }
        if (!string.Equals(request.SchemaId, XboxMaterialSchemaIds.StandardSchemaId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Xbox does not support material schema '{request.SchemaId}'.");
        }

        // Textures are not cooked yet. Fail rather than drop an authored binding silently, so a textured
        // material is reported at build time instead of rendering untextured on the console.
        string texture = Resolve(request.FieldValues, XboxMaterialSchemaIds.TextureRelativePathFieldId);
        if (!string.IsNullOrWhiteSpace(texture)) {
            throw new InvalidOperationException("Xbox xbox-standard-v1 does not support texture bindings in this milestone.");
        }

        string lighting = Resolve(request.FieldValues, XboxMaterialSchemaIds.LightingModeFieldId, XboxMaterialSchemaIds.LightingUnlitValue);
        if (!string.Equals(lighting, XboxMaterialSchemaIds.LightingUnlitValue, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Xbox xbox-standard-v1 renders unlit only; received lighting '{lighting}'.");
        }

        string transparency = Resolve(request.FieldValues, XboxMaterialSchemaIds.TransparencyFieldId, XboxMaterialSchemaIds.TransparencyOpaqueValue);
        if (!string.Equals(transparency, XboxMaterialSchemaIds.TransparencyOpaqueValue, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Xbox xbox-standard-v1 requires opaque materials.");
        }

        ParseColor(Resolve(request.FieldValues, XboxMaterialSchemaIds.BaseColorFieldId, "#FFFFFFFF"), out byte r, out byte g, out byte b, out byte a);
        if (a != 255) {
            throw new InvalidOperationException("Xbox xbox-standard-v1 requires an opaque base color (alpha FF).");
        }

        helengine.PlatformMaterialAsset cooked = new() {
            Id = request.MaterialAssetId,
            // This cooker emits the classic format even when shader batching is enabled.
            RendererFamilyId = XboxMaterialSchemaIds.StandardGraphicsProfileId,
            TextureRelativePath = string.Empty,
            DoubleSided = false,
            UseVertexColor = false,
            Lit = false,
            BaseColorR = r,
            BaseColorG = g,
            BaseColorB = b,
            BaseColorA = a
        };
        return new PlatformMaterialCookResult(helengine.files.AssetSerializer.SerializeToBytes(cooked, request.NumericPolicy), []);
    }

    static string Resolve(IReadOnlyDictionary<string, string> values, string key, string fallback = "") {
        if (values == null) throw new ArgumentNullException(nameof(values));
        return values.TryGetValue(key, out string value) && !string.IsNullOrWhiteSpace(value) ? value.Trim() : fallback;
    }

    static void ParseColor(string value, out byte r, out byte g, out byte b, out byte a) {
        if (string.IsNullOrWhiteSpace(value)) throw new InvalidOperationException("Xbox base color is required.");
        string normalized = value.Trim();
        if (normalized.StartsWith('#')) normalized = normalized[1..];
        if (normalized.Length == 6) normalized += "FF";
        if (normalized.Length != 8) throw new InvalidOperationException($"Xbox color '{value}' must use #RRGGBB or #RRGGBBAA format.");
        try {
            r = byte.Parse(normalized.AsSpan(0, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            g = byte.Parse(normalized.AsSpan(2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            b = byte.Parse(normalized.AsSpan(4, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            a = byte.Parse(normalized.AsSpan(6, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        } catch (FormatException exception) {
            throw new InvalidOperationException($"Xbox color '{value}' contains invalid hexadecimal channels.", exception);
        }
    }
}
