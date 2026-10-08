namespace helengine.xbox.shaderprobe;

/// <summary>Runs an experimental translation without changing engine shader registration.</summary>
internal static class Program {
    /// <summary>Writes compiler evidence only after translation succeeds; rejects reused outputs.</summary>
    public static int Main(string[] args) {
        try {
            if (args.Length != 8 || args[0] != "--source" || args[2] != "--entry" ||
                args[4] != "--stage" || args[6] != "--output") {
                throw new ArgumentException("Expected --source PATH --entry NAME --stage vertex|pixel --output PREFIX");
            }
            if (args[5] != "vertex" && args[5] != "pixel") throw new ArgumentException("Unknown shader stage.");
            string prefix = Path.GetFullPath(args[7]);
            string filename = Path.GetFileName(prefix);
            if (filename.Any(c => !char.IsAsciiLetterOrDigit(c) && c != '-' && c != '_'))
                throw new ArgumentException("Output basename must contain only ASCII letters, digits, hyphens or underscores.");
            foreach (string suffix in new[] { ".spv", ".cg", ".entry.cg", ".reflection.txt" }) {
                if (File.Exists(prefix + suffix)) throw new IOException("Refusing to overwrite " + prefix + suffix);
            }
            XboxShaderProbeOutput output = new XboxShaderCompatibilityProbe().Translate(args[1], args[3], args[5] == "vertex");
            Directory.CreateDirectory(Path.GetDirectoryName(prefix));
            File.WriteAllBytes(prefix + ".spv", output.Spirv);
            File.WriteAllText(prefix + ".cg", output.Source);
            File.WriteAllText(prefix + ".entry.cg", $"#include \"{filename}.cg\"\n" + output.AdapterSource);
            File.WriteAllText(prefix + ".reflection.txt", output.InterfaceReport);
            Console.WriteLine("EntryPoint=" + output.EntryPoint);
            return 0;
        } catch (Exception exception) {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }
}
