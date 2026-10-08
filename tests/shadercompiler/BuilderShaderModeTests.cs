/// <summary>Checks explicit shader-runtime opt-in without changing the legacy default.</summary>
internal static class BuilderShaderModeTests {
    /// <summary>Requires packaging and codegen to select the same material resolution contract.</summary>
    public static void Run(string assemblyPath) {
        const string option = "HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME";
        string previous = Environment.GetEnvironmentVariable(option);
        var assembly = System.Reflection.Assembly.LoadFrom(Path.GetFullPath(assemblyPath));
        var type = assembly.GetType("helengine.xbox.builder.XboxPlatformAssetBuilder", true);
        try {
            foreach (bool enabled in new[] { false, true }) {
                Environment.SetEnvironmentVariable(option, enabled ? "1" : null);
                dynamic builder = Activator.CreateInstance(type);
                dynamic definition = builder.Definition;
                string mode = definition.RuntimeGenerationContract.MaterialResolutionMode.ToString();
                bool fatalOnly = false;
                foreach (string symbol in definition.RuntimeGenerationContract.PortableInputPreprocessorSymbols)
                    fatalOnly |= symbol == "HELENGINE_CODEGEN_FATAL_ONLY";
                if (!fatalOnly) throw new Exception("Xbox no-exception codegen did not select the shared fatal-only path.");
                if (mode != (enabled ? "RawShaderBacked" : "CookedPlatformOwned"))
                    throw new Exception("Explicit shader mode selected the wrong material loading contract: " + mode);
                bool shadersDisabled = false;
                foreach (dynamic setting in definition.CodegenProfiles[0].Settings) {
                    if ((string)setting.SettingId == "codegen-forced-disabled-features")
                        shadersDisabled = ((string)setting.DefaultValue).Split(';').Contains("shaders");
                }
                if (shadersDisabled == enabled) throw new Exception("Shader codegen disagrees with material packaging.");
            }
            Environment.SetEnvironmentVariable(option, "invalid");
            try {
                Activator.CreateInstance(type);
                throw new Exception("Invalid runtime mode silently selected a fallback.");
            } catch (System.Reflection.TargetInvocationException error) when (error.InnerException is InvalidOperationException) { }
        } finally {
            Environment.SetEnvironmentVariable(option, previous);
        }
        Console.WriteLine("Explicit shader runtime packaging/codegen mode and legacy default passed.");
    }
}
