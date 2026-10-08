using System.Buffers.Binary;
using System.Text;

namespace helengine.xbox.shaderprobe;

/// <summary>Records a compiler-confirmed semantic and its NV2A attribute or varying route.</summary>
public sealed class XboxShaderInterfaceBinding {
    /// <summary>Gets record kind: 4 VS input, 5 VS output, 6 PS input, 7 PS output.</summary>
    public int Kind { get; init; }
    /// <summary>Gets the canonical POSITION, COLOR0 or TEXCOORD0 semantic.</summary>
    public string Semantic { get; init; }
    /// <summary>Gets the NV2A attribute/varying route, or zero for the pixel colour target.</summary>
    public int Register { get; init; }
    /// <summary>Gets the number of float components in the active interface element.</summary>
    public int Components { get; init; }

    /// <summary>Reads final Cg interface assignments and checks the pinned translator's bounded ABI.</summary>
    public static IReadOnlyList<XboxShaderInterfaceBinding> Read(string assembly, bool vertex) {
        ArgumentNullException.ThrowIfNull(assembly);
        List<XboxShaderInterfaceBinding> result = [];
        HashSet<string> occupied = new(StringComparer.Ordinal);
        string prefix = vertex ? "#var " : "//var ";
        foreach (string line in assembly.Split('\n')) {
            string text = line.Trim();
            if (!text.StartsWith(prefix, StringComparison.Ordinal)) continue;
            string[] fields = text[prefix.Length..].Split(':', StringSplitOptions.TrimEntries);
            if (fields.Length != 5) throw Invalid(text);
            bool input = fields[1].StartsWith("$vin.", StringComparison.Ordinal);
            bool output = fields[1].StartsWith("$vout.", StringComparison.Ordinal);
            if (!input && !output) continue;
            if (fields[4] == "0") continue;
            if (fields[4] != "1") throw Invalid(text);
            string semantic = fields[1][(input ? 5 : 6)..];
            int components = semantic switch { "POSITION" => input ? 3 : 4, "COLOR0" => 4, "TEXCOORD0" => 2, _ => 0 };
            if (vertex && input && (semantic == "POSITION" || semantic == "TEXCOORD0") &&
                fields[0].StartsWith("float4 ", StringComparison.Ordinal)) components = 4;
            if (components == 0 || (!vertex && semantic == "POSITION") || (!vertex && output && semantic != "COLOR0") ||
                !fields[0].StartsWith("float" + components + " ", StringComparison.Ordinal)) throw Invalid(text);
            string location;
            if (vertex && input) location = semantic switch { "POSITION" => "ATTR0", "COLOR0" => "ATTR3", _ => "TEXCOORD0" };
            else if (vertex) location = semantic switch { "POSITION" => "HPOS", "COLOR0" => "COL0", _ => "TEX0" };
            else location = input ? semantic : "COLOR";
            int kind = vertex ? (input ? 4 : 5) : (input ? 6 : 7);
            if (fields[2] != location || !occupied.Add(kind + ":" + semantic)) throw Invalid(text);
            int register = !vertex && output ? 0 : semantic switch { "POSITION" => 0, "COLOR0" => 3, _ => 9 };
            result.Add(new XboxShaderInterfaceBinding { Kind = kind, Semantic = semantic, Register = register, Components = components });
        }
        if (!result.Any(item => item.Kind == (vertex ? 5 : 7) && item.Semantic == (vertex ? "POSITION" : "COLOR0")))
            throw Invalid("Missing required stage output.");
        return result.OrderBy(item => item.Kind).ThenBy(item => item.Register).ToList().AsReadOnly();
    }

    /// <summary>Rejects missing or incompatible routes before publishing a selected pair.</summary>
    public static void ValidatePair(IReadOnlyList<XboxShaderInterfaceBinding> vertex, IReadOnlyList<XboxShaderInterfaceBinding> pixel) {
        ArgumentNullException.ThrowIfNull(vertex);
        ArgumentNullException.ThrowIfNull(pixel);
        foreach (XboxShaderInterfaceBinding input in pixel.Where(item => item.Kind == 6)) {
            if (!vertex.Any(output => output.Kind == 5 && output.Semantic == input.Semantic &&
                output.Register == input.Register && output.Components == input.Components))
                throw Invalid("Pixel input has no matching vertex route: " + input.Semantic);
        }
    }

