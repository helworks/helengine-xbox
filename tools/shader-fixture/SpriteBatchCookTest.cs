using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using helengine;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.xbox.shaderfixture;

/// <summary>Checks actual shared batch cooking, variant identity and rejection before publication.</summary>
internal static class SpriteBatchCookTest {
    internal static void Run(string repository, string builderPath) {
        string root = Path.GetFullPath(repository);
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_REPOSITORY_ROOT", root);
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME", "1");
        var type = Assembly.LoadFrom(Path.GetFullPath(builderPath)).GetType("helengine.xbox.builder.XboxPlatformAssetBuilder", true);
        var cooker = (IPlatformShaderArtifactBuilder)Activator.CreateInstance(type);
        if (cooker is not IPlatformRendererShaderDependencyProvider provider || provider.RendererShaderDependencies.Count != 1 ||
            provider.RendererShaderDependencies[0].ShaderAssetId != "SpriteBatchShader" ||
            provider.RendererShaderDependencies[0].VariantName != "Textured")
            throw new Exception("Shader profile does not declare its shared Textured renderer dependency.");
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME", "0");
        if (((IPlatformRendererShaderDependencyProvider)Activator.CreateInstance(type)).RendererShaderDependencies.Count != 0)
            throw new Exception("Legacy profile unexpectedly requests batch shader cooking.");
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME", "1");
        string path = Path.GetFullPath(Path.Combine(root, "../helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl"));
        string text = File.ReadAllText(path);
        var source = new PlatformShaderArtifactCookSource("SpriteBatchShader",
            Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text))), text, path);
        var dependency = new PlatformShaderDependency("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured");
        string output = Path.Combine(root, ".xbox/verification/sprite-cook-" + Guid.NewGuid().ToString("N"));
        var result = cooker.CookShaderArtifacts(new(output, "xbox", "debug", "xbox-shader", [dependency, dependency], [source]));
        if (result.CookedArtifactDeclarations.Count != 1) throw new Exception("Duplicate batch dependencies were not collapsed.");
        string package = Path.Combine(output, "shaders/spritebatchshader.xbox.hasset");
        using (var stream = File.OpenRead(package)) {
            var shader = ShaderAssetBinarySerializer.Deserialize(stream);
            if (shader.TargetName != "xbox" || shader.Binaries.Length != 2 || shader.Programs.Length != 2 ||
                shader.Binaries.Any(binary => binary.Variant != "Textured"))
                throw new Exception("Shared batch package lost its selected variant.");
        }
        byte[] before = File.ReadAllBytes(package);
        foreach (string variant in new[] { "RoundedShape", "default", "textured", "Unknown" }) {
            var unsupported = new PlatformShaderDependency("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variant);
            bool rejected = false;
            try { cooker.CookShaderArtifacts(new(output, "xbox", "debug", "xbox-shader", [dependency, unsupported], [source])); }
            catch (InvalidOperationException) { rejected = true; }
            if (!rejected || !before.SequenceEqual(File.ReadAllBytes(package)))
                throw new Exception("Unsupported batch variant was accepted or mutated a valid package: " + variant);
        }
        Console.WriteLine("Shared Textured package, deduplication and fail-closed variant selection passed: " + output);
    }
}
