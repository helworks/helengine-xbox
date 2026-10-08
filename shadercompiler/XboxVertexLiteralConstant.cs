using System.Globalization;
using System.Text.RegularExpressions;

namespace helengine.xbox.shaderprobe;

/// <summary>Preserves one immutable Cg-owned float4 constant register.</summary>
public sealed class XboxVertexLiteralConstant {
    /// <summary>Gets the physical vp20 register receiving the literal.</summary>
    public int Register { get; init; }
    /// <summary>Gets the four finite values in compiler-emitted component order.</summary>
    public IReadOnlyList<float> Values { get; init; }

    /// <summary>Reads literal metadata without modifying generated Cg or assembly.</summary>
    public static IReadOnlyList<XboxVertexLiteralConstant> Read(string assembly, IReadOnlyList<XboxVertexConstantBinding> uniforms) {
        ArgumentNullException.ThrowIfNull(assembly);
        ArgumentNullException.ThrowIfNull(uniforms);
        HashSet<int> occupied = new(uniforms.Select(binding => binding.Register));
        List<XboxVertexLiteralConstant> result = [];
        foreach (string line in assembly.Split('\n')) {
            string text = line.Trim();
            if (!text.StartsWith("#const", StringComparison.Ordinal)) continue;
            Match match = Regex.Match(text, @"^#const\s+c\[([0-9]+)\]\s*=\s*(.*)$", RegexOptions.CultureInvariant);
            if (!match.Success || !int.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out int register) ||
                register >= 93 || !occupied.Add(register)) throw Invalid(text);
            string[] components = match.Groups[2].Value.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
            if (components.Length != 4) throw Invalid(text);
            float[] values = new float[4];
            for (int index = 0; index < 4; index++) {
                if (!float.TryParse(components[index], NumberStyles.Float, CultureInfo.InvariantCulture, out values[index]) ||
                    !float.IsFinite(values[index])) throw Invalid(text);
            }
            result.Add(new XboxVertexLiteralConstant { Register = register, Values = Array.AsReadOnly(values) });
        }
        return result.OrderBy(item => item.Register).ToList().AsReadOnly();
    }

    /// <summary>Rejects malformed, reserved or overlapping register assignments.</summary>
    static InvalidOperationException Invalid(string detail) => new("XBOX_SHADER_LITERAL: " + detail);
}
