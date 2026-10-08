using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using helengine;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.xbox.shaderfixture;

/// <summary>Verifies the platform cook capability with real authored source and serialized runtime packages.</summary>
internal static class ShaderArtifactCookTest {
    /// <summary>Requires the actual builder to compile and publish the material-selected pair.</summary>
    internal static void Run(string repository, string builderPath) {
        string root = Path.GetFullPath(repository);
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_REPOSITORY_ROOT", root);
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME", "1");
        Assembly assembly = Assembly.LoadFrom(Path.GetFullPath(builderPath));
        object builder = Activator.CreateInstance(assembly.GetType("helengine.xbox.builder.XboxPlatformAssetBuilder", true));
        if (builder is not IPlatformShaderArtifactBuilder cooker)
            throw new InvalidOperationException("Xbox builder does not expose platform shader artifact cooking.");
        string sourcePath = Path.Combine(root, ".xbox/projects/cube-test-shaders/assets/XboxFixtureColour.hlsl");
        string text = File.ReadAllText(sourcePath);
        string hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text))).ToLowerInvariant();
        PlatformShaderDependency dependency = new("XboxFixtureColour", "XboxFixtureColour.vs", "XboxFixtureColour.ps", "default");
        PlatformShaderArtifactCookSource source = new("XboxFixtureColour", hash, text, sourcePath);
        string output = Path.Combine(root, ".xbox/verification/cook-" + Guid.NewGuid().ToString("N"));
        PlatformShaderArtifactCookResult result = cooker.CookShaderArtifacts(new PlatformShaderArtifactCookRequest(
            output, "xbox", "debug", "xbox-shader", [dependency, dependency], [source]));
        if (result.CookedArtifactDeclarations.Count != 1) throw new InvalidOperationException("Cook must deduplicate identical material dependencies.");
        var declaration = result.CookedArtifactDeclarations[0];
        if (declaration.RelativePath != "shaders/xboxfixturecolour.xbox.hasset" || declaration.ArtifactKind != "shader")
            throw new InvalidOperationException("Cook declaration disagrees with runtime shader package path.");
        ShaderAsset shader;
        using (FileStream stream = File.OpenRead(Path.Combine(output, declaration.RelativePath)))
            shader = ShaderAssetBinarySerializer.Deserialize(stream);
        if (shader.Id != "XboxFixtureColour" || shader.TargetName != "xbox" || shader.Programs.Length != 2 || shader.Binaries.Length != 2)
            throw new InvalidOperationException("Cooked package identity or program count is incorrect.");
        foreach (ShaderStage stage in new[] { ShaderStage.Vertex, ShaderStage.Pixel }) {
            string name = stage == ShaderStage.Vertex ? "XboxFixtureColour.vs" : "XboxFixtureColour.ps";
            ShaderBinaryAsset binary = shader.Binaries.Single(item => item.ProgramName == name && item.Stage == stage && item.Variant == "default");
            if (binary.TargetName != "xbox" || binary.Bytecode.Length <= 128 || BitConverter.ToUInt32(binary.Bytecode, 0) != 0x31535848 ||
                BitConverter.ToUInt32(binary.Bytecode, 8) != (stage == ShaderStage.Vertex ? 1u : 2u))
                throw new InvalidOperationException("Cooked package contains an invalid Xbox stage.");
        }
        AssertRejected(() => cooker.CookShaderArtifacts(new PlatformShaderArtifactCookRequest(output, "windows", "debug", "xbox-shader", [], [])));
        PlatformShaderDependency unsupported = new("XboxFixtureColour", "XboxFixtureColour.vs", "XboxFixtureColour.ps", "UnknownVariant");
        AssertRejected(() => cooker.CookShaderArtifacts(new PlatformShaderArtifactCookRequest(output, "xbox", "debug", "xbox-shader", [unsupported], [source])));
        if (!File.Exists(Path.Combine(output, declaration.RelativePath))) throw new InvalidOperationException("Invalid request mutated an unrelated successful default package.");
        PlatformShaderArtifactCookSource invalidSource = new("XboxFixtureColour", hash, "float4 VS() : SV_Position { return missing_symbol; }", sourcePath);
        AssertRejected(() => cooker.CookShaderArtifacts(new PlatformShaderArtifactCookRequest(output, "xbox", "debug", "xbox-shader", [dependency], [invalidSource])));
        if (File.Exists(Path.Combine(output, declaration.RelativePath))) throw new InvalidOperationException("Failed recompilation left a reusable stale shader package.");
        Environment.SetEnvironmentVariable("HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME", "0");
        IPlatformShaderArtifactBuilder legacy = (IPlatformShaderArtifactBuilder)Activator.CreateInstance(assembly.GetType("helengine.xbox.builder.XboxPlatformAssetBuilder", true));
        AssertRejected(() => legacy.CookShaderArtifacts(new PlatformShaderArtifactCookRequest(output, "xbox", "debug", "xbox-shader", [dependency], [source])));
        Console.WriteLine("Actual builder shader cook and duplicate dependency publication passed: " + output);
        Console.WriteLine("Foreign target/variant/legacy requests rejected; failed compilation removed the requested stale package.");
    }

    /// <summary>Requires an explicit unsupported-request or compilation diagnostic from the production cooker.</summary>
    private static void AssertRejected(Action operation) {
        try { operation(); }
        catch (InvalidOperationException) { return; }
        throw new Exception("Invalid shader cook unexpectedly succeeded.");
    }
}
