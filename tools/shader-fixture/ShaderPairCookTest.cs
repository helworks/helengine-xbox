using System.Reflection;
using helengine;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.xbox.shaderfixture;

/// <summary>Rejects individually compilable stages whose authored interfaces do not connect.</summary>
internal static class ShaderPairCookTest {
    /// <summary>Requires a pair-specific diagnostic and no published package from the real cooker.</summary>
    internal static void Run(string repository, string builderPath) {
        string root = Path.GetFullPath(repository);
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_REPOSITORY_ROOT", root);
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME", "1");
        Assembly assembly = Assembly.LoadFrom(Path.GetFullPath(builderPath));
        var cooker = (IPlatformShaderArtifactBuilder)Activator.CreateInstance(assembly.GetType("helengine.xbox.builder.XboxPlatformAssetBuilder", true));
        string path = Path.Combine(root, "tests/shaders/incompatible-pair.hlsl");
        string output = Path.Combine(root, ".xbox/verification/pair-cook-" + Guid.NewGuid().ToString("N"));
        PlatformShaderDependency dependency = new("Mismatch", "Mismatch.vs", "Mismatch.ps", "default");
        PlatformShaderArtifactCookSource source = new("Mismatch", "test-snapshot", File.ReadAllText(path), path);
        try {
            cooker.CookShaderArtifacts(new PlatformShaderArtifactCookRequest(output, "xbox", "debug", "xbox-shader", [dependency], [source]));
        } catch (InvalidOperationException error) when (error.Message.Contains("XBOX_SHADER_INTERFACE")) {
            if (File.Exists(Path.Combine(output, "shaders/mismatch.xbox.hasset")))
                throw new Exception("Incompatible pair left a reusable shader package.");
            Console.WriteLine("Real cook rejected missing VS TEXCOORD0 before publishing: " + error.Message);
            return;
        }
        throw new Exception("Cook published individually valid stages with incompatible interfaces.");
    }
}
