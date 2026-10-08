using helengine.xbox.shaderprobe;

/// <summary>Requires compiler-owned literal rows to retain values and reject colliding assignments.</summary>
internal static class VertexLiteralTests {
    /// <summary>Requires real Cg output to survive native artifact publication with exact float bits.</summary>
    internal static void RunNative(string repository) {
        var result = new XboxShaderToolchainCompiler().Compile(repository,
            Path.Combine(repository, "tests/shaders/literal-colour.hlsl"), "VSMain", true);
        byte[] bytes = result.Binary;
        int offset = checked(128 + (int)BitConverter.ToUInt32(bytes, 12) * 4 + (int)BitConverter.ToUInt32(bytes, 120) * 72);
        if (BitConverter.ToUInt32(bytes, 4) != 6 || BitConverter.ToUInt32(bytes, 124) != 5 || bytes.Length != offset + 400 ||
            BitConverter.ToUInt32(bytes, offset) != 3 || BitConverter.ToUInt32(bytes, offset + 68) != 5)
            throw new Exception("Real Cg literal register was not published in the artifact: " + result.JobRoot);
        float[] expected = [.25f, .5f, .75f, .875f];
        for (int index = 0; index < 4; index++)
            if (BitConverter.ToSingle(bytes, offset + 4 + index * 4) != expected[index])
                throw new Exception("Real Cg literal component changed during serialization.");
        Console.WriteLine("Real Cg literal artifact retained register5 and all four exact values: " + result.JobRoot);
    }
    /// <summary>Uses the literal row observed in the pinned Cg output, plus malformed boundaries.</summary>
    internal static void Run() {
        const string uniforms = "#var float4 Transform : : c[0] : -1 : 1\n"
            + "#var float4 Xbox_ViewportScale : C94 : c[94] : -1 : 1\n"
            + "#var float4 Xbox_ViewportOffset : C95 : c[95] : -1 : 1\n";
        IReadOnlyList<XboxVertexConstantBinding> bindings = XboxVertexConstantBinding.Read(uniforms);
        var values = XboxVertexLiteralConstant.Read("#const c[5] = 0.25 0.5 0.75 0.875\n", bindings);
        if (values.Count != 1 || values[0].Register != 5 ||
            !values[0].Values.SequenceEqual(new[] { .25f, .5f, .75f, .875f }))
            throw new Exception("Cg literal values were lost.");
        foreach (string invalid in new[] {
            "#const c[0] = 1 2 3 4", "#const c[93] = 1 2 3 4", "#const c[96] = 1 2 3 4",
            "#const c[5] = 1 2 3", "#const c[5] = 1 2 3 4 5", "#const c[5] = NaN 2 3 4",
            "#const c[5] = 1e999 2 3 4", "#const c[5] = 1 2 3 4\n#const c[5] = 5 6 7 8",
            "#const c[bad] = 1 2 3 4"
        }) {
            try { XboxVertexLiteralConstant.Read(invalid, bindings); }
            catch (InvalidOperationException error) when (error.Message.Contains("XBOX_SHADER_LITERAL")) { continue; }
            throw new Exception("Invalid Cg literal assignment was accepted: " + invalid);
        }
        Console.WriteLine("Cg literal constants preserve finite values and reject reserved/colliding registers.");
    }
}
