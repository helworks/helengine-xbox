using Silk.NET.SPIRV.Cross;

namespace helengine.xbox.shaderprobe;

/// <summary>Preserves the original resources behind one emitted legacy combined sampler.</summary>
public sealed class XboxCombinedSampler {
    /// <summary>Gets the authored image resource name.</summary>
    public string TextureName { get; init; }
    /// <summary>Gets the authored sampler resource name.</summary>
    public string SamplerName { get; init; }
    /// <summary>Gets the compiler-API-assigned combined symbol, later matched against Cg metadata.</summary>
    public string CombinedName { get; init; }

    /// <summary>Copies native ID relationships and names the combined resource without rewriting emitted source.</summary>
    internal static unsafe IReadOnlyList<XboxCombinedSampler> Read(Cross api, Silk.NET.SPIRV.Cross.Compiler* compiler) {
        CombinedImageSampler* entries = null;
        nuint count = 0;
        if (api.CompilerGetCombinedImageSamplers(compiler, &entries, &count) != Result.Success)
            throw new InvalidOperationException("XBOX_TEXTURE_BINDING: cannot read combined sampler identities.");
        List<XboxCombinedSampler> result = [];
        for (nuint i = 0; i < count; ++i) {
            string texture = api.CompilerGetNameS(compiler, entries[i].ImageId);
            string sampler = api.CompilerGetNameS(compiler, entries[i].SamplerId);
            if (string.IsNullOrWhiteSpace(texture) || string.IsNullOrWhiteSpace(sampler))
                throw new InvalidOperationException("XBOX_TEXTURE_BINDING: combined sampler has an unnamed source resource.");
            string combined = "Xbox_CombinedSampler_" + entries[i].CombinedId;
            api.CompilerSetName(compiler, entries[i].CombinedId, combined);
            result.Add(new XboxCombinedSampler { TextureName = texture, SamplerName = sampler, CombinedName = combined });
        }
        return result.AsReadOnly();
    }
}
