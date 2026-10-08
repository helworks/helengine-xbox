using helengine.xbox.shaderprobe;

/// <summary>Checks original image/sampler identity through legacy combined-sampler emission.</summary>
internal static class TextureBindingTests {
    /// <summary>Rejects missing, duplicate and foreign Cg sampler assignments instead of guessing a slot.</summary>
    public static void RunMetadataParser() {
        XboxCombinedSampler[] combined = [new() { TextureName = "Albedo", SamplerName = "AlbedoSampler", CombinedName = "GeneratedSampler" }];
        const string line = "//var sampler2D GeneratedSampler : : 0 : -1 : 1\n";
        var binding = XboxTextureBinding.Read(line, combined).Single();
        if (binding.TextureName != "Albedo" || binding.SamplerName != "AlbedoSampler" || binding.Stage != 0)
            throw new Exception("Compiler-generated sampler name lost its original resource mapping.");
        foreach (string invalid in new[] { "", line + line, line.Replace("GeneratedSampler", "Foreign"),
            line.Replace(": 0 :", ": 1 :"), line.Replace("sampler2D", "samplerCUBE"),
            line.Replace("GeneratedSampler", "GeneratedSampler[0]"), "//var sampler2D GeneratedSampler" }) {
            try { XboxTextureBinding.Read(invalid, combined); }
            catch (InvalidOperationException error) when (error.Message.Contains("XBOX_TEXTURE_BINDING")) { continue; }
            throw new Exception("Invalid Cg sampler assignment was accepted.");
        }
        Console.WriteLine("Texture binding parser rejects missing, duplicate, foreign and unsupported sampler assignments.");
    }
    /// <summary>Requires the actual Cg unit rather than reusing authored t3/s5 binding indices.</summary>
    public static void RunNative(string repository) {
        string path = Path.Combine(repository, "tests/shaders/textured.hlsl");
        string source = File.ReadAllText(path).Replace("register(t0)", "register(t3)").Replace("register(s0)", "register(s5)");
        var request = new XboxShaderSourceRequest(new XboxShaderSource(path, source), "PSMain", false,
            new Dictionary<string, string>(), (_, _) => throw new InvalidOperationException("Fixture unexpectedly requested an include."));
        var result = new XboxShaderToolchainCompiler().Compile(repository, request);
        if (result.TextureBindings.Count != 1 || result.TextureBindings[0].TextureName != "Albedo" ||
            result.TextureBindings[0].SamplerName != "AlbedoSampler" || result.TextureBindings[0].Stage != 0 ||
            !result.Translation.Metadata.Any(item => item.Kind == "SeparateImage" && item.Binding == 3) ||
            !result.Translation.Metadata.Any(item => item.Kind == "SeparateSamplers" && item.Binding == 5))
            throw new Exception("Authored t3/s5 resources did not resolve to the actual Cg texture stage0.");
        if (BitConverter.ToUInt32(result.Binary, 4) != 6 || BitConverter.ToUInt32(result.Binary, 124) != 5)
            throw new Exception("Published shader artifact lost its physical texture/sampler map.");
        int offset = checked(128 + (int)BitConverter.ToUInt32(result.Binary, 12) * 4 + (int)BitConverter.ToUInt32(result.Binary, 120) * 72);
        if (result.Binary.Length != offset + 400 || BitConverter.ToUInt32(result.Binary, offset) != 1 ||
            System.Text.Encoding.ASCII.GetString(result.Binary, offset + 4, 64).TrimEnd('\0') != "Albedo" ||
            BitConverter.ToUInt32(result.Binary, offset + 68) != 0 || BitConverter.ToUInt32(result.Binary, offset + 80) != 2 ||
            System.Text.Encoding.ASCII.GetString(result.Binary, offset + 84, 64).TrimEnd('\0') != "AlbedoSampler" ||
            BitConverter.ToUInt32(result.Binary, offset + 148) != 0)
            throw new Exception("Published resource table does not match actual material names and Cg stage.");
        Console.WriteLine("Actual Cg sampler maps authored Albedo t3/AlbedoSampler s5 to stage0: " + result.JobRoot);
    }
    /// <summary>Requires a copied combined mapping after the native compiler context is destroyed.</summary>
    public static void RunFrontend(string repository) {
        var translated = new XboxShaderCompatibilityProbe().Translate(Path.Combine(repository, "tests/shaders/textured.hlsl"), "PSMain", false);
        if (translated.CombinedSamplers.Count != 1 || translated.CombinedSamplers[0].TextureName != "Albedo" ||
            translated.CombinedSamplers[0].SamplerName != "AlbedoSampler" || string.IsNullOrWhiteSpace(translated.CombinedSamplers[0].CombinedName))
            throw new Exception("Original texture/sampler identities were lost during Cg emission.");
        Console.WriteLine("Native combined sampler retains original texture and sampler identities.");
    }
}
