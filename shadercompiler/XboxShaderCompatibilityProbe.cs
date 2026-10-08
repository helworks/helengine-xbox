using System.Runtime.InteropServices;
using System.Text;
using Silk.NET.Shaderc;
using Silk.NET.SPIRV;
using Silk.NET.SPIRV.Cross;

namespace helengine.xbox.shaderprobe;

/// <summary>Translates HLSL through the verified native frontend and typed Cg entry adapter.</summary>
public sealed unsafe class XboxShaderCompatibilityProbe {
    /// <summary>Translates a source request with active includes and variant macros.</summary>
    public XboxShaderProbeOutput Translate(XboxShaderSourceRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        using XboxShaderIncludeSession includes = new(request);
        byte[] spirv = CompileSpirv(request, includes);
        XboxShaderCapabilities.Validate(spirv, request.VertexStage);
        XboxShaderProbeOutput output = Emit(spirv, request.EntryPoint, request.VertexStage);
        output.RecordDependencies(request, includes.Sources);
        return output;
    }
    /// <summary>Translates a self-contained fixture and preserves the compiler's original output.</summary>
    public XboxShaderProbeOutput Translate(string sourcePath, string entryPoint, bool vertexStage) {
        string path = Path.GetFullPath(sourcePath);
        return Translate(new XboxShaderSourceRequest(new XboxShaderSource(path, File.ReadAllText(path)),
            entryPoint, vertexStage, new Dictionary<string, string>(), ResolveFileInclude));
    }

