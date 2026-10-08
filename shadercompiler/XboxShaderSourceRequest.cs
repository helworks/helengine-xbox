namespace helengine.xbox.shaderprobe;

/// <summary>Supplies source, selected entry, variant macros and active include resolution to shaderc.</summary>
public sealed class XboxShaderSourceRequest {
    /// <summary>Captures required inputs and makes a stable copy of macro definitions.</summary>
    public XboxShaderSourceRequest(XboxShaderSource source, string entryPoint, bool vertexStage,
                                  IReadOnlyDictionary<string, string> defines,
                                  Func<string, string, XboxShaderSource> includeResolver,
                                  bool optimize = false, bool warningsAsErrors = false) {
        Source = source ?? throw new ArgumentNullException(nameof(source));
        if (string.IsNullOrWhiteSpace(entryPoint)) throw new ArgumentException("Entry point is required.", nameof(entryPoint));
        EntryPoint = entryPoint;
        VertexStage = vertexStage;
        Defines = new System.Collections.ObjectModel.ReadOnlyDictionary<string, string>(
            new Dictionary<string, string>(defines ?? throw new ArgumentNullException(nameof(defines)), StringComparer.Ordinal));
        IncludeResolver = includeResolver ?? throw new ArgumentNullException(nameof(includeResolver));
        Optimize = optimize;
        WarningsAsErrors = warningsAsErrors;
    }
    /// <summary>Gets the root authored source.</summary>
    public XboxShaderSource Source { get; }
    /// <summary>Gets the authored entry point name.</summary>
    public string EntryPoint { get; }
    /// <summary>Gets whether this entry uses the vertex profile rather than pixel.</summary>
    public bool VertexStage { get; }
    /// <summary>Gets copied variant macros.</summary>
    public IReadOnlyDictionary<string, string> Defines { get; }
    /// <summary>Gets the callback for includes actually selected by the preprocessor.</summary>
    public Func<string, string, XboxShaderSource> IncludeResolver { get; }
    /// <summary>Gets whether shaderc should optimize the selected entry.</summary>
    public bool Optimize { get; }
    /// <summary>Gets whether frontend warnings fail compilation.</summary>
    public bool WarningsAsErrors { get; }
}
