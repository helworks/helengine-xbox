using helengine;
using helengine.xbox.builder;

/// <summary>Checks varying identity and type independently of SPIR-V location order.</summary>
internal static class PairInterfaceTests {
    /// <summary>Requires every consumed semantic and format, permits unused outputs and no-input pixel stages.</summary>
    public static void Run() {
        ShaderVertexElement position = new("SV_POSITION", 0, "float4");
        ShaderVertexElement colour = new("COLOR", 0, "float4");
        ShaderVertexElement uv = new("TEXCOORD", 0, "float2");
        ShaderProgramDefinition vertex = Program(ShaderStage.Vertex, [], [position, uv, colour]);
        XboxShaderPairValidator.Validate(vertex, Program(ShaderStage.Pixel, [colour, uv], [new("SV_TARGET", 0, "float4")]));
        XboxShaderPairValidator.Validate(vertex, Program(ShaderStage.Pixel, [], [new("SV_TARGET", 0, "float4")]));
        Reject(Program(ShaderStage.Vertex, [], [position, colour]), Program(ShaderStage.Pixel, [uv], [new("SV_TARGET", 0, "float4")]));
        Reject(vertex, Program(ShaderStage.Pixel, [new("COLOR", 0, "float3")], [new("SV_TARGET", 0, "float4")]));
        Reject(vertex, Program(ShaderStage.Pixel, [new("TEXCOORD", 1, "float2")], [new("SV_TARGET", 0, "float4")]));
        Reject(Program(ShaderStage.Vertex, [], [position, colour, colour]), Program(ShaderStage.Pixel, [colour], [new("SV_TARGET", 0, "float4")]));
        Reject(vertex, Program(ShaderStage.Pixel, [colour, colour], [new("SV_TARGET", 0, "float4")]));
        Reject(Program(ShaderStage.Vertex, [], [colour]), Program(ShaderStage.Pixel, [colour], [new("SV_TARGET", 0, "float4")]));
        Reject(vertex, vertex);
        Reject(vertex, Program(ShaderStage.Pixel, [], [null]));
        Console.WriteLine("Stage-pair semantic/type, missing/duplicate varying and constant-colour contracts passed.");
    }

    /// <summary>Constructs typed signatures without invoking a compiler for synthetic invalid metadata.</summary>
    static ShaderProgramDefinition Program(ShaderStage stage, ShaderVertexElement[] inputs, ShaderVertexElement[] outputs) =>
        new("test", stage, "main", [], inputs, outputs, []);

    /// <summary>Requires the named pair diagnostic rather than accepting a partial connection.</summary>
    static void Reject(ShaderProgramDefinition vertex, ShaderProgramDefinition pixel) {
        try { XboxShaderPairValidator.Validate(vertex, pixel); }
        catch (InvalidOperationException error) when (error.Message.Contains("XBOX_SHADER_INTERFACE")) { return; }
        throw new Exception("Invalid stage interface pair was accepted.");
    }
}
