using System.Runtime.InteropServices;

namespace helengine.xbox.shaderprobe;

/// <summary>Resolves native tools beside this compiler, not beside its editor or CLI host.</summary>
internal static class XboxShaderNativeLibraries {
    /// <summary>
    /// Retains one Mac loader reference per loaded backend assembly. The supported editor/CLI loader uses the
    /// non-collectible default context. SPIRV-Cross registers exit destructors during translation, so unloading
    /// its image before process exit leaves those callbacks pointing at unmapped code. This reference is never
    /// freed; each compilation still destroys its native context and disposes its own Silk loader reference.
    /// </summary>
    static readonly Lazy<IntPtr> MacSpirvCrossReference = new(() => NativeLibrary.Load(Resolve("spirv-cross.dll")),
        LazyThreadSafetyMode.ExecutionAndPublication);

    /// <summary>Gets the exact shaderc binary used for compilation and tool identity.</summary>
    internal static string Shaderc => Resolve("shaderc_shared.dll");
    /// <summary>Gets the exact SPIRV-Cross binary used for translation and tool identity.</summary>
    internal static string SpirvCross {
        get {
            string path = Resolve("spirv-cross.dll");
            if (OperatingSystem.IsMacOS()) _ = MacSpirvCrossReference.Value;
            return path;
        }
    }

    /// <summary>Selects only native dependencies shipped for the actual host.</summary>
    static string RelativeLibraryPath(string platform, Architecture architecture, string filename) {
        string rid;
        if (platform == "windows" && architecture == Architecture.X64) rid = "win-x64";
        else if (platform == "macos" && architecture == Architecture.X64) rid = "osx-x64";
        else if (platform == "macos" && architecture == Architecture.Arm64) rid = "osx-arm64";
        else if (platform == "linux" && architecture == Architecture.X64) rid = "linux-x64";
        else throw new PlatformNotSupportedException($"Xbox shader native tools do not support {platform}/{architecture}.");
        string nativeName = platform == "windows" ? filename
            : "lib" + Path.GetFileNameWithoutExtension(filename) + (platform == "macos" ? ".dylib" : ".so");
        return Path.Combine("runtimes", rid, "native", nativeName);
    }

    /// <summary>Requires the host dependency shipped with the backend assembly.</summary>
    private static string Resolve(string filename) {
        string assembly = typeof(XboxShaderNativeLibraries).Assembly.Location;
        if (string.IsNullOrEmpty(assembly)) throw new NotSupportedException("Xbox shader backend requires a file-based plugin installation.");
        string platform = OperatingSystem.IsWindows() ? "windows" : OperatingSystem.IsMacOS() ? "macos" : OperatingSystem.IsLinux() ? "linux" : "unsupported";
        string path = Path.Combine(Path.GetDirectoryName(assembly), RelativeLibraryPath(platform, RuntimeInformation.ProcessArchitecture, filename));
        if (!File.Exists(path)) throw new FileNotFoundException("Xbox shader backend native dependency is missing.", path);
        return path;
    }
}
