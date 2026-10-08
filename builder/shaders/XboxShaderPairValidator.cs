namespace helengine.xbox.builder;

/// <summary>Connects authored stage signatures by semantic and type, never by reflection list position.</summary>
public static class XboxShaderPairValidator {
    /// <summary>Rejects incompatible stages before the cooker can publish a reusable package.</summary>
    public static void Validate(ShaderProgramDefinition vertex, ShaderProgramDefinition pixel) {
        ArgumentNullException.ThrowIfNull(vertex);
        ArgumentNullException.ThrowIfNull(pixel);
        if (vertex.Stage != ShaderStage.Vertex || pixel.Stage != ShaderStage.Pixel) throw Invalid("Expected a vertex/pixel pair.");
        Dictionary<string, ShaderVertexElement> outputs = Signature(vertex.Outputs, true);
        Dictionary<string, ShaderVertexElement> inputs = Signature(pixel.Inputs, false);
        if (!outputs.TryGetValue("POSITION0", out ShaderVertexElement position) || position.Format != "float4")
            throw Invalid("Vertex stage must produce a float4 position.");
        foreach (var input in inputs) {
            if (!outputs.TryGetValue(input.Key, out ShaderVertexElement output))
                throw Invalid($"Pixel stage requires {input.Key} ({input.Value.Format}), absent from vertex outputs.");
            if (output.Format != input.Value.Format)
                throw Invalid($"{input.Key} type mismatch: vertex {output.Format}, pixel {input.Value.Format}.");
        }
        if (pixel.Outputs.Length != 1 || pixel.Outputs[0] == null || pixel.Outputs[0].Index != 0 || pixel.Outputs[0].Format != "float4" ||
            (pixel.Outputs[0].Semantic.ToUpperInvariant() != "SV_TARGET" && pixel.Outputs[0].Semantic.ToUpperInvariant() != "COLOR"))
            throw Invalid("Pixel stage must produce one float4 colour target.");
    }

    /// <summary>Normalizes position aliases and checks the bounded initial varying set.</summary>
    static Dictionary<string, ShaderVertexElement> Signature(ShaderVertexElement[] elements, bool vertexOutput) {
        Dictionary<string, ShaderVertexElement> signature = new(StringComparer.Ordinal);
        foreach (ShaderVertexElement element in elements) {
            if (element == null) throw Invalid("Null interface element.");
            string semantic = element.Semantic.ToUpperInvariant();
            if (semantic == "SV_POSITION") semantic = "POSITION";
            if (element.Index != 0 || (semantic != "COLOR" && semantic != "TEXCOORD" && !(vertexOutput && semantic == "POSITION")))
                throw Invalid("Unsupported varying " + element.Semantic + element.Index + ".");
            string key = semantic + element.Index;
            if (!signature.TryAdd(key, element)) throw Invalid("Duplicate varying " + key + ".");
        }
        return signature;
    }

    /// <summary>Provides a stable diagnostic family for pair failures.</summary>
    static InvalidOperationException Invalid(string detail) => new("XBOX_SHADER_INTERFACE: " + detail);
}
