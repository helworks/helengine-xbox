using helengine.xbox.shaderprobe;

/// <summary>Checks compiler-assigned global factors without assuming metadata line order.</summary>
internal static class PixelBindingTests {
    /// <summary>Rejects ambiguous or unsupported compiler bindings before publication.</summary>
    public static void Run() {
        const string first = "//var float4 Tint[0] :  : COMBINER_CONST1 : -1 : 1\n";
        const string second = "//var float4 Other[1] :  : COMBINER_CONST0 : -1 : 1\n";
        var bindings = XboxPixelConstantBinding.Read(first + second);
        if (bindings.Count != 2 || bindings[0].Name != "Other" || bindings[0].Row != 1 || bindings[0].Factor != 0 ||
            bindings[1].Name != "Tint" || bindings[1].Row != 0 || bindings[1].Factor != 1)
            throw new Exception("Global pixel factors lost their physical assignment.");
        Reject(first + second.Replace("COMBINER_CONST0", "COMBINER_CONST1"));
        Reject(first + second.Replace("Other[1]", "Tint[0]"));
        Reject(first.Replace("COMBINER_CONST1", "COMBINER_CONST2"));
        Reject(first.Replace("COMBINER_CONST1", "COMBINER_STAGE0_CONST1"));
        Reject(first.Replace("Tint[0]", "Tint[96]"));
        Reject(first.Replace("Tint[0]", "Tint[99999999999999999999]"));
        Reject(first.Replace("Tint[0]", new string('A', 64)));
        Reject(first.Replace("float4", "float3"));
        Reject("//var float4 Tint : : COMBINER_CONST0\n");
        if (XboxPixelConstantBinding.Read("//var float4 output : $vout.COLOR0 : COLOR : -1 : 1\n").Count != 0)
            throw new Exception("Pixel output was confused with a uniform factor.");
        Console.WriteLine("Physical pixel factors, collisions and malformed metadata contracts passed.");
    }

    /// <summary>Requires a named binding diagnostic instead of silently dropping invalid uniforms.</summary>
    static void Reject(string source) {
        try { XboxPixelConstantBinding.Read(source); }
        catch (InvalidOperationException error) when (error.Message.Contains("XBOX_PIXEL_BINDING")) { return; }
        throw new Exception("Invalid pixel uniform assignment was accepted.");
    }
}