    /// <summary>Resolves standalone file callers relative to the requesting file; engine callers supply their own resolver.</summary>
    static XboxShaderSource ResolveFileInclude(string requesting, string include) {
        string path = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(requesting), include));
        return new XboxShaderSource(path, File.ReadAllText(path));
    }

    /// <summary>Compiles the actual entry point with source names and HLSL semantics retained.</summary>
    static byte[] CompileSpirv(XboxShaderSourceRequest request, XboxShaderIncludeSession includes) {
        string source = request.Source.Text;
        using Shaderc api = new(Shaderc.CreateDefaultContext([XboxShaderNativeLibraries.Shaderc]));
        Silk.NET.Shaderc.Compiler* compiler = api.CompilerInitialize();
        CompileOptions* options = api.CompileOptionsInitialize();
        if (compiler == null || options == null) {
            if (compiler != null) api.CompilerRelease(compiler);
            if (options != null) api.CompileOptionsRelease(options);
            throw new InvalidOperationException("Cannot initialize shaderc.");
        }
        try {
            api.CompileOptionsSetSourceLanguage(options, Silk.NET.Shaderc.SourceLanguage.Hlsl);
            api.CompileOptionsSetTargetEnv(options, TargetEnv.Vulkan, (uint)EnvVersion.Vulkan10);
            api.CompileOptionsSetHlslIoMapping(options, true);
            api.CompileOptionsSetHlslFunctionality1(options, true);
            api.CompileOptionsSetAutoMapLocations(options, true);
            api.CompileOptionsSetAutoBindUniforms(options, true);
            api.CompileOptionsSetPreserveBindings(options, true);
            api.CompileOptionsSetGenerateDebugInfo(options);
            api.CompileOptionsSetOptimizationLevel(options, request.Optimize ? OptimizationLevel.Performance : OptimizationLevel.Zero);
            if (request.WarningsAsErrors) api.CompileOptionsSetWarningsAsErrors(options);
            api.CompileOptionsSetIncludeCallbacks(options, includes.ResolveCallback, includes.ReleaseCallback, null);
            foreach (var define in request.Defines) {
                string value = define.Value ?? string.Empty;
                api.CompileOptionsAddMacroDefinition(options, define.Key, (nuint)Encoding.UTF8.GetByteCount(define.Key), value, (nuint)Encoding.UTF8.GetByteCount(value));
            }
            CompilationResult* result = api.CompileIntoSpv(compiler, source,
                (nuint)Encoding.UTF8.GetByteCount(source), request.VertexStage ? ShaderKind.VertexShader : ShaderKind.FragmentShader,
                request.Source.Path, request.EntryPoint, options);
            if (result == null) throw new InvalidOperationException("shaderc returned no result.");
            try {
                if (includes.Failure != null) throw new InvalidOperationException("Xbox include resolution failed: " + includes.Failure.Message, includes.Failure);
                if (api.ResultGetCompilationStatus(result) != CompilationStatus.Success)
                    throw new InvalidOperationException(api.ResultGetErrorMessageS(result));
                int length = checked((int)api.ResultGetLength(result));
                if (length == 0 || length % 4 != 0) throw new InvalidOperationException("Invalid SPIR-V byte length.");
                byte[] bytes = new byte[length];
                Marshal.Copy((IntPtr)api.ResultGetBytes(result), bytes, 0, length);
                return bytes;
            } finally {
                api.ResultRelease(result);
            }
        } finally {
            api.CompileOptionsRelease(options);
            api.CompilerRelease(compiler);
        }
    }

    /// <summary>Emits legacy HLSL through native options; never rewrites generated text.</summary>
    static XboxShaderProbeOutput Emit(byte[] spirv, string entryPoint, bool vertexStage) {
        using Cross api = new(Cross.CreateDefaultContext([XboxShaderNativeLibraries.SpirvCross]));
        Context* context = null;
        Check(api, context, api.ContextCreate(&context));
        try {
            ParsedIr* parsed = null;
            fixed (byte* data = spirv) {
                Check(api, context, api.ContextParseSpirv(context, (uint*)data, (nuint)(spirv.Length / 4), &parsed));
            }
            Silk.NET.SPIRV.Cross.Compiler* compiler = null;
            Check(api, context, api.ContextCreateCompiler(context, Backend.Hlsl, parsed, CaptureMode.TakeOwnership, &compiler));
            CompilerOptions* options = null;
            Check(api, context, api.CompilerCreateCompilerOptions(compiler, &options));
            Check(api, context, api.CompilerOptionsSetUint(options, CompilerOption.HlslShaderModel, 30));
            Check(api, context, api.CompilerInstallCompilerOptions(compiler, options));
            ExecutionModel model = vertexStage ? ExecutionModel.Vertex : ExecutionModel.Fragment;
            Check(api, context, api.CompilerSetEntryPoint(compiler, entryPoint, model));
            XboxShaderReflectionItem[] metadata = XboxShaderReflection.Read(api, compiler);
            string report = Reflect(api, context, compiler, vertexStage);
            Check(api, context, api.CompilerBuildCombinedImageSamplers(compiler));
            var combinedSamplers = XboxCombinedSampler.Read(api, compiler);
            byte* source = null;
            Check(api, context, api.CompilerCompile(compiler, &source));
            string emitted = Marshal.PtrToStringUTF8((IntPtr)source);
            if (string.IsNullOrWhiteSpace(emitted)) throw new InvalidOperationException("Emitter returned no source.");
            string emittedEntry = api.CompilerGetCleansedEntryPointNameS(compiler, entryPoint, model);
            string adapter = XboxShaderEntryAdapter.Create(api, compiler, emittedEntry, vertexStage);
            return new XboxShaderProbeOutput(spirv, emitted, "XboxMain", report, adapter) { Metadata = metadata, CombinedSamplers = combinedSamplers };
        } finally {
            if (context != null) api.ContextDestroy(context);
        }
    }

    /// <summary>Records reflected locations/resources and maps authored VS attributes via the compiler API.</summary>
    static string Reflect(Cross api, Context* context, Silk.NET.SPIRV.Cross.Compiler* compiler, bool vertexStage) {
        Resources* resources = null;
        Check(api, context, api.CompilerCreateShaderResources(compiler, &resources));
        StringBuilder report = new();
        foreach (ResourceType kind in new[] { ResourceType.StageInput, ResourceType.StageOutput,
            ResourceType.UniformBuffer, ResourceType.SeparateImage, ResourceType.SeparateSamplers }) {
            ReflectedResource* entries = null;
            nuint count = 0;
            Check(api, context, api.ResourcesGetResourceListForType(resources, kind, &entries, &count));
            for (nuint i = 0; i < count; ++i) {
                uint id = entries[i].Id;
                string semantic = api.CompilerGetDecorationStringS(compiler, id, (Decoration)5635);
                uint location = api.CompilerGetDecoration(compiler, id, Decoration.Location);
                report.AppendLine($"{kind}: {api.CompilerGetNameS(compiler, id)} semantic={semantic} location={location} binding={api.CompilerGetDecoration(compiler, id, Decoration.Binding)}");
                if (kind == ResourceType.UniformBuffer) {
                    Check(api, context, api.CompilerFlattenBufferBlock(compiler, id));
                }
                if (vertexStage && kind == ResourceType.StageInput && !string.IsNullOrEmpty(semantic)) {
                    IntPtr text = Marshal.StringToCoTaskMemUTF8(semantic);
                    try {
                        HlslVertexAttributeRemap remap = new() { Location = location, Semantic = (byte*)text };
                        Check(api, context, api.CompilerHlslAddVertexAttributeRemap(compiler, &remap, 1));
                    } finally {
                        Marshal.FreeCoTaskMem(text);
                    }
                }
            }
        }
        return report.ToString();
    }

    /// <summary>Propagates native errors before using invalid native output.</summary>
    static void Check(Cross api, Context* context, Result result) {
        if (result != Result.Success)
            throw new InvalidOperationException(context == null ? result.ToString() : api.ContextGetLastErrorStringS(context));
    }
}
