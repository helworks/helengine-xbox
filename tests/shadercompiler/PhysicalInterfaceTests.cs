using helengine.xbox.shaderprobe;

/// <summary>Tests actual Cg semantic routes rather than equating reflection locations with NV2A attributes.</summary>
internal static class PhysicalInterfaceTests {
    /// <summary>Requires the actual compiler to publish stage signatures alongside its instructions.</summary>
    internal static void RunNative(string repository) {
        XboxShaderToolchainCompiler compiler = new();
        string path = Path.Combine(repository, "tests/shaders/textured.hlsl");
        var vertex = compiler.Compile(repository, path, "VSMain", true);
        var pixel = compiler.Compile(repository, path, "PSMain", false);
        var vs = XboxShaderInterfaceBinding.Read(File.ReadAllText(Path.Combine(vertex.JobRoot, "shader.asm")), true);
        var ps = XboxShaderInterfaceBinding.Read(File.ReadAllText(Path.Combine(pixel.JobRoot, "shader.asm")), false);
        XboxShaderInterfaceBinding.ValidatePair(vs, ps);
        if (BitConverter.ToUInt32(vertex.Binary, 4) != 6 || BitConverter.ToUInt32(vertex.Binary, 124) != 6 ||
            BitConverter.ToUInt32(pixel.Binary, 4) != 6 || BitConverter.ToUInt32(pixel.Binary, 124) != 5)
            throw new Exception("Actual compiler did not publish physical interface records: " + vertex.JobRoot + " / " + pixel.JobRoot);
        XboxShaderInterfaceBinding.ValidateArtifacts(vertex.Binary, pixel.Binary);
        RejectArtifacts(vertex.Binary[..127], pixel.Binary);
        RejectArtifacts(vertex.Binary[..^1], pixel.Binary);
        RejectArtifacts(pixel.Binary, vertex.Binary);
        byte[] corrupt = (byte[])vertex.Binary.Clone();
        corrupt[128] ^= 1;
        RejectArtifacts(corrupt, pixel.Binary);
        int records = 128 + (int)BitConverter.ToUInt32(vertex.Binary, 12) * 4 + (int)BitConverter.ToUInt32(vertex.Binary, 120) * 72;
        // Last record is VS TEXCOORD0 output: turn it into a duplicate colour output,
        // corrupt its physical route/type, and remove it entirely (with valid checksums).
        int last = records + 5 * 80;
        RejectArtifacts(ChangeWord(vertex.Binary, last + 68, 3), pixel.Binary);
        RejectArtifacts(ChangeWord(vertex.Binary, last + 72, 4), pixel.Binary);
        RejectArtifacts(ChangeWord(vertex.Binary, last, 6), pixel.Binary);
        byte[] duplicate = (byte[])vertex.Binary.Clone();
        Array.Copy(duplicate, last - 80, duplicate, last, 80);
        RejectArtifacts(ChangeWord(duplicate, 124, 6), pixel.Binary);
        RejectArtifacts(ChangeWord(vertex.Binary[..^80], 124, 5), pixel.Binary);
        Console.WriteLine("Actual Cg pair publishes physical interfaces: " + vertex.JobRoot + " / " + pixel.JobRoot);
    }

    /// <summary>Mutates metadata and repairs the checksum so semantic validation is exercised.</summary>
    private static byte[] ChangeWord(byte[] source, int offset, uint value) {
        byte[] result = (byte[])source.Clone();
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(offset, 4), value);
        uint checksum = 2166136261;
        for (int index = 128; index < result.Length; ++index) checksum = unchecked((checksum ^ result[index]) * 16777619);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(20, 4), checksum);
        return result;
    }

    /// <summary>Requires malformed or incompatible compiler artifacts to fail before publication.</summary>
    private static void RejectArtifacts(byte[] vertex, byte[] pixel) {
        try { XboxShaderInterfaceBinding.ValidateArtifacts(vertex, pixel); }
        catch (InvalidOperationException error) when (error.Message.Contains("XBOX_SHADER_INTERFACE")) { return; }
        throw new Exception("Invalid physical artifact pair was accepted.");
    }
    /// <summary>Requires pinned POSITION/COLOR0/TEXCOORD0 routes and fail-closed incompatible metadata.</summary>
    internal static void Run() {
        const string vertex = "#var float3 i.position : $vin.POSITION : ATTR0 : 0 : 1\n"
            + "#var float4 i.colour : $vin.COLOR0 : ATTR3 : 0 : 1\n"
            + "#var float2 i.uv : $vin.TEXCOORD0 : TEXCOORD0 : 0 : 1\n"
            + "#var float4 o.position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float4 o.colour : $vout.COLOR0 : COL0 : -1 : 1\n"
            + "#var float2 o.uv : $vout.TEXCOORD0 : TEX0 : -1 : 1\n";
        const string pixel = "//var float4 i.colour : $vin.COLOR0 : COLOR0 : 0 : 1\n"
            + "//var float2 i.uv : $vin.TEXCOORD0 : TEXCOORD0 : 0 : 1\n"
            + "//var float4 o.colour : $vout.COLOR0 : COLOR : -1 : 1\n";
        var vs = XboxShaderInterfaceBinding.Read(vertex, true);
        var batch = XboxShaderInterfaceBinding.Read(vertex.Replace("float3 i.position", "float4 i.position")
            .Replace("float2 i.uv", "float4 i.uv"), true);
        if (batch.Count(item => item.Kind == 4 && item.Components == 4) != 3)
            throw new Exception("Batch float4 vertex inputs were truncated.");
        var ps = XboxShaderInterfaceBinding.Read(pixel, false);
        if (vs.Count != 6 || ps.Count != 3 ||
            !vs.Any(item => item.Kind == 4 && item.Semantic == "TEXCOORD0" && item.Register == 9 && item.Components == 2) ||
            !vs.Any(item => item.Kind == 5 && item.Semantic == "COLOR0" && item.Register == 3 && item.Components == 4))
            throw new Exception("Cg physical interface routes were lost.");
        XboxShaderInterfaceBinding.ValidatePair(vs, ps);
        foreach (string invalid in new[] {
            vertex.Replace("ATTR3", "ATTR9"), vertex.Replace("float2 i.uv", "float3 i.uv"),
            vertex.Replace("HPOS", "COL0"), vertex.Replace("$vin.TEXCOORD0", "$vin.TEXCOORD1"),
            vertex + "#var float4 duplicate : $vout.COLOR0 : COL0 : -1 : 1\n",
            vertex.Replace("$vout.POSITION", "$vout.UNKNOWN")
        }) {
            try { XboxShaderInterfaceBinding.Read(invalid, true); }
            catch (InvalidOperationException error) when (error.Message.Contains("XBOX_SHADER_INTERFACE")) { continue; }
            throw new Exception("Invalid physical interface was accepted.");
        }
        var missingUv = XboxShaderInterfaceBinding.Read(vertex.Replace("#var float2 o.uv : $vout.TEXCOORD0 : TEX0 : -1 : 1\n", ""), true);
        try { XboxShaderInterfaceBinding.ValidatePair(missingUv, ps); }
        catch (InvalidOperationException error) when (error.Message.Contains("XBOX_SHADER_INTERFACE")) {
            Console.WriteLine("Cg physical attributes and varying routes reject mismatched shader pairs.");
            return;
        }
        throw new Exception("Pixel UV accepted without a matching vertex output.");
    }
}
