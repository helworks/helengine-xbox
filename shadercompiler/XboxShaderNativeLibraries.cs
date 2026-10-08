namespace helengine.xbox.shaderprobe;

/// <summary>Resolves native tools beside this compiler, not beside its editor or CLI host.</summary>
internal static class XboxShaderNativeLibraries {
    /// <summary>Gets the exact shaderc binary used for compilation and tool identity.</summary>
    internal static string Shaderc => Resolve("shaderc_shared.dll");
    /// <summary>Gets the exact SPIRV-Cross binary used for translation and tool identity.</summary>
    internal static string SpirvCross => Resolve("spirv-cross.dll");

    /// <summary>Requires the Windows x64 native dependency shipped with the backend assembly.</summary>
    private static string Resolve(string filename) {
        string assembly = typeof(XboxShaderNativeLibraries).Assembly.Location;
        if (string.IsNullOrEmpty(assembly)) throw new NotSupportedException("Xbox shader backend requires a file-based plugin installation.");
        string path = Path.Combine(Path.GetDirectoryName(assembly), "runtimes", "win-x64", "native", filename);
        if (!File.Exists(path)) throw new FileNotFoundException("Xbox shader backend native dependency is missing.", path);
        return path;
    }
}
