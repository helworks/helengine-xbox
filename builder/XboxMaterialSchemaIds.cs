namespace helengine.xbox.builder;

/// <summary>Stable editor field identifiers and values for the Original Xbox material schema.</summary>
public static class XboxMaterialSchemaIds {
    /// <summary>Schema id for the single opaque unlit Xbox material.</summary>
    public const string StandardSchemaId = "xbox-standard-v1";

    /// <summary>Graphics profile and cooked renderer family id shared by every Xbox build profile.</summary>
    public const string StandardGraphicsProfileId = "xbox-standard";

    /// <summary>Field id for the opaque base color.</summary>
    public const string BaseColorFieldId = "base-color";

    /// <summary>
    /// Field id for the texture binding. The first Xbox milestone renders untextured geometry, so this field
    /// exists to make an authored texture fail loudly rather than be silently dropped during cooking.
    /// </summary>
    public const string TextureRelativePathFieldId = "texture-relative-path";

    /// <summary>Field id selecting lit or unlit rendering. Only unlit is implemented so far.</summary>
    public const string LightingModeFieldId = "lighting-mode";

    /// <summary>Lighting value that draws the base color with no light contribution.</summary>
    public const string LightingUnlitValue = "unlit";

    /// <summary>Field id for the transparency mode, which currently only accepts opaque.</summary>
    public const string TransparencyFieldId = "transparency";

    /// <summary>Transparency value for fully opaque rendering.</summary>
    public const string TransparencyOpaqueValue = "opaque";
}
