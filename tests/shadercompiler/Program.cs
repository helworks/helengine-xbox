using System.Buffers.Binary;
using helengine.xbox.shaderprobe;

/// <summary>Runs real toolchain API contracts, with no simulated compiler acceptance.</summary>
internal static class Program {
    /// <summary>Compiles the supported entries and checks returned binary stage and deterministic payload.</summary>
    public static int Main(string[] args) {
        if (args[0] == "--shader-numeric-policy") {
            ShaderNumericPolicyTests.Run(Path.GetFullPath(args[1]));
            return 0;
        }
        if (args[0] == "--host-portability") {
            HostPortabilityTests.Run(Path.GetFullPath(args[1]));
            return 0;
        }
        if (args[0] == "--compile-lock-probe") {
            return HostPortabilityTests.ProbeCompileLock(Path.GetFullPath(args[1]));
        }
        if (args[0] == "--builder-shader-mode") {
            BuilderShaderModeTests.Run(args[1]);
            return 0;
        }
        if (args[0] == "--builder-registry") {
            var assembly = System.Reflection.Assembly.LoadFrom(Path.GetFullPath(args[1]));
            var contributor = (helengine.IShaderBackendRegistryContributor)Activator.CreateInstance(
                assembly.GetType("helengine.xbox.builder.XboxPlatformAssetBuilder", throwOnError: true));
            helengine.ShaderBackendRegistry registry = new();
            contributor.RegisterShaderBackends(registry);
            if (!registry.ContainsTarget(helengine.ShaderCompileTarget.Xbox)) throw new Exception("Platform contributor did not register Xbox.");
            Console.WriteLine("Actual platform builder contributes the Xbox shader backend.");
            return 0;
        }
        if (args[0] == "--editor-target") {
            var assembly = System.Reflection.Assembly.LoadFrom(Path.GetFullPath(args[1]));
            var method = assembly.GetType("helengine.editor.EditorCliBuildRunner", throwOnError: true)
                .GetMethod("ResolveShaderCompileTarget", System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic);
            foreach (string platform in new[] { "xbox", "XBOX", "psvita", "windows" }) {
                string expected = platform.Equals("xbox", StringComparison.OrdinalIgnoreCase) ? "Xbox" : platform == "psvita" ? "PsVita" : "DirectX11";
                string actual = method.Invoke(null, [platform]).ToString();
                if (actual != expected) throw new Exception($"Editor target {platform}: expected {expected}, got {actual}");
            }
            Console.WriteLine("Compiled editor target selection passed.");
            return 0;
        }
        if (args[0] == "--child") {
            Console.WriteLine("child stdout");
            Console.Error.WriteLine("child stderr");
            if (args[1] == "sleep") Thread.Sleep(5000);
            return args[1] == "error" ? 7 : 0;
        }
        string repository = Path.GetFullPath(args[0]);
        if (args.Length > 1 && args[1] == "--sprite-batch") {
            SpriteBatchShaderTests.Run(repository);
            return 0;
        }
        if (args.Length > 1 && args[1] == "--pixel-literals") {
            PixelLiteralTests.RunNative(repository);
            return 0;
        }
        if (args.Length > 1 && args[1] == "--physical-interfaces") {
            PhysicalInterfaceTests.Run();
            PhysicalInterfaceTests.RunNative(repository);
            return 0;
        }
        if (args.Length > 1 && args[1] == "--vertex-literals") {
            VertexLiteralTests.Run();
            VertexLiteralTests.RunNative(repository);
            return 0;
        }
        if (args.Length > 1 && args[1] == "--texture-bindings") {
            TextureBindingTests.RunFrontend(repository);
            TextureBindingTests.RunNative(repository);
            return 0;
        }
        if (args.Length > 1 && args[1] == "--pixel-uniform") {
            TestPixelUniform(repository);
            return 0;
        }
        PairInterfaceTests.Run();
        TextureBindingTests.RunMetadataParser();
        PixelBindingTests.Run();
        VertexBindingTests.Run();
        VertexLiteralTests.Run();
        PhysicalInterfaceTests.Run();
        if (args.Length > 1 && args[1] == "--bindings-only") return 0;
        TestSharedTarget();
        if (helengine.ShaderTargetNames.GetTargetName(helengine.ShaderCompileTarget.Xbox) != "xbox" ||
            helengine.ShaderTargetNames.ParseTarget(" XBOX ") != helengine.ShaderCompileTarget.Xbox ||
            helengine.ShaderTargetDescriptors.Get(helengine.ShaderCompileTarget.Xbox).DefineName != "HEL_API_XBOX")
            throw new Exception("Xbox package identity or API preprocessor selection is missing.");
        if (args.Length > 1 && args[1] == "--target-only") return 0;
        TestMetadata(repository);
        TextureBindingTests.RunFrontend(repository);
        TestCapabilities(repository);
        TestIncludes();
        TestToolProcess(repository);
        if (args.Length > 1 && args[1] == "--abi-only") {
            var result = new XboxShaderToolchainCompiler().Compile(repository, Path.Combine(repository, "tests/shaders/textured.hlsl"), "VSMain", true);
            TestVertexAbi(result.JobRoot);
            return 0;
        }
        if (args.Length > 1 && args[1] == "--frontend-only") return 0;
        TestBackend(repository);
        if (args.Length > 1 && args[1] == "--backend-only") return 0;
        SpriteBatchShaderTests.Run(repository);
        XboxShaderToolchainCompiler compiler = new();
        PhysicalInterfaceTests.RunNative(repository);
        VertexLiteralTests.RunNative(repository);
        PixelLiteralTests.RunNative(repository);
        byte[] firstVertex = null;
        foreach (string fixture in new[] { "colour", "textured" }) {
            foreach (bool vertex in new[] { true, false }) {
                string source = Path.Combine(repository, "tests", "shaders", fixture + ".hlsl");
                XboxShaderBuildResult result = compiler.Compile(repository, source, vertex ? "VSMain" : "PSMain", vertex);
                if (vertex && !result.VertexConstants.Any(binding => binding.Name == "Transform" && binding.Row == 0 && binding.Register == 0))
                    throw new Exception("Actual Cg matrix assignment was not retained.");
                if (BinaryPrimitives.ReadUInt32LittleEndian(result.Binary.AsSpan(4)) != 6 ||
                    BinaryPrimitives.ReadUInt32LittleEndian(result.Binary.AsSpan(124)) != result.TextureBindings.Count * 2 + result.VertexLiterals.Count + result.InterfaceBindings.Count ||
                    BinaryPrimitives.ReadUInt32LittleEndian(result.Binary.AsSpan(120)) != result.VertexConstants.Count)
                    throw new Exception("Published artifact omits its physical bindings.");
                if (result.Binary.Length <= 128 || BinaryPrimitives.ReadUInt32LittleEndian(result.Binary) != 0x31535848 ||
                    BinaryPrimitives.ReadUInt32LittleEndian(result.Binary.AsSpan(8)) != (vertex ? 1u : 2u))
                    throw new Exception("Wrong native artifact identity or stage.");
                Console.WriteLine($"API {fixture}/{(vertex ? "vertex" : "pixel")}: {result.Binary.Length} validated bytes at {result.JobRoot}");
                if (fixture == "colour" && vertex) firstVertex = result.Binary;
            }
        }
        byte[] repeatedVertex = compiler.Compile(repository, Path.Combine(repository, "tests/shaders/colour.hlsl"), "VSMain", true).Binary;
        if (!firstVertex.SequenceEqual(repeatedVertex)) throw new Exception("Repeated API compilation changed artifact bytes.");
        try {
            compiler.Compile(repository, Path.Combine(repository, "tests/shaders/invalid.hlsl"), "PSMain", false);
            throw new Exception("Invalid source compiled successfully.");
        } catch (InvalidOperationException error) when (error.Message.Contains("missing_symbol")) { }
        using CancellationTokenSource cancelled = new();
        cancelled.Cancel();
        try {
            compiler.Compile(repository, Path.Combine(repository, "tests/shaders/colour.hlsl"), "VSMain", true, cancelled.Token);
            throw new Exception("Cancelled compile unexpectedly succeeded.");
        } catch (OperationCanceledException) { }
        TestPixelUniform(repository);
        TextureBindingTests.RunNative(repository);
        Console.WriteLine("Single-entry compiler API contracts passed.");
        return 0;
    }
    /// <summary>Compiles the no-input pixel entry and retains the physical uniform-colour assignment.</summary>
    static void TestPixelUniform(string repository) {
        var vertex = new XboxShaderToolchainCompiler().Compile(repository, Path.Combine(repository, "tests/shaders/uniform-colour.hlsl"), "VS", true);
        Console.WriteLine("Actual vp20 uniform colour vertex published at " + vertex.JobRoot);
        var pixel = new XboxShaderToolchainCompiler().Compile(repository, Path.Combine(repository, "tests/shaders/uniform-colour.hlsl"), "PS", false);
        if (!pixel.Translation.Metadata.Any(binding => binding.Name == "BaseColorBuffer" && binding.Kind == "UniformBuffer" && binding.Size == 16) ||
            pixel.PixelConstants.Count != 1 || pixel.PixelConstants[0].Name != "BaseColorBuffer" ||
            pixel.PixelConstants[0].Row != 0 || pixel.PixelConstants[0].Factor != 0 ||
            BinaryPrimitives.ReadUInt32LittleEndian(pixel.Binary.AsSpan(4)) != 6 ||
            BinaryPrimitives.ReadUInt32LittleEndian(pixel.Binary.AsSpan(120)) != 1)
            throw new Exception("Uniform pixel shader lacks reflected physical constant bindings.");
        Console.WriteLine("Actual fp20 uniform colour bindings published at " + pixel.JobRoot);
    }
    /// <summary>Checks non-symmetric matrix, viewport, preserved clip W, colour and normalized-to-texel UV using compiled instructions.</summary>
    static void TestVertexAbi(string job) {
        Vp20ReferenceInterpreter machine = new();
        for (int register = 0; register < 96; ++register) machine.Set($"c[{register}]", 0, 0, 0, 0);
        machine.Set("c[0]", 1, 2, 3, 4);
        machine.Set("c[1]", 5, 6, 7, 8);
        machine.Set("c[2]", 9, 10, 11, 12);
        machine.Set("c[3]", 13, 14, 15, 16);
        machine.Set("c[93]", 256, 128, 0, 0);
        machine.Set("c[94]", 320, -240, 16777215, 0);
        machine.Set("c[95]", 320.53125f, 240.53125f, 0, 0);
        machine.Set("v[0]", 2, 3, 5, 1);
        machine.Set("v[3]", .25f, .5f, .75f, .8f);
        machine.Set("v[8]", .25f, .75f, 0, 1);
        machine.Execute(File.ReadAllText(Path.Combine(job, "shader.asm")));
        float[] position = machine.Get("o[HPOS]");
        if (Math.Abs(position[0] - (75f / 108 * 320 + 320.53125f)) > .001f ||
            Math.Abs(position[1] - (86f / 108 * -240 + 240.53125f)) > .001f ||
            Math.Abs(position[2] - 97f / 108 * 16777215) > 2 || position[3] != 108)
            throw new Exception("Compiled viewport/matrix ABI is incorrect: " + string.Join(",", position));
        float[] uv = machine.Get("o[TEX0]");
        if (uv[0] != 64 || uv[1] != 96) throw new Exception("Compiled shader did not preserve normalized sampling semantics for linear textures.");
        if (!machine.Get("o[COL0]").SequenceEqual(new[] { .25f, .5f, .75f, .8f })) throw new Exception("Vertex colour/alpha changed.");
        Console.WriteLine("Compiled vp20 matrix/viewport/W/colour/UV numerical ABI passed.");
    }
    /// <summary>Requires a named pre-Cg diagnostic for an unsupported fragment operation.</summary>
    static void TestCapabilities(string repository) {
        try {
            new XboxShaderCompatibilityProbe().Translate(Path.Combine(repository, "tests/shaders/unsupported-derivative.hlsl"), "PSMain", false);
            throw new Exception("Unsupported derivative passed Xbox frontend validation.");
        } catch (NotSupportedException error) when (error.Message.Contains("XBOX_SHADER_OPERATION")) { }
    }
    /// <summary>Exercises engine metadata and native binary results through the public backend contract.</summary>
    static void TestBackend(string repository) {
        var backend = new helengine.xbox.builder.XboxShaderBackend(repository);
        helengine.ShaderBackendRegistry registry = new();
        registry.Register(backend);
        var service = registry.CreateCompileService(new RejectIncludeResolver(), new RejectShaderCompileCache(), new helengine.ShaderSourceHasher());
        if (!registry.ContainsTarget(helengine.ShaderCompileTarget.Xbox) || backend.UseSharedCache)
            throw new Exception("Xbox registry or dependency-aware cache policy is incorrect.");
        List<helengine.ShaderProgramDefinition> definitions = [];
        foreach (bool vertex in new[] { true, false }) {
            string path = Path.Combine(repository, "tests/shaders/textured.hlsl");
            helengine.ShaderCompileRequest request = new(new helengine.ShaderSourceInfo(path, File.ReadAllText(path)),
                "Textured", vertex ? "VSMain" : "PSMain", vertex ? helengine.ShaderStage.Vertex : helengine.ShaderStage.Pixel,
                helengine.ShaderCompileTarget.Xbox, new helengine.ShaderModel(5, 0), "default", [],
                new helengine.ShaderCompileOptions(new helengine.ShaderBindingPolicy(0, 0, 0, 0, 0), false, false, true));
            var result = service.Compile(request);
            if (!result.Success || result.Binary.Target != helengine.ShaderCompileTarget.Xbox || result.Binary.Bytecode.Length <= 128)
                throw new Exception("Engine backend did not return an Xbox native artifact.");
            if (vertex && !result.ProgramDefinition.Bindings.Any(binding => binding.Name == "Transform" && binding.Size == 64))
                throw new Exception("Engine matrix binding is missing.");
            if (!vertex && !result.ProgramDefinition.Bindings.Any(binding => binding.Name == "Albedo" && binding.Type == helengine.ShaderResourceType.Texture2D))
                throw new Exception("Engine texture binding is missing.");
            definitions.Add(result.ProgramDefinition);
        }
        helengine.xbox.builder.XboxShaderPairValidator.Validate(definitions[0], definitions[1]);
        foreach (var definition in definitions) definition.Dispose();
        Console.WriteLine("Engine backend binary and reflected binding contracts passed.");
    }
    /// <summary>Verifies typed reflection rather than relying on diagnostic text for the engine binding contract.</summary>
    static void TestMetadata(string repository) {
        XboxShaderCompatibilityProbe compiler = new();
        string path = Path.Combine(repository, "tests/shaders/textured.hlsl");
        var vertex = compiler.Translate(path, "VSMain", true);
        var buffer = vertex.Metadata.Single(item => item.Kind == "UniformBuffer");
        if (buffer.Name != "Transform" || buffer.Size != 64 || buffer.Binding != 0 || buffer.Members.Length != 1 ||
            buffer.Members[0].Name != "WorldViewProjection" || buffer.Members[0].Format != "float4x4" || buffer.Members[0].Offset != 0)
            throw new Exception("Wrong reflected matrix layout.");
        if (!vertex.Metadata.Any(item => item.Kind == "StageInput" && item.Semantic == "COLOR0" && item.Format == "float4"))
            throw new Exception("Missing authored colour interface.");
        var pixel = compiler.Translate(path, "PSMain", false);
        if (pixel.Metadata.Any(item => item.Kind == "UniformBuffer") ||
            !pixel.Metadata.Any(item => item.Kind == "SeparateImage" && item.Name == "Albedo" && item.Binding == 0) ||
            !pixel.Metadata.Any(item => item.Kind == "SeparateSamplers" && item.Name == "AlbedoSampler"))
            throw new Exception("Wrong active pixel resources.");
        Console.WriteLine("Typed active matrix/interface/sampler metadata passed.");
    }
    /// <summary>Requires a distinct appended Xbox target without changing serialized values of older platforms.</summary>
    static void TestSharedTarget() {
        string[] previous = ["DirectX9", "DirectX11", "DirectX12", "Vulkan", "Metal", "PsVita", "WiiU", "Ps3", "Xbox360"];
        for (int i = 0; i < previous.Length; ++i)
            if (Enum.GetName((helengine.ShaderCompileTarget)i) != previous[i]) throw new Exception("Existing serialized shader target changed.");
        if (Enum.GetName((helengine.ShaderCompileTarget)9) != "Xbox") throw new Exception("Original Xbox shader target is missing.");
    }
    /// <summary>Checks preprocessor-selected dependencies without opening inactive includes.</summary>
    static void TestIncludes() {
        int resolutions = 0;
        bool changeInclude = false;
        XboxShaderSourceRequest request = new(new XboxShaderSource("virtual/main.hlsl",
            "#if ENABLE_COLOR\n#include \"active.hlsli\"\n#else\n#include \"absent.hlsli\"\n#endif\nfloat4 PSMain(float4 color : COLOR0) : SV_Target { return Shade(color); }"),
            "PSMain", false, new Dictionary<string, string> { ["ENABLE_COLOR"] = "1" },
            (requesting, path) => {
                if (requesting != "virtual/main.hlsl" || path != "active.hlsli") throw new Exception("Wrong active include.");
                ++resolutions;
                return new XboxShaderSource("virtual/active.hlsli", changeInclude ? "float4 Shade(float4 c) { return c * 0.5; }" : "float4 Shade(float4 c) { return c; }");
            });
        XboxShaderProbeOutput output = new XboxShaderCompatibilityProbe().Translate(request);
        if (resolutions != 1 || output.Spirv.Length == 0 || output.DependencyHashes.Count != 2 || !output.DependencyHashes.ContainsKey("virtual/active.hlsli"))
            throw new Exception("Active dependency closure is incorrect.");
        XboxShaderCompatibilityProbe compiler = new();
        if (compiler.Translate(request).SourceIdentity != output.SourceIdentity) throw new Exception("Unstable dependency identity.");
        changeInclude = true;
        if (compiler.Translate(request).SourceIdentity == output.SourceIdentity) throw new Exception("Changed include did not invalidate identity.");
        XboxShaderSourceRequest missing = new(request.Source, request.EntryPoint, false, request.Defines,
            (requesting, path) => throw new IOException("include unavailable"));
        try {
            compiler.Translate(missing);
            throw new Exception("Missing include succeeded.");
        } catch (InvalidOperationException error) when (error.Message.Contains("include unavailable")) { }
        Console.WriteLine("Active includes and defines passed.");
    }
    /// <summary>Checks real process exits, diagnostics, timeout and cancellation using this test executable as a child.</summary>
    static void TestToolProcess(string repository) {
        string job = Path.Combine(repository, ".xbox", "process-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(job);
        string assembly = typeof(Program).Assembly.Location;
        string output = XboxShaderToolProcess.Run("dotnet", [assembly, "--child", "ok"], job, "success", CancellationToken.None);
        if (!output.Contains("child stdout") || !File.ReadAllText(Path.Combine(job, "success.stderr.log")).Contains("child stderr"))
            throw new Exception("Process diagnostics were lost.");
        try {
            XboxShaderToolProcess.Run("dotnet", [assembly, "--child", "error"], job, "failure", CancellationToken.None);
            throw new Exception("Failed native process succeeded.");
        } catch (InvalidOperationException error) when (error.Message.Contains("exited 7") && error.Message.Contains("child stderr")) { }
        try {
            XboxShaderToolProcess.Run("dotnet", [assembly, "--child", "sleep"], job, "timeout", CancellationToken.None, 1);
            throw new Exception("Native timeout was ignored.");
        } catch (TimeoutException) { }
        using CancellationTokenSource cancelled = new(TimeSpan.FromMilliseconds(300));
        try {
            XboxShaderToolProcess.Run("dotnet", [assembly, "--child", "sleep"], job, "cancelled", cancelled.Token);
            throw new Exception("Native cancellation was ignored.");
        } catch (OperationCanceledException) { }
        Console.WriteLine("Real process exit/diagnostics/timeout/cancellation contracts passed.");
    }
}
