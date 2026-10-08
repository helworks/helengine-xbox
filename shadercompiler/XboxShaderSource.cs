namespace helengine.xbox.shaderprobe;

/// <summary>Identifies one immutable authored source or resolved include.</summary>
public sealed class XboxShaderSource {
    /// <summary>Requires a diagnostic identity and preserves the supplied source text.</summary>
    public XboxShaderSource(string path, string text) {
        if (string.IsNullOrWhiteSpace(path)) throw new ArgumentException("Source path is required.", nameof(path));
        Path = path;
        Text = text ?? throw new ArgumentNullException(nameof(text));
    }
    /// <summary>Gets the resolver identity, independent from job directories.</summary>
    public string Path { get; }
    /// <summary>Gets the exact authored contents.</summary>
    public string Text { get; }
}
