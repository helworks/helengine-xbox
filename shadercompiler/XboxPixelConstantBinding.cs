using System.Globalization;
using System.Text.RegularExpressions;

namespace helengine.xbox.shaderprobe;

/// <summary>Maps one flattened float4 uniform row to an fp20 global combiner constant.</summary>
public sealed class XboxPixelConstantBinding {
    /// <summary>Gets the compiler-emitted buffer name.</summary>
    public string Name { get; init; }
    /// <summary>Gets the float4 row index within that buffer.</summary>
    public int Row { get; init; }
    /// <summary>Gets the global combiner factor index, zero or one.</summary>
    public int Factor { get; init; }

    /// <summary>Reads Cg binding metadata without editing its source or combiner program.</summary>
    public static IReadOnlyList<XboxPixelConstantBinding> Read(string assembly) {
        ArgumentNullException.ThrowIfNull(assembly);
        List<XboxPixelConstantBinding> bindings = [];
        HashSet<int> factors = [];
        HashSet<string> rows = new(StringComparer.Ordinal);
        foreach (string line in assembly.Split('\n')) {
            string text = line.Trim();
            if (!text.StartsWith("//var ", StringComparison.Ordinal)) continue;
            string[] fields = text[6..].Split(':', StringSplitOptions.TrimEntries);
            if (fields.Length != 5) throw Invalid(text);
            if (!fields[2].StartsWith("COMBINER_", StringComparison.Ordinal)) continue;
            Match variable = Regex.Match(fields[0], @"^float4 ([A-Za-z_][A-Za-z_0-9]*)(?:\[([0-9]+)\])?$", RegexOptions.CultureInvariant);
            int factor = fields[2] switch { "COMBINER_CONST0" => 0, "COMBINER_CONST1" => 1, _ => -1 };
            if (!variable.Success || factor < 0) throw Invalid(text);
            int row = 0;
            if (variable.Groups[2].Success && (!int.TryParse(variable.Groups[2].Value, NumberStyles.None, CultureInfo.InvariantCulture, out row) || row >= 96))
                throw Invalid(text);
            string name = variable.Groups[1].Value;
            if (name.Length >= 64 || !factors.Add(factor) || !rows.Add(name + "[" + row.ToString(CultureInfo.InvariantCulture) + "]")) throw Invalid(text);
            bindings.Add(new XboxPixelConstantBinding { Name = name, Row = row, Factor = factor });
        }
        return bindings.OrderBy(binding => binding.Factor).ToList().AsReadOnly();
    }

    /// <summary>Rejects ambiguous, colliding or unsupported assignments before artifact publication.</summary>
    static InvalidOperationException Invalid(string detail) => new("XBOX_PIXEL_BINDING: " + detail);
}
