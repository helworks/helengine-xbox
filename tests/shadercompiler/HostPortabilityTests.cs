using System.Diagnostics;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using helengine.xbox.shaderprobe;

/// <summary>Checks existing host-tool selection, external scratch ownership and shared SDK-proof integration.</summary>
internal static class HostPortabilityTests {
    /// <summary>Runs bounded host contracts without invoking Cg or starting a native build.</summary>
    public static void Run(string repository) {
        Type libraries=typeof(XboxShaderToolchainCompiler).Assembly.GetType("helengine.xbox.shaderprobe.XboxShaderNativeLibraries",true);
        Equal("runtimes/win-x64/native/shaderc_shared.dll",Relative(libraries,"windows",Architecture.X64,"shaderc_shared.dll"));
        Equal("runtimes/win-x64/native/spirv-cross.dll",Relative(libraries,"windows",Architecture.X64,"spirv-cross.dll"));
        Equal("runtimes/osx-arm64/native/libshaderc_shared.dylib",Relative(libraries,"macos",Architecture.Arm64,"shaderc_shared.dll"));
        Equal("runtimes/osx-x64/native/libspirv-cross.dylib",Relative(libraries,"macos",Architecture.X64,"spirv-cross.dll"));
        Equal("runtimes/linux-x64/native/libshaderc_shared.so",Relative(libraries,"linux",Architecture.X64,"shaderc_shared.dll"));
        Expect<PlatformNotSupportedException>(()=>Relative(libraries,"windows",Architecture.Arm64,"shaderc_shared.dll"));
        Equal(".xbox/dependencies/nxdk/tools/cg/win/cgc.exe",Cg("windows",Architecture.X64));
        Equal(".xbox/dependencies/nxdk/tools/cg/mac/cgc",Cg("macos",Architecture.Arm64));
        Equal(".xbox/dependencies/nxdk/tools/cg/mac/cgc",Cg("macos",Architecture.X64));
        Equal(".xbox/dependencies/nxdk/tools/cg/linux/cgc",Cg("linux",Architecture.X64));
        Expect<PlatformNotSupportedException>(()=>Cg("linux",Architecture.Arm64));
        Console.WriteLine("Existing Windows/Mac/Linux tools selected by host OS and architecture; unsupported Cg hosts reject.");

        string previous=Environment.GetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT");
        string scratch=OperatingSystem.IsWindows()?@"C:\temp\helengine\xbox\host-contract":OperatingSystem.IsMacOS()?"/private/tmp/helengine/xbox/host-contract":"/tmp/helengine/xbox/host-contract";
        try {
            Environment.SetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT",scratch);
            string vertex=(string)Invoke(typeof(XboxShaderToolchainCompiler),"ResolveJobRoot",repository,true);
            string pixel=(string)Invoke(typeof(XboxShaderToolchainCompiler),"ResolveJobRoot",repository,false);
            Equal(Path.Combine(scratch,"shader","vertex"),vertex);
            Equal(vertex,(string)Invoke(typeof(XboxShaderToolchainCompiler),"ResolveJobRoot",repository,true));
            Equal(Path.Combine(scratch,"shader","pixel"),pixel);
            Environment.SetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT",repository);
            Expect<InvalidOperationException>(()=>Invoke(typeof(XboxShaderToolchainCompiler),"ResolveJobRoot",repository,true));
            Environment.SetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT","relative-scratch");
            Expect<InvalidOperationException>(()=>Invoke(typeof(XboxShaderToolchainCompiler),"ResolveJobRoot",repository,true));
            Environment.SetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT",scratch);
            Directory.CreateDirectory(Path.GetDirectoryName(vertex));
            if(!OperatingSystem.IsWindows()) {
                string sourceLink=Path.Combine(Path.GetDirectoryName(vertex),"checkout-link");
                Directory.CreateSymbolicLink(sourceLink,repository);
                try {
                    Environment.SetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT",sourceLink);
                    Expect<InvalidOperationException>(()=>Invoke(typeof(XboxShaderToolchainCompiler),"ResolveJobRoot",repository,true));
                } finally {Directory.Delete(sourceLink);Environment.SetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT",scratch);}
                Console.WriteLine("Physical scratch containment rejects a source checkout reached through a symbolic link.");
            } else Console.WriteLine("Symbolic-link creation contract is Unix-only; Windows requires a privilege not requested by this test.");
            using(var held=(IDisposable)Invoke(typeof(XboxShaderToolchainCompiler),"AcquireJobLock",vertex)) {
                var start=new ProcessStartInfo(Environment.ProcessPath){UseShellExecute=false,RedirectStandardOutput=true,RedirectStandardError=true,CreateNoWindow=true};
                if(Path.GetFileNameWithoutExtension(Environment.ProcessPath).Equals("dotnet",StringComparison.OrdinalIgnoreCase)) start.ArgumentList.Add(typeof(Program).Assembly.Location);
                start.ArgumentList.Add("--compile-lock-probe");start.ArgumentList.Add(repository);
                using var child=Process.Start(start);
                Task<string> output=child.StandardOutput.ReadToEndAsync(),error=child.StandardError.ReadToEndAsync();
                if(!child.WaitForExit(10000)) {child.Kill(true);throw new Exception("Cross-process scratch-lock probe timed out.");}
                if(child.ExitCode!=0 || !output.Result.Contains("Cross-process compile rejected before tools")) throw new Exception(output.Result+error.Result);
            }
            Console.WriteLine("Fixed external scratch is reused and a second process cannot compile into an owned job.");
            VerifyIdentityAdapter(repository,vertex);
            var translated=new XboxShaderCompatibilityProbe().Translate(Path.Combine(repository,"tests/shaders/colour.hlsl"),"VSMain",true);
            if(translated.Spirv.Length==0) throw new Exception("Actual host shaderc/SPIRV-Cross frontend returned no SPIR-V.");
            Console.WriteLine("Actual host native shaderc and SPIRV-Cross libraries load and translate the existing fixture.");
        } finally {Environment.SetEnvironmentVariable("HELENGINE_BUILD_WORKSPACE_ROOT",previous);}
    }

    /// <summary>Invokes the public compile boundary in another process while the parent holds its real file lock.</summary>
    public static int ProbeCompileLock(string repository) {
        try {new XboxShaderToolchainCompiler().Compile(repository,Path.Combine(repository,"tests/shaders/colour.hlsl"),"VSMain",true);}
        catch(IOException error) when(error.GetType()==typeof(IOException)) {Console.WriteLine("Cross-process compile rejected before tools");return 0;}
        return 1;
    }

    /// <summary>Checks the existing verifier rejects tampered OCI proof before looking up Docker; path text remains literal arguments.</summary>
    static void VerifyIdentityAdapter(string repository,string job) {
        string previous=Environment.GetEnvironmentVariable("HELENGINE_XBOX_SDK_IDENTITY_ROOT");
        string proof=Path.Combine(Path.GetDirectoryName(job),"proof with space ' $ literal");
        Directory.CreateDirectory(proof);Directory.CreateDirectory(job);
        try {
            File.WriteAllText(Path.Combine(proof,"manifest.json"),"{}");
            File.WriteAllText(Path.Combine(proof,"config.json"),"{}");
            Environment.SetEnvironmentVariable("HELENGINE_XBOX_SDK_IDENTITY_ROOT",proof);
            Expect<InvalidOperationException>(()=>Invoke(typeof(XboxShaderToolchainCompiler),"ResolveSdkImage",repository,job,
                new Dictionary<string,string>{{"LocalImageId","sha256:"+new string('0',64)},{"LocalImageTag","helengine-xbox-sdk:v01"}},CancellationToken.None));
            string diagnostics=File.ReadAllText(Path.Combine(job,"sdk-identity.stderr.log"))+File.ReadAllText(Path.Combine(job,"sdk-identity.stdout.log"));
            if(!diagnostics.Contains("SDK manifest bytes do not match the locked digest.")) throw new Exception("Shared SDK verifier was not reached with the literal proof path: "+diagnostics);
            Console.WriteLine("Existing PowerShell SDK proof verifier rejects tampering; spaces, apostrophe and dollar remain literal.");
        } finally {
            Environment.SetEnvironmentVariable("HELENGINE_XBOX_SDK_IDENTITY_ROOT",previous);
            File.Delete(Path.Combine(proof,"manifest.json"));File.Delete(Path.Combine(proof,"config.json"));Directory.Delete(proof);
        }
    }

    /// <summary>Reads a private path-selection seam without exposing a new production API.</summary>
    static string Relative(Type libraries,string platform,Architecture architecture,string file)=>(string)Invoke(libraries,"RelativeLibraryPath",platform,architecture,file);
    /// <summary>Reads the host-specific existing SDK Cg path without executing a compiler.</summary>
    static string Cg(string platform,Architecture architecture)=>(string)Invoke(typeof(XboxShaderToolchainCompiler),"RelativeCgPath",platform,architecture);
    /// <summary>Calls an implementation detail while preserving its original exception type for assertions.</summary>
    static object Invoke(Type type,string name,params object[] arguments) {
        var method=type.GetMethod(name,BindingFlags.Static|BindingFlags.NonPublic)??throw new Exception("Missing host portability behavior: "+name);
        try {return method.Invoke(null,arguments);}
        catch(TargetInvocationException error) {ExceptionDispatchInfo.Capture(error.InnerException).Throw();throw;}
    }
    /// <summary>Checks a path or identity contract independently of directory separator conventions.</summary>
    static void Equal(string expected,string actual) {if(expected.Replace('\\','/')!=actual.Replace('\\','/')) throw new Exception("Expected "+expected+", got "+actual);}
    /// <summary>Requires an explicit rejection instead of silently accepting an unsupported or unsafe operation.</summary>
    static void Expect<T>(Action action) where T:Exception {try {action();}catch(T) {return;}throw new Exception("Expected "+typeof(T).Name);}
}
