using System.Globalization;
using System.Text.RegularExpressions;

namespace helengine.xbox.shaderprobe;

/// <summary>Maps one flattened Cg uniform row to a physical vp20 constant register.</summary>
public sealed class XboxVertexConstantBinding {
    /// <summary>Gets the compiler uniform name before its optional flattened row index.</summary>
    public string Name { get; init; }
    /// <summary>Gets the flattened float4 row within the uniform, zero for a scalar vector.</summary>
    public int Row { get; init; }
    /// <summary>Gets the physical vp20 constant register assigned by Cg.</summary>
    public int Register { get; init; }
    /// <summary>Reads compiler metadata without rewriting compiler source or assembly.</summary>
    public static IReadOnlyList<XboxVertexConstantBinding> Read(string assembly) {
        ArgumentNullException.ThrowIfNull(assembly);
        List<XboxVertexConstantBinding> result = [];
        HashSet<int> registers = [];
        HashSet<string> rows = new(StringComparer.Ordinal);
        foreach (string line in assembly.Split('\n')) {
            string text = line.Trim();
            if (!text.StartsWith("#var ", StringComparison.Ordinal)) continue;
            string[] fields = text[5..].Split(':', StringSplitOptions.TrimEntries);
            if (fields.Length != 5) throw Invalid(text);
            if (!fields[2].StartsWith("c[", StringComparison.Ordinal)) continue;
            Match variable = Regex.Match(fields[0], @"^float[1-4]? ([A-Za-z_][A-Za-z_0-9]*)(?:\[([0-9]+)\])?$", RegexOptions.CultureInvariant);
            Match location = Regex.Match(fields[2], @"^c\[([0-9]+)\]$", RegexOptions.CultureInvariant);
            if (!variable.Success || !location.Success ||
                !int.TryParse(location.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out int register) || register >= 96)
                throw Invalid(text);
            int row = 0;
            if (variable.Groups[2].Success && (!int.TryParse(variable.Groups[2].Value, NumberStyles.None, CultureInfo.InvariantCulture, out row) || row >= 96))
                throw Invalid(text);
            string name = variable.Groups[1].Value;
            if (name.Length >= 64) throw Invalid(text);
            int reserved = name switch { "Xbox_TextureExtent" => 93, "Xbox_ViewportScale" => 94, "Xbox_ViewportOffset" => 95, _ => -1 };
            if ((reserved >= 0 && (register != reserved || row != 0)) || (reserved < 0 && register >= 93) ||
                !registers.Add(register) || !rows.Add(name + "[" + row.ToString(CultureInfo.InvariantCulture) + "]")) throw Invalid(text);
            result.Add(new XboxVertexConstantBinding { Name = name, Row = row, Register = register });
        }
        if (!result.Any(item => item.Name == "Xbox_ViewportScale") || !result.Any(item => item.Name == "Xbox_ViewportOffset"))
            throw Invalid("missing viewport ABI constants");
        return result.OrderBy(item => item.Register).ToList().AsReadOnly();
    }

    /// <summary>Reports malformed or colliding compiler assignments before publishing shader data.</summary>
    static InvalidOperationException Invalid(string detail) => new("XBOX_SHADER_BINDING: " + detail);
}
