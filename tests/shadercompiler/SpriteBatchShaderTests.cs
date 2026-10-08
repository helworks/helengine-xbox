using helengine.xbox.shaderprobe;

/// <summary>Exercises the canonical shared sprite shader through the actual Xbox compiler.</summary>
internal static class SpriteBatchShaderTests {
    public static void Run(string repository) {
        string source = Path.GetFullPath(Path.Combine(repository,
            "../helengine/engine/helengine.editor/shaders/builtin/SpriteBatchShader.hlsl"));
        XboxShaderToolchainCompiler compiler = new();
        var vertex = compiler.Compile(repository, source, "VS", true);
        var pixel = compiler.Compile(repository, source, "PS", false);
        XboxShaderInterfaceBinding.ValidateArtifacts(vertex.Binary, pixel.Binary);
        if (vertex.VertexConstants.Count(binding => binding.Name == "BatchCameraBuffer" && binding.Row >= 0 && binding.Row < 4) != 4)
            throw new Exception("Shared sprite projection did not retain four physical matrix rows.");
        var texture = pixel.TextureBindings.Single();
        if (!vertex.InterfaceBindings.Any(binding => binding.Kind == 4 && binding.Semantic == "POSITION" && binding.Components == 4) ||
            !vertex.InterfaceBindings.Any(binding => binding.Kind == 4 && binding.Semantic == "TEXCOORD0" && binding.Components == 4))
            throw new Exception("Shared sprite input lost its homogeneous position or packed texture vector.");
        if (texture.TextureName != "BatchTexture" || texture.SamplerName != "BatchSampler" || texture.Stage != 0)
            throw new Exception("Shared sprite resources lost their names or native texture stage.");
        Console.WriteLine("Shared Textured SpriteBatch shader compiled and physical pair validated.");
        Console.WriteLine("VS: " + vertex.JobRoot);
        Console.WriteLine("PS: " + pixel.JobRoot);
    }
}
