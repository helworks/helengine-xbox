namespace helengine.xbox.shaderprobe;

/// <summary>Owns copied translation evidence after native compiler contexts are disposed.</summary>
public sealed class XboxShaderProbeOutput {
    /// <summary>Constructs one successful experimental translation.</summary>
    public XboxShaderProbeOutput(byte[] spirv, string source, string entryPoint, string interfaceReport, string adapterSource) {
        Spirv = spirv;
        Source = source;
        EntryPoint = entryPoint;
        InterfaceReport = interfaceReport;
        AdapterSource = adapterSource;
    }
    /// <summary>Gets the validated frontend module.</summary>
    public byte[] Spirv { get; }
    /// <summary>Gets unmodified compiler-emitted legacy HLSL for testing with Cg.</summary>
    public string Source { get; }
    /// <summary>Gets the emitted stage entry point.</summary>
    public string EntryPoint { get; }
    /// <summary>Gets reflected interface evidence.</summary>
    public string InterfaceReport { get; }
    /// <summary>Gets the separate authored-semantic entry adapter, without its include directive.</summary>
    public string AdapterSource { get; }
    /// <summary>Gets typed active resource and interface metadata copied before native emission.</summary>
    public XboxShaderReflectionItem[] Metadata { get; internal set; } = [];
    /// <summary>Gets original texture/sampler identities behind emitted legacy combined resources.</summary>
    public IReadOnlyList<XboxCombinedSampler> CombinedSamplers { get; internal set; } = Array.Empty<XboxCombinedSampler>();
    /// <summary>Gets hashes of the authored root and only the active resolved includes.</summary>
    public IReadOnlyDictionary<string, string> DependencyHashes { get; private set; }
    /// <summary>Gets identity covering source closure, entry, stage, macros and frontend options.</summary>
    public string SourceIdentity { get; private set; }
    /// <summary>Copies dependency hashes and computes a stable identity independent of native job paths.</summary>
    internal void RecordDependencies(XboxShaderSourceRequest request, IReadOnlyDictionary<string, string> sources) {
        SortedDictionary<string, string> hashes = new(StringComparer.Ordinal);
        foreach (var source in sources)
            hashes.Add(source.Key, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(source.Value))));
        DependencyHashes = new System.Collections.ObjectModel.ReadOnlyDictionary<string, string>(hashes);
        string identity = System.Text.Json.JsonSerializer.Serialize(new {
            sources = hashes, request.EntryPoint, request.VertexStage, request.Optimize, request.WarningsAsErrors,
            defines = new SortedDictionary<string, string>(request.Defines.ToDictionary(pair => pair.Key, pair => pair.Value), StringComparer.Ordinal)
        });
        SourceIdentity = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(identity)));
    }
}