    /// <summary>Checks physical pair metadata in native-validated compiler artifacts before packaging.</summary>
    public static void ValidateArtifacts(byte[] vertex, byte[] pixel) {
        ValidatePair(ReadArtifact(vertex, true), ReadArtifact(pixel, false));
    }

    /// <summary>Bounds and decodes interface records; GPU instruction validation remains the native extractor's responsibility.</summary>
    private static IReadOnlyList<XboxShaderInterfaceBinding> ReadArtifact(byte[] bytes, bool vertex) {
        ArgumentNullException.ThrowIfNull(bytes);
        if (bytes.Length < 128 || Word(bytes, 0) != 0x31535848 || Word(bytes, 4) != 6 ||
            Word(bytes, 8) != (vertex ? 1u : 2u) || Word(bytes, 16) != 2)
            throw Invalid("Invalid physical-interface artifact header.");
        uint words = Word(bytes, 12), constants = Word(bytes, 120), resources = Word(bytes, 124);
        if (words == 0 || words > 544 || constants > (vertex ? 96u : 2u) || resources == 0 || resources > 102 ||
            bytes.Length != 128L + words * 4L + constants * 72L + resources * 80L)
            throw Invalid("Invalid physical-interface artifact length.");
        uint checksum = 2166136261;
        for (int index = 128; index < bytes.Length; ++index) checksum = unchecked((checksum ^ bytes[index]) * 16777619);
        if (checksum != Word(bytes, 20)) throw Invalid("Physical-interface artifact checksum mismatch.");
        List<XboxShaderInterfaceBinding> result = [];
        HashSet<string> occupied = new(StringComparer.Ordinal);
        int start = checked(128 + (int)words * 4 + (int)constants * 72);
        for (int index = 0; index < resources; ++index) {
            int offset = start + index * 80;
            uint kind = Word(bytes, offset);
            if (kind < 4) {
                if (vertex ? kind != 3 : kind != 1 && kind != 2) throw Invalid("Foreign resource kind.");
                continue;
            }
            if (vertex ? kind != 4 && kind != 5 : kind != 6 && kind != 7)
                throw Invalid("Foreign interface stage.");
            ReadOnlySpan<byte> name = bytes.AsSpan(offset + 4, 64);
            int length = name.IndexOf((byte)0);
            if (length <= 0 || name[length..].IndexOfAnyExcept((byte)0) >= 0)
                throw Invalid("Invalid semantic padding.");
            string semantic = Encoding.ASCII.GetString(name[..length]);
            int components = semantic switch {
                "POSITION" when vertex => kind == 4 ? 3 : 4,
                "COLOR0" => 4,
                "TEXCOORD0" when kind != 7 => 2,
                _ => 0
            };
            int register = semantic == "TEXCOORD0" ? 9 : semantic == "COLOR0" && kind != 7 ? 3 : 0;
            if (kind == 4 && (semantic == "POSITION" || semantic == "TEXCOORD0") && Word(bytes, offset + 72) == 4)
                components = 4;
            if (components == 0 || Word(bytes, offset + 68) != register || Word(bytes, offset + 72) != components ||
                Word(bytes, offset + 76) != 0 || !occupied.Add(kind + ":" + semantic))
                throw Invalid("Invalid physical interface assignment.");
            result.Add(new XboxShaderInterfaceBinding { Kind = (int)kind, Semantic = semantic, Register = register, Components = components });
        }
        if (!result.Any(item => item.Kind == (vertex ? 5 : 7) && item.Semantic == (vertex ? "POSITION" : "COLOR0")))
            throw Invalid("Missing required stage output.");
        return result.AsReadOnly();
    }

    /// <summary>Reads a bounded little-endian artifact field.</summary>
    private static uint Word(byte[] bytes, int offset) => BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset, 4));

    /// <summary>Provides a stable diagnostic for unsupported or conflicting physical assignments.</summary>
    static InvalidOperationException Invalid(string detail) => new("XBOX_SHADER_INTERFACE: " + detail);
}
