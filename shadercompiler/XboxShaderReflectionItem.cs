namespace helengine.xbox.shaderprobe;

/// <summary>Copies one authored interface/resource or constant member from native reflection.</summary>
public sealed class XboxShaderReflectionItem {
    /// <summary>Gets the resource category, or Member for a constant buffer field.</summary>
    public string Kind { get; init; }
    /// <summary>Gets the authored resource or field name.</summary>
    public string Name { get; init; }
    /// <summary>Gets the authored HLSL semantic, when this is an interface value.</summary>
    public string Semantic { get; init; }
    /// <summary>Gets the reflected numeric type or resource classification.</summary>
    public string Format { get; init; }
    /// <summary>Gets the authored descriptor set.</summary>
    public int Set { get; init; }
    /// <summary>Gets the authored resource slot.</summary>
    public int Binding { get; init; }
    /// <summary>Gets the declared byte size for buffers and members.</summary>
    public int Size { get; init; }
    /// <summary>Gets the byte offset within a constant buffer.</summary>
    public int Offset { get; init; }
    /// <summary>Gets the constant buffer members, empty for other categories.</summary>
    public XboxShaderReflectionItem[] Members { get; init; } = [];
}
