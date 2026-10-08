using helengine.xbox.shaderprobe;

namespace helengine.xbox.builder;

/// <summary>Adapts the verified Xbox compiler to the engine's single-entry compilation contract.</summary>
public sealed class XboxShaderBackend : IShaderBackend, IShaderBackendCachePolicy {
    /// <summary>Holds an explicitly configured checkout root, or resolves the established environment setting at compile time.</summary>
    readonly string RepositoryRoot;
    /// <summary>Creates a discoverable backend without requiring SDK tools during registry discovery.</summary>
    public XboxShaderBackend() { }
    /// <summary>Creates a backend for one explicit Xbox checkout.</summary>
    public XboxShaderBackend(string repositoryRoot) {
        if (string.IsNullOrWhiteSpace(repositoryRoot)) throw new ArgumentException("Xbox checkout root is required.", nameof(repositoryRoot));
        RepositoryRoot = Path.GetFullPath(repositoryRoot);
    }
    /// <summary>Gets the original Xbox target, distinct from Xbox 360.</summary>
    public ShaderCompileTarget Target => ShaderCompileTarget.Xbox;
    /// <summary>Gets the accepted frontend stage/model envelope; NV2A subset validation further restricts expressions.</summary>
    public ShaderBackendCapabilities Capabilities => new(new ShaderModel(2, 0), new ShaderModel(5, 0), [ShaderStage.Vertex, ShaderStage.Pixel], false);
    /// <summary>Bypasses the shared root-source-only cache because active includes and native tools participate in Xbox identity.</summary>
    public bool UseSharedCache => false;
    /// <summary>Compiles with the caller's resolver and returns typed engine metadata plus a validated native artifact.</summary>
    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(includeResolver);
        if (request.Target != Target) throw new NotSupportedException("XBOX_SHADER_TARGET: expected Original Xbox.");
        if (request.Stage != ShaderStage.Vertex && request.Stage != ShaderStage.Pixel)
            throw new NotSupportedException("XBOX_SHADER_STAGE: only vertex and pixel stages are supported.");
        if (request.ShaderModel.Major < 2 || request.ShaderModel.Major > 5 || request.ShaderModel.Minor != 0)
            throw new NotSupportedException("XBOX_SHADER_MODEL: frontend models 2.0 through 5.0 are supported within the NV2A subset.");
        string root = RepositoryRoot ?? Environment.GetEnvironmentVariable("HELENGINE_XBOX_REPOSITORY_ROOT");
        if (string.IsNullOrWhiteSpace(root)) throw new InvalidOperationException("HELENGINE_XBOX_REPOSITORY_ROOT must identify the Xbox checkout.");
        Dictionary<string, string> defines = new(StringComparer.Ordinal);
        foreach (ShaderDefine define in request.Defines) {
            if (!defines.TryAdd(define.Name, define.Value)) throw new ArgumentException("Duplicate shader macro: " + define.Name);
        }
        XboxShaderSourceRequest sourceRequest = new(new XboxShaderSource(request.Source.Path, request.Source.Source), request.EntryPoint,
            request.Stage == ShaderStage.Vertex, defines, (requesting, include) => {
                ShaderIncludeResult resolved = includeResolver.Resolve(requesting, include);
                return new XboxShaderSource(resolved.Path, resolved.Source);
            }, request.Options.Optimize, request.Options.TreatWarningsAsErrors);
        XboxShaderBuildResult compiled = new XboxShaderToolchainCompiler().Compile(root, sourceRequest);
        List<ShaderBinding> bindings = [];
        List<ShaderVertexElement> inputs = [], outputs = [];
        foreach (XboxShaderReflectionItem item in compiled.Translation.Metadata) {
            if (item.Kind == "StageInput") inputs.Add(Element(item));
            else if (item.Kind == "StageOutput") outputs.Add(Element(item));
            else {
                ShaderResourceType type = item.Kind switch {
                    "UniformBuffer" => ShaderResourceType.ConstantBuffer,
                    "SeparateImage" => ShaderResourceType.Texture2D,
                    "SeparateSamplers" => ShaderResourceType.Sampler,
                    _ => throw new NotSupportedException("XBOX_SHADER_RESOURCE: " + item.Kind)
                };
                bindings.Add(new ShaderBinding(item.Name, type, item.Set == 0 ? request.Options.BindingPolicy.DefaultSpace : item.Set,
                    request.Options.BindingPolicy.GetSlot(type, item.Binding), item.Size,
                    item.Members.Select(member => new ShaderConstantMember(member.Name, member.Format, member.Offset, member.Size)).ToArray()));
            }
        }
        ShaderProgramDefinition definition = new(request.ProgramName, request.Stage, request.EntryPoint,
            bindings.ToArray(), inputs.ToArray(), outputs.ToArray(),
            [new ShaderVariant(request.Variant, request.Defines.Select(define => define.Name + "=" + define.Value).ToArray())]);
        ShaderCompiledBinary binary = new(Target, request.Stage, request.EntryPoint, request.Variant, compiled.Binary);
        return new ShaderCompileResult(request, definition, binary, [], true);
    }
    /// <summary>Separates an authored semantic name from its numeric index without changing the reflected format.</summary>
    static ShaderVertexElement Element(XboxShaderReflectionItem item) {
        string semantic = item.Semantic.ToUpperInvariant();
        int split = semantic.Length;
        while (split > 0 && char.IsAsciiDigit(semantic[split - 1])) --split;
        int index = split == semantic.Length ? 0 : int.Parse(semantic.AsSpan(split), System.Globalization.CultureInfo.InvariantCulture);
        return new ShaderVertexElement(semantic[..split], index, item.Format);
    }
}
