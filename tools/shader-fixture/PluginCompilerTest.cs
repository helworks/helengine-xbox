using System.Reflection;
using System.Runtime.Loader;

namespace helengine.xbox.shaderfixture;

/// <summary>Exercises the actual compiler from a plugin directory distinct from the host.</summary>
internal sealed class PluginCompilerTest : AssemblyLoadContext {
    /// <summary>Resolves the installed builder's managed and native dependencies.</summary>
    private readonly AssemblyDependencyResolver Resolver;

    /// <summary>Creates an isolated plugin load context without modifying host dependencies.</summary>
    private PluginCompilerTest(string builder) : base(isCollectible: true) {
        Resolver = new AssemblyDependencyResolver(builder);
    }

    /// <summary>Loads managed dependencies from the plugin installation.</summary>
    protected override Assembly Load(AssemblyName name) {
        string path = Resolver.ResolveAssemblyToPath(name);
        return path == null ? null : LoadFromAssemblyPath(path);
    }

    /// <summary>Loads plugin native dependencies from the actual dependency manifest.</summary>
    protected override nint LoadUnmanagedDll(string name) {
        string path = Resolver.ResolveUnmanagedDllToPath(name);
        return path == null ? 0 : LoadUnmanagedDllFromPath(path);
    }

    /// <summary>Compiles a real vertex fixture; catches host-relative DLL lookup and identity hashing bugs.</summary>
    internal static void Run(string repository, string builderPath) {
        string builder = Path.GetFullPath(builderPath);
        if (Path.GetDirectoryName(builder) == AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar))
            throw new InvalidOperationException("Test requires separate host and plugin directories.");
        if (File.Exists(Path.Combine(AppContext.BaseDirectory, "runtimes/win-x64/native/spirv-cross.dll")))
            throw new InvalidOperationException("Host must not supply the plugin's SPIRV-Cross library.");
        PluginCompilerTest context = new(builder);
        try {
            Assembly compilerAssembly = context.LoadFromAssemblyPath(Path.Combine(Path.GetDirectoryName(builder), "helengine.xbox.shadercompiler.dll"));
            Type type = compilerAssembly.GetType("helengine.xbox.shaderprobe.XboxShaderToolchainCompiler", throwOnError: true);
            MethodInfo compile = type.GetMethod("Compile", [typeof(string), typeof(string), typeof(string), typeof(bool), typeof(CancellationToken)]);
            object result = compile.Invoke(Activator.CreateInstance(type), [Path.GetFullPath(repository),
                Path.Combine(Path.GetFullPath(repository), "tests/shaders/colour.hlsl"), "VSMain", true, CancellationToken.None]);
            byte[] binary = (byte[])result.GetType().GetProperty("Binary").GetValue(result);
            if (binary.Length <= 128 || BitConverter.ToUInt32(binary, 0) != 0x31535848 || BitConverter.ToUInt32(binary, 8) != 1)
                throw new InvalidOperationException("Plugin did not produce a validated vertex artifact.");
            Console.WriteLine("Independent host loaded plugin compiler and produced a real Xbox vertex artifact.");
        } finally { context.Unload(); }
    }
}
