using System.Security.Cryptography;
using System.Text;

namespace helengine.xbox.shaderprobe;

/// <summary>Compiles a single entry point into validated Xbox data using the pinned native tools.</summary>
public sealed class XboxShaderToolchainCompiler {
    /// <summary>Compiles one stage, retaining job evidence under the repository's ignored .xbox directory.</summary>
    public XboxShaderBuildResult Compile(string repositoryRoot, string sourcePath, string entryPoint,
                                        bool vertexStage, CancellationToken cancellationToken = default) {
        cancellationToken.ThrowIfCancellationRequested();
        string source = Path.GetFullPath(sourcePath);
        return Compile(repositoryRoot, new XboxShaderSourceRequest(new XboxShaderSource(source, File.ReadAllText(source)),
            entryPoint, vertexStage, new Dictionary<string, string>(), ResolveFileInclude), cancellationToken);
    }

    /// <summary>Compiles an in-memory engine request without changing authored paths or preprocessing its includes outside shaderc.</summary>
    public XboxShaderBuildResult Compile(string repositoryRoot, XboxShaderSourceRequest request,
                                        CancellationToken cancellationToken = default) {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        bool vertexStage = request.VertexStage;
        string entryPoint = request.EntryPoint;
        string root = Path.GetFullPath(repositoryRoot);
        string job = Path.Combine(root, ".xbox", "shader-jobs", Guid.NewGuid().ToString("N"));
        string cgc = Path.Combine(root, ".xbox/dependencies/nxdk/tools/cg/win/cgc.exe");
        if (!File.Exists(cgc)) throw new FileNotFoundException("Pinned Cg executable is missing.", cgc);
        Dictionary<string, string> toolLock = File.ReadAllLines(Path.Combine(root, "toolchain-lock.txt"))
            .Where(line => !line.StartsWith('#') && line.Contains('='))
            .Select(line => line.Split('=', 2)).ToDictionary(parts => parts[0], parts => parts[1]);
        Directory.CreateDirectory(job);
        string nxdkRoot = Path.Combine(root, ".xbox/dependencies/nxdk");
        string revision = XboxShaderToolProcess.Run("git", ["-c", "safe.directory=" + nxdkRoot, "-C", nxdkRoot, "rev-parse", "HEAD"], job, "nxdk-revision", cancellationToken).Trim();
        string imageId = XboxShaderToolProcess.Run("docker", ["image", "inspect", toolLock["LocalImageTag"], "--format", "{{.Id}}"], job, "image-id", cancellationToken).Trim();
        if (revision != toolLock["NxdkCommit"] || imageId != toolLock["LocalImageId"])
            throw new InvalidOperationException("Xbox shader toolchain does not match toolchain-lock.txt.");
        XboxShaderProbeOutput translation;
        try {
            translation = new XboxShaderCompatibilityProbe().Translate(request);
        } catch (Exception error) {
            File.WriteAllText(Path.Combine(job, "frontend.error.log"), error.ToString());
            throw;
        }
        cancellationToken.ThrowIfCancellationRequested();
        File.WriteAllText(Path.Combine(job, "shader.cg"), translation.Source);
        File.WriteAllText(Path.Combine(job, "shader.entry.cg"), "#include \"shader.cg\"\n" + translation.AdapterSource);
        File.WriteAllBytes(Path.Combine(job, "shader.spv"), translation.Spirv);
        File.WriteAllText(Path.Combine(job, "shader.reflection.txt"), translation.InterfaceReport);
        File.WriteAllText(Path.Combine(job, "dependencies.json"), System.Text.Json.JsonSerializer.Serialize(translation.DependencyHashes));
        File.WriteAllText(Path.Combine(job, "authored.hlsl"), request.Source.Text);
        string profile = vertexStage ? "vp20" : "fp20";
        XboxShaderToolProcess.Run(cgc, ["-profile", profile, "-entry", translation.EntryPoint,
            "-o", Path.Combine(job, "shader.asm"), Path.Combine(job, "shader.entry.cg")], job, "cgc", cancellationToken);
        IReadOnlyList<XboxVertexConstantBinding> vertexConstants = vertexStage
            ? XboxVertexConstantBinding.Read(File.ReadAllText(Path.Combine(job, "shader.asm")))
            : Array.Empty<XboxVertexConstantBinding>();
        File.WriteAllText(Path.Combine(job, "shader.vertex-bindings.json"), System.Text.Json.JsonSerializer.Serialize(vertexConstants));
        IReadOnlyList<XboxVertexLiteralConstant> vertexLiterals = vertexStage
            ? XboxVertexLiteralConstant.Read(File.ReadAllText(Path.Combine(job, "shader.asm")), vertexConstants)
            : Array.Empty<XboxVertexLiteralConstant>();
        File.WriteAllText(Path.Combine(job, "shader.vertex-literals.json"), System.Text.Json.JsonSerializer.Serialize(vertexLiterals));
        var interfaces = XboxShaderInterfaceBinding.Read(File.ReadAllText(Path.Combine(job, "shader.asm")), vertexStage);
        File.WriteAllText(Path.Combine(job, "shader.interfaces.json"), System.Text.Json.JsonSerializer.Serialize(interfaces));
        IReadOnlyList<XboxPixelConstantBinding> pixelConstants = vertexStage ? Array.Empty<XboxPixelConstantBinding>()
            : XboxPixelConstantBinding.Read(File.ReadAllText(Path.Combine(job, "shader.asm")));
        File.WriteAllText(Path.Combine(job, "shader.pixel-bindings.json"), System.Text.Json.JsonSerializer.Serialize(pixelConstants));
        var textureBindings = XboxTextureBinding.Read(File.ReadAllText(Path.Combine(job, "shader.asm")), translation.CombinedSamplers);
        File.WriteAllText(Path.Combine(job, "shader.texture-bindings.json"), System.Text.Json.JsonSerializer.Serialize(textureBindings));
        byte[] resources = new byte[textureBindings.Count * 160 + (vertexLiterals.Count + interfaces.Count) * 80];
        for (int index = 0; index < textureBindings.Count; ++index) {
            WriteResource(resources, index * 160, 1, textureBindings[index].TextureName, textureBindings[index].Stage);
            WriteResource(resources, index * 160 + 80, 2, textureBindings[index].SamplerName, textureBindings[index].Stage);
        }
        for (int index = 0; index < vertexLiterals.Count; index++) {
            int offset = textureBindings.Count * 160 + index * 80;
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(resources.AsSpan(offset), 3);
            for (int component = 0; component < 4; component++)
                System.Buffers.Binary.BinaryPrimitives.WriteSingleLittleEndian(resources.AsSpan(offset + 4 + component * 4), vertexLiterals[index].Values[component]);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(resources.AsSpan(offset + 68), vertexLiterals[index].Register);
        }
        for (int index = 0; index < interfaces.Count; index++) {
            int offset = textureBindings.Count * 160 + (vertexLiterals.Count + index) * 80;
            WriteResource(resources, offset, interfaces[index].Kind, interfaces[index].Semantic, interfaces[index].Register);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(resources.AsSpan(offset + 72), interfaces[index].Components);
        }
        File.WriteAllBytes(Path.Combine(job, "shader.resources"), resources);
        byte[] bindingTable = new byte[(vertexConstants.Count + pixelConstants.Count) * 72];
        for (int index = 0; index < vertexConstants.Count; ++index) {
            XboxVertexConstantBinding binding = vertexConstants[index];
            Encoding.ASCII.GetBytes(binding.Name).CopyTo(bindingTable, index * 72);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bindingTable.AsSpan(index * 72 + 64), binding.Row);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bindingTable.AsSpan(index * 72 + 68), binding.Register);
        }
        for (int index = 0; index < pixelConstants.Count; ++index) {
            XboxPixelConstantBinding binding = pixelConstants[index];
            Encoding.ASCII.GetBytes(binding.Name).CopyTo(bindingTable, index * 72);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bindingTable.AsSpan(index * 72 + 64), binding.Row);
            System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bindingTable.AsSpan(index * 72 + 68), binding.Factor);
        }
        File.WriteAllBytes(Path.Combine(job, "shader.bindings"), bindingTable);
        string translator = vertexStage ? "vp20compiler" : "fp20compiler";
        string generated = Docker(root, job, toolLock["LocalImageTag"], "translate",
            $"/opt/nxdk/tools/{translator}/{translator}", ["/job/shader.asm"], cancellationToken);
        if (string.IsNullOrWhiteSpace(generated)) throw new InvalidOperationException("NXDK emitted no shader commands.");
        File.WriteAllText(Path.Combine(job, "shader.inl"), generated);
        List<string> build = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-I/src/src", "-I/opt/nxdk/lib", "-I/job"];
        if (vertexStage) build.Add("-DXBOX_EXTRACT_VERTEX");
        build.AddRange(["/src/tools/shader-probe/native/ExtractShaderArtifact.cpp", "/src/src/platform/xbox/XboxShaderArtifact.cpp", "-o", "/job/extract"]);
        Docker(root, job, toolLock["LocalImageTag"], "extractor-build", "g++", build, cancellationToken);
        string identity = revision + imageId + HashFile(cgc) + HashFile(typeof(XboxShaderToolchainCompiler).Assembly.Location)
            + HashFile(XboxShaderNativeLibraries.Shaderc)
            + HashFile(XboxShaderNativeLibraries.SpirvCross)
            + HashFile(Path.Combine(root, "tools/shader-probe/native/ExtractShaderArtifact.cpp"))
            + HashFile(Path.Combine(root, "tools/shader-probe/native/XboxPixelShaderBaseline.hpp"))
            + HashFile(Path.Combine(root, "src/platform/xbox/XboxShaderArtifact.cpp"));
        Docker(root, job, toolLock["LocalImageTag"], "extract", "/job/extract",
            ["/job/shader.hxsb", translation.SourceIdentity, HashText(identity), HashText(entryPoint), "/job/shader.bindings", "/job/shader.resources"], cancellationToken);
        cancellationToken.ThrowIfCancellationRequested();
        return new XboxShaderBuildResult(File.ReadAllBytes(Path.Combine(job, "shader.hxsb")), job, translation) {
            VertexConstants = vertexConstants,
            VertexLiterals = vertexLiterals,
            InterfaceBindings = interfaces,
            PixelConstants = pixelConstants,
            TextureBindings = textureBindings
        };
    }

    /// <summary>Writes a bounded resource identity and physical stage into the native record.</summary>
    static void WriteResource(byte[] output, int offset, int kind, string name, int stage) {
        if (name.Length == 0 || name.Length >= 64 || !System.Text.RegularExpressions.Regex.IsMatch(name, @"^[A-Za-z_][A-Za-z_0-9]*$"))
            throw new InvalidOperationException("XBOX_TEXTURE_BINDING: resource name cannot be represented: " + name);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(output.AsSpan(offset), kind);
        Encoding.ASCII.GetBytes(name).CopyTo(output, offset + 4);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(output.AsSpan(offset + 68), stage);
    }

    /// <summary>Resolves standalone source includes relative to their authoring file.</summary>
    static XboxShaderSource ResolveFileInclude(string requesting, string include) {
        string path = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(requesting), include));
        return new XboxShaderSource(path, File.ReadAllText(path));
    }

    /// <summary>Runs a uniquely owned disposable container and removes it on timeout or cancellation.</summary>
    static string Docker(string root, string job, string image, string phase, string executable,
                         IEnumerable<string> arguments, CancellationToken cancellationToken) {
        string name = "helengine-shader-" + Guid.NewGuid().ToString("N");
        List<string> command = ["run", "--rm", "--name", name, "--mount", $"type=bind,source={root},target=/src,readonly",
            "--mount", $"type=bind,source={job},target=/job", "--entrypoint", executable, image];
        command.AddRange(arguments);
        try {
            return XboxShaderToolProcess.Run("docker", command, job, phase, cancellationToken);
        } catch (Exception error) {
            try {
                XboxShaderToolProcess.Run("docker", ["rm", "--force", name], job, phase + "-cleanup", CancellationToken.None, 10);
            } catch (Exception cleanup) {
                File.WriteAllText(Path.Combine(job, phase + "-cleanup-error.txt"), cleanup.ToString());
            }
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error).Throw();
            throw;
        }
    }

    /// <summary>Hashes exact tool or source bytes without interpreting their encoding.</summary>
    static string HashFile(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));

    /// <summary>Hashes a stable UTF-8 identity string.</summary>
    static string HashText(string value) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value)));
}
