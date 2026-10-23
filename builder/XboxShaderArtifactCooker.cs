using System.Text.RegularExpressions;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.xbox.shaderprobe;

namespace helengine.xbox.builder;

/// <summary>Compiles material-selected HLSL snapshots into the raw shader packages consumed by the Xbox runtime.</summary>
public sealed class XboxShaderArtifactCooker {
    /// <summary>Validates the entire request before compiling and publishes only complete vertex/pixel packages.</summary>
    public PlatformShaderArtifactCookResult Cook(PlatformShaderArtifactCookRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        if (request.PlatformId != "xbox" || request.GraphicsProfileId != "xbox-shader")
            throw new InvalidOperationException("Xbox shader cooking requires the explicit xbox-shader profile.");
        Dictionary<string, PlatformShaderArtifactCookSource> sources = request.ShaderSources.ToDictionary(source => source.ShaderAssetId, StringComparer.Ordinal);
        Dictionary<string, PlatformShaderDependency> dependencies = new(StringComparer.Ordinal);
        HashSet<string> outputNames = new(StringComparer.OrdinalIgnoreCase);
        foreach (PlatformShaderDependency dependency in request.ShaderDependencies) {
            if (!Regex.IsMatch(dependency.ShaderAssetId, @"^[A-Za-z_][A-Za-z_0-9.-]*$", RegexOptions.CultureInvariant) ||
                !dependency.HasProgramPair || dependency.VertexProgramName != dependency.ShaderAssetId + ".vs" ||
                dependency.PixelProgramName != dependency.ShaderAssetId + ".ps")
                throw new InvalidOperationException("XBOX_SHADER_SELECTION: custom packages require explicit .vs/.ps programs and the default VS/PS entry convention.");
            _ = ResolveVariant(dependency);
            if (!sources.TryGetValue(dependency.ShaderAssetId, out PlatformShaderArtifactCookSource source) ||
                !source.HasSourcePath || string.IsNullOrWhiteSpace(source.SourceText))
                throw new InvalidOperationException("XBOX_SHADER_SOURCE: missing authored source snapshot or path.");
            if (dependencies.TryAdd(dependency.ShaderAssetId, dependency) && !outputNames.Add(RelativePath(dependency.ShaderAssetId)))
                throw new InvalidOperationException("XBOX_SHADER_PATH: case-insensitive package path collision.");
        }
        Dictionary<string, byte[]> prepared = new(StringComparer.Ordinal);
        try {
            foreach (PlatformShaderDependency dependency in dependencies.Values) {
                PlatformShaderArtifactCookSource source = sources[dependency.ShaderAssetId];
                ShaderFilesystemIncludeResolver resolver = new(Path.GetDirectoryName(source.SourcePath));
                ShaderCompileService service = new(resolver, new ShaderMemoryCompileCache(), new ShaderSourceHasher());
                service.RegisterBackend(new XboxShaderBackend());
                StandardShaderVariant variant = ResolveVariant(dependency);
                ShaderCompileResult vertex = Compile(service, source, dependency.VertexProgramName, ShaderStage.Vertex, variant);
                ShaderCompileResult pixel = Compile(service, source, dependency.PixelProgramName, ShaderStage.Pixel, variant);
                XboxShaderPairValidator.Validate(vertex.ProgramDefinition, pixel.ProgramDefinition);
                XboxShaderInterfaceBinding.ValidateArtifacts(vertex.Binary.Bytecode, pixel.Binary.Bytecode);
                ShaderModuleDefinition module = new(dependency.ShaderAssetId,
                    [vertex.ProgramDefinition, pixel.ProgramDefinition],
                    [new ShaderProgramBinary(dependency.VertexProgramName, ShaderStage.Vertex, "xbox", variant.Name, vertex.Binary.Bytecode),
                     new ShaderProgramBinary(dependency.PixelProgramName, ShaderStage.Pixel, "xbox", variant.Name, pixel.Binary.Bytecode)]);
                prepared.Add(RelativePath(dependency.ShaderAssetId), SerializePackage(module, request.NumericPolicy));
            }
            foreach (var package in prepared) {
                string destination = Path.Combine(request.CookRootPath, package.Key);
                Directory.CreateDirectory(Path.GetDirectoryName(destination));
                string temporary = destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
                try {
                    File.WriteAllBytes(temporary, package.Value);
                    File.Move(temporary, destination, overwrite: true);
                } finally { if (File.Exists(temporary)) File.Delete(temporary); }
            }
        } catch {
            // Requested outputs cannot survive a failed compilation as apparently reusable packages.
            foreach (PlatformShaderDependency dependency in dependencies.Values) {
                string destination = Path.Combine(request.CookRootPath, RelativePath(dependency.ShaderAssetId));
                if (File.Exists(destination)) File.Delete(destination);
            }
            throw;
        }
        return new PlatformShaderArtifactCookResult(dependencies.Values.Select(dependency =>
            new PlatformCookedArtifactDeclaration(RelativePath(dependency.ShaderAssetId), dependency.ShaderAssetId, "shader", "xbox")).ToArray());
    }

