using System.Security.Cryptography;
using System.Text;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace helengine.xbox.shaderprobe;

/// <summary>Compiles a single entry point into validated Xbox data using the pinned native tools.</summary>
public sealed class XboxShaderToolchainCompiler {
    /// <summary>Compiles one stage, retaining bounded evidence in the configured external build workspace.</summary>
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
        string job = ResolveJobRoot(root, vertexStage);
        using FileStream jobLock = AcquireJobLock(job);
        Directory.CreateDirectory(job);
        // A failed tool may leave a partial result. Never reuse a previous compile's executable output.
        foreach (string filename in new[] { "shader.asm", "shader.inl", "shader.hxsb", "extract" })
            File.Delete(Path.Combine(job, filename));
        string platform = OperatingSystem.IsWindows() ? "windows" : OperatingSystem.IsMacOS() ? "macos" : OperatingSystem.IsLinux() ? "linux" : "unsupported";
        string cgc = Path.Combine(root, RelativeCgPath(platform, RuntimeInformation.ProcessArchitecture));
        if (!File.Exists(cgc)) throw new FileNotFoundException("Pinned Cg executable is missing.", cgc);
        Dictionary<string, string> toolLock = File.ReadAllLines(Path.Combine(root, "toolchain-lock.txt"))
            .Where(line => !line.StartsWith('#') && line.Contains('='))
            .Select(line => line.Split('=', 2)).ToDictionary(parts => parts[0], parts => parts[1]);
        string nxdkRoot = Path.Combine(root, ".xbox/dependencies/nxdk");
        string revision = XboxShaderToolProcess.Run("git", ["-c", "safe.directory=" + nxdkRoot, "-C", nxdkRoot, "rev-parse", "HEAD"], job, "nxdk-revision", cancellationToken).Trim();
        if (revision != toolLock["NxdkCommit"])
            throw new InvalidOperationException("Xbox shader toolchain does not match toolchain-lock.txt.");
        string imageId = ResolveSdkImage(root, job, toolLock, cancellationToken);
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
        string generated = Docker(root, job, imageId, "translate",
            $"/opt/nxdk/tools/{translator}/{translator}", ["/job/shader.asm"], cancellationToken);
        if (string.IsNullOrWhiteSpace(generated)) throw new InvalidOperationException("NXDK emitted no shader commands.");
        File.WriteAllText(Path.Combine(job, "shader.inl"), generated);
        List<string> build = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-I/src/src", "-I/opt/nxdk/lib", "-I/job"];
        if (vertexStage) build.Add("-DXBOX_EXTRACT_VERTEX");
        build.AddRange(["/src/tools/shader-probe/native/ExtractShaderArtifact.cpp", "/src/src/platform/xbox/XboxShaderArtifact.cpp", "-o", "/job/extract"]);
        Docker(root, job, imageId, "extractor-build", "g++", build, cancellationToken);
        string identity = revision + toolLock["LocalImageId"] + HashFile(cgc) + HashFile(typeof(XboxShaderToolchainCompiler).Assembly.Location)
            + HashFile(XboxShaderNativeLibraries.Shaderc)
            + HashFile(XboxShaderNativeLibraries.SpirvCross)
            + HashFile(Path.Combine(root, "tools/shader-probe/native/ExtractShaderArtifact.cpp"))
            + HashFile(Path.Combine(root, "tools/shader-probe/native/XboxPixelShaderBaseline.hpp"))
            + HashFile(Path.Combine(root, "src/platform/xbox/XboxShaderArtifact.cpp"));
        Docker(root, job, imageId, "extract", "/job/extract",
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

    /// <summary>Selects an existing SDK compiler; the Mac binary includes x64 and uses installed Rosetta on arm64.</summary>
    static string RelativeCgPath(string platform, Architecture architecture) {
        string host;
        if (platform == "windows" && architecture == Architecture.X64) host = "win/cgc.exe";
        else if (platform == "macos" && (architecture == Architecture.X64 || architecture == Architecture.Arm64)) host = "mac/cgc";
        else if (platform == "linux" && architecture == Architecture.X64) host = "linux/cgc";
        else throw new PlatformNotSupportedException($"Pinned Xbox Cg does not support {platform}/{architecture}.");
        return Path.Combine(".xbox", "dependencies", "nxdk", "tools", "cg", host.Replace('/', Path.DirectorySeparatorChar));
    }

    /// <summary>Reuses fixed external stage directories in the caller workspace or standard host temporary root.</summary>
    static string ResolveJobRoot(string repository, bool vertex) {
        string workspace = Environment.GetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT");
        if (string.IsNullOrWhiteSpace(workspace)) workspace = Path.Combine(Path.GetTempPath(), "helengine", "xbox");
        if (!Path.IsPathFullyQualified(workspace)) throw new InvalidOperationException("Xbox shader scratch requires an absolute external path.");
        string job = PhysicalDirectoryPath(Path.Combine(workspace, "shader", vertex ? "vertex" : "pixel"));
        if (IsWithin(job, PhysicalDirectoryPath(repository))) throw new InvalidOperationException("Xbox shader scratch must be outside the source checkout.");
        return job;
    }

    /// <summary>Resolves existing ancestor links before comparing or creating an external scratch directory.</summary>
    static string PhysicalDirectoryPath(string path) {
        string full = Path.GetFullPath(path);
        string root = Path.GetPathRoot(full);
        string current = root;
        foreach (string component in full[root.Length..].Split(Path.DirectorySeparatorChar, StringSplitOptions.RemoveEmptyEntries)) {
            current = Path.Combine(current, component);
            DirectoryInfo directory = new(current);
            if (directory.LinkTarget != null) current = directory.ResolveLinkTarget(true).FullName;
        }
        return current;
    }

    /// <summary>Compares normalized directory boundaries without matching sibling prefixes.</summary>
    static bool IsWithin(string path, string root) {
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        string full = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string parent = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return full.Equals(parent, comparison) || full.StartsWith(parent + Path.DirectorySeparatorChar, comparison);
    }

    /// <summary>Rejects competing compiles across processes before writing any shared stage evidence.</summary>
    static FileStream AcquireJobLock(string job) {
        string directory = Path.GetDirectoryName(job);
        Directory.CreateDirectory(directory);
        return new FileStream(Path.Combine(directory, "compile.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }

    /// <summary>Reuses the native builder's canonical SDK proof; only the proven local ID selects execution.</summary>
    static string ResolveSdkImage(string repository, string job, Dictionary<string, string> toolLock, CancellationToken cancellationToken) {
        string helper = Path.Combine(repository, "scripts", "xbox-sdk-identity.ps1");
        if (!File.Exists(helper)) throw new FileNotFoundException("Existing Xbox SDK identity verifier is missing.", helper);
        string adapter = Path.Combine(job, "resolve-sdk.ps1");
        File.WriteAllText(adapter, """
            param([string]$HelperPath, [string]$CanonicalImageId, [string]$LockedTag)
            $ErrorActionPreference = 'Stop'
            function Invoke-CheckedNative {
                param([string]$Executable, [string[]]$Arguments)
                $previous = $ErrorActionPreference
                $ErrorActionPreference = 'Continue'
                try {
                    $result = & $Executable @Arguments 2>&1
                    $exitCode = $LASTEXITCODE
                } finally { $ErrorActionPreference = $previous }
                if ($exitCode -ne 0) { throw ($result -join [Environment]::NewLine) }
                return @($result | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] } | ForEach-Object { "$_" })
            }
            . $HelperPath
            Get-XboxSdkImageIdentity -CanonicalImageId $CanonicalImageId -LockedTag $LockedTag | ConvertTo-Json -Compress
            """);
        string output = XboxShaderToolProcess.Run(OperatingSystem.IsWindows() ? "powershell.exe" : "pwsh",
            ["-NoProfile", "-NonInteractive", "-File", adapter, helper, toolLock["LocalImageId"], toolLock["LocalImageTag"]],
            job, "sdk-identity", cancellationToken);
        using JsonDocument proof = JsonDocument.Parse(output);
        string canonical = proof.RootElement.GetProperty("CanonicalImageId").GetString();
        string local = proof.RootElement.GetProperty("LocalImageId").GetString();
        if (canonical != toolLock["LocalImageId"] || local == null ||
            !System.Text.RegularExpressions.Regex.IsMatch(local, "^sha256:[0-9a-f]{64}$"))
            throw new InvalidOperationException("Existing Xbox SDK verifier returned an invalid image identity.");
        return local;
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
