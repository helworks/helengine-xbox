namespace helengine.xbox.shaderprobe;

/// <summary>Holds one validated native shader binary and the retained build evidence.</summary>
public sealed class XboxShaderBuildResult {
    /// <summary>Copies ownership of the native output and translation evidence into the result.</summary>
    public XboxShaderBuildResult(byte[] binary, string jobRoot, XboxShaderProbeOutput translation) {
        Binary = binary;
        JobRoot = jobRoot;
        Translation = translation;
    }
    /// <summary>Gets the HXS1 bytes validated by the native extractor.</summary>
    public byte[] Binary { get; }
    /// <summary>Gets the unique retained job directory for compiler diagnostics.</summary>
    public string JobRoot { get; }
    /// <summary>Gets the source and reflected interface used for the compilation.</summary>
    public XboxShaderProbeOutput Translation { get; }
    /// <summary>Gets compiler-assigned vertex constants; empty for fragment stages.</summary>
    public IReadOnlyList<XboxVertexConstantBinding> VertexConstants { get; init; } = Array.Empty<XboxVertexConstantBinding>();
    /// <summary>Gets the immutable compiler-owned literal rows stored in vertex artifacts.</summary>
    public IReadOnlyList<XboxVertexLiteralConstant> VertexLiterals { get; init; } = Array.Empty<XboxVertexLiteralConstant>();
    /// <summary>Gets compiler-confirmed physical attribute and varying routes.</summary>
    public IReadOnlyList<XboxShaderInterfaceBinding> InterfaceBindings { get; init; } = Array.Empty<XboxShaderInterfaceBinding>();
    /// <summary>Gets the compiler-assigned global fragment constants; empty for vertex stages.</summary>
    public IReadOnlyList<XboxPixelConstantBinding> PixelConstants { get; init; } = Array.Empty<XboxPixelConstantBinding>();
    /// <summary>Gets original image/sampler pairs with compiler-assigned physical units.</summary>
    public IReadOnlyList<XboxTextureBinding> TextureBindings { get; init; } = Array.Empty<XboxTextureBinding>();
}