    /// <summary>Serializes the compiled module's runtime envelope under the consuming build's captured numeric policy.</summary>
    static byte[] SerializePackage(ShaderModuleDefinition module, NumericPrecisionPolicy numericPolicy) {
        ArgumentNullException.ThrowIfNull(numericPolicy);
        return helengine.files.AssetSerializer.SerializeToBytes(ShaderAsset.FromDefinition(module, ShaderCompileTarget.Xbox), numericPolicy);
    }

    /// <summary>Builds the canonical package filename expected by ShaderRuntimeMaterialLoader.</summary>
    private static string RelativePath(string assetId) => "shaders/" + assetId.ToLowerInvariant() + ".xbox.hasset";

    /// <summary>Compiles the supplied snapshot with active include tracking and the explicit Xbox API define.</summary>
    private static ShaderCompileResult Compile(ShaderCompileService service, PlatformShaderArtifactCookSource source, string program, ShaderStage stage, StandardShaderVariant variant) {
        ShaderModel model = new(4, 0);
        ShaderDefine[] defines = variant.Defines.Select(value => {
            string[] parts = value.Split('=', 2);
            return new ShaderDefine(parts[0], parts.Length == 2 ? parts[1] : "1");
        }).ToArray();
        ShaderCompileRequest request = new(new ShaderSourceInfo(source.SourcePath, source.SourceText), program,
            stage == ShaderStage.Vertex ? variant.VertexEntryPoint : variant.PixelEntryPoint, stage, ShaderCompileTarget.Xbox, model, variant.Name,
            ShaderPlatformDefines.BuildDefines(ShaderCompileTarget.Xbox, model, defines),
            new ShaderCompileOptions(ShaderBindingPolicies.Default, true, false, true));
        ShaderCompileResult result = service.Compile(request);
        if (!result.Success || result.Binary.Target != ShaderCompileTarget.Xbox || result.Binary.Stage != stage)
            throw new InvalidOperationException("XBOX_SHADER_COMPILE: compiler did not produce the requested Xbox stage.");
        return result;
    }

    /// <summary>Uses the shared batch catalog without admitting unsupported NV2A variants.</summary>
    private static StandardShaderVariant ResolveVariant(PlatformShaderDependency dependency) {
        if (dependency.ShaderAssetId == "SpriteBatchShader") {
            if (dependency.VariantName != "Textured")
                throw new InvalidOperationException("XBOX_BATCH_VARIANT: only Textured is supported; RoundedShape is not implemented on NV2A.");
            return SpriteBatchShaderVariants.All.Single(variant => variant.Name == "Textured");
        }
        if (dependency.VariantName != "default")
            throw new InvalidOperationException("XBOX_SHADER_SELECTION: custom packages require the default variant.");
        return new StandardShaderVariant("default", "VS", "PS", []);
    }
}
