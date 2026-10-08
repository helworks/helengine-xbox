using System.Globalization;
using System.Text.RegularExpressions;

/// <summary>Executes the arithmetic subset emitted for the acceptance fixtures; unknown instructions fail the test.</summary>
internal sealed class Vp20ReferenceInterpreter {
    /// <summary>Stores source, temporary and output vectors addressed by vp20 assembly names.</summary>
    readonly Dictionary<string, float[]> Registers = new(StringComparer.Ordinal);
    /// <summary>Sets one explicit input or constant vector in the reference machine.</summary>
    public void Set(string name, params float[] values) { Registers[name] = values.ToArray(); }
    /// <summary>Gets the resulting vector, rejecting reads of uninitialized registers.</summary>
    public float[] Get(string name) => Registers[name];
    /// <summary>Interprets actual Cg output rather than inferring behavior from authored HLSL.</summary>
    public void Execute(string assembly) {
        foreach (string raw in assembly.Split('\n')) {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#') || line.StartsWith('!')) continue;
            if (line == "END") return;
            Match instruction = Regex.Match(line, @"^(\w+)\s+(.+);$");
            if (!instruction.Success) throw new Exception("Unrecognized vp20 instruction: " + line);
            string opcode = instruction.Groups[1].Value;
            string[] operands = instruction.Groups[2].Value.Split(',').Select(value => value.Trim()).ToArray();
            float[][] sources = operands.Skip(1).Select(Read).ToArray();
            float[] result = new float[4];
            for (int lane = 0; lane < 4; ++lane) result[lane] = opcode switch {
                "MOV" => sources[0][lane],
                "MUL" => sources[0][lane] * sources[1][lane],
                "ADD" => sources[0][lane] + sources[1][lane],
                "MAD" => sources[0][lane] * sources[1][lane] + sources[2][lane],
                "RCP" or "RCC" => 1.0f / sources[0][0],
                "DP4" => sources[0].Zip(sources[1], (a, b) => a * b).Sum(),
                _ => throw new Exception("Unimplemented reference opcode: " + opcode)
            };
            Write(operands[0], result);
        }
        throw new Exception("Vertex program has no END.");
    }
    /// <summary>Reads a register with vp20 source swizzle and optional negation.</summary>
    float[] Read(string operand) {
        Match match = Regex.Match(operand, @"^(-?)([A-Za-z0-9]+(?:\[[A-Za-z0-9]+\])?)(?:\.([xyzw]+))?$");
        if (!match.Success) throw new Exception("Unsupported vp20 operand: " + operand);
        string name = match.Groups[2].Value;
        float[] value = Registers.TryGetValue(name, out float[] register) ? register :
            float.TryParse(name, NumberStyles.Float, CultureInfo.InvariantCulture, out float scalar) ? [scalar, scalar, scalar, scalar] :
            throw new Exception("Uninitialized vp20 register: " + name);
        string swizzle = match.Groups[3].Success ? match.Groups[3].Value : "xyzw";
        float sign = match.Groups[1].Value == "-" ? -1 : 1;
        return Enumerable.Range(0, 4).Select(lane => sign * value["xyzw".IndexOf(swizzle[Math.Min(lane, swizzle.Length - 1)])]).ToArray();
    }
    /// <summary>Writes only the destination mask lanes, preserving the remaining register state.</summary>
    void Write(string operand, float[] value) {
        string[] parts = operand.Split('.');
        if (!Registers.TryGetValue(parts[0], out float[] target)) Registers[parts[0]] = target = new float[4];
        string mask = parts.Length == 1 ? "xyzw" : parts[1];
        foreach (char component in mask) target["xyzw".IndexOf(component)] = value["xyzw".IndexOf(component)];
    }
}
