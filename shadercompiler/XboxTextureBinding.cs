using System.Globalization;
using System.Text.RegularExpressions;

namespace helengine.xbox.shaderprobe;

/// <summary>Connects original material resources to the texture unit assigned by the actual Cg compiler.</summary>
public sealed class XboxTextureBinding {
    /// <summary>Gets the authored image name.</summary>
    public string TextureName { get; init; }
    /// <summary>Gets the authored sampler name.</summary>
    public string SamplerName { get; init; }
    /// <summary>Gets the physical NV2A texture stage.</summary>
    public int Stage { get; init; }

    /// <summary>Matches Cg sampler metadata against native combined identities, rejecting missing or ambiguous assignments.</summary>
    public static IReadOnlyList<XboxTextureBinding> Read(string assembly, IReadOnlyList<XboxCombinedSampler> combined) {
        ArgumentNullException.ThrowIfNull(assembly);
        ArgumentNullException.ThrowIfNull(combined);
        if (combined.Count > 1) throw Invalid("Initial Xbox shaders support one combined texture/sampler.");
        List<XboxTextureBinding> result = [];
        foreach (string line in assembly.Split('\n')) {
            string text = line.Trim();
            if (!text.StartsWith("//var sampler", StringComparison.Ordinal)) continue;
            string[] fields = text[6..].Split(':', StringSplitOptions.TrimEntries);
            if (fields.Length != 5) throw Invalid(text);
            Match variable = Regex.Match(fields[0], @"^sampler2D ([A-Za-z_][A-Za-z_0-9]*)$", RegexOptions.CultureInvariant);
            if (!variable.Success || !int.TryParse(fields[2], NumberStyles.None, CultureInfo.InvariantCulture, out int stage) || stage != 0)
                throw Invalid("Only the verified ordinary 2D stage0 assignment is supported: " + text);
            var matches = combined.Where(item => item.CombinedName == variable.Groups[1].Value).ToArray();
            if (matches.Length != 1 || result.Count != 0) throw Invalid("Unknown or duplicate combined sampler: " + text);
            result.Add(new XboxTextureBinding { TextureName = matches[0].TextureName, SamplerName = matches[0].SamplerName, Stage = stage });
        }
        if (result.Count != combined.Count) throw Invalid("Cg omitted the active combined sampler assignment.");
        return result.AsReadOnly();
    }

    /// <summary>Creates the named unsupported or malformed texture binding diagnostic.</summary>
    static InvalidOperationException Invalid(string detail) => new("XBOX_TEXTURE_BINDING: " + detail);
}
