using helengine.xbox.shaderprobe;

/// <summary>Checks compiler-owned fragment literals survive extraction as bounded combiner writes.</summary>
internal static class PixelLiteralTests {
    /// <summary>Requires the real fp20 toolchain to retain a nontrivial RGBA literal, without material uniforms.</summary>
    internal static void RunNative(string repository) {
        var result = new XboxShaderToolchainCompiler().Compile(repository,
            Path.Combine(repository, "tests/shaders/pixel-literal-colour.hlsl"), "PSMain", false);
        if (result.PixelConstants.Count != 0) throw new Exception("Literal was incorrectly exposed as a material uniform.");
        uint count = BitConverter.ToUInt32(result.Binary, 12);
        bool found = false;
        for (int word = 0; word < count; word += 2) {
            uint method = BitConverter.ToUInt32(result.Binary, 128 + word * 4);
            uint value = BitConverter.ToUInt32(result.Binary, 132 + word * 4);
            if (method == 0xA60 && value == 0xCC336699) found = true;
        }
        if (!found) throw new Exception("Pixel literal RGBA did not reach stage-zero factor0: " + result.JobRoot);
        Console.WriteLine("Actual fp20 literal retained ARGB 0xCC336699: " + result.JobRoot);
    }
}
