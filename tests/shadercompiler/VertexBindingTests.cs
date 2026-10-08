using helengine.xbox.shaderprobe;

/// <summary>Checks physical constant discovery independently of assumed fixture register zero.</summary>
internal static class VertexBindingTests {
    /// <summary>Requires actual Cg row assignments, reserved ownership and fail-closed malformed metadata.</summary>
    public static void Run() {
        const string source = "#var float4 Transform[0] :  : c[12] : -1 : 1\n"
            + "#var float4 Transform[1] :  : c[13] : -1 : 1\n"
            + "#var float4 gl_HalfPixel :  : c[2] : -1 : 1\n"
            + "#var float4 Xbox_ViewportScale : C94 : c[94] : -1 : 1\n"
            + "#var float4 Xbox_ViewportOffset : C95 : c[95] : -1 : 1\n";
        var bindings = XboxVertexConstantBinding.Read(source);
        if (bindings.Count != 5 || !bindings.Any(item => item.Name == "Transform" && item.Row == 1 && item.Register == 13))
            throw new Exception("Shifted matrix register assignment was lost.");
        Reject(source.Replace("c[13]", "c[12]"));
        Reject(source.Replace("c[13]", "c[93]"));
        Reject(source.Replace("c[13]", "c[96]"));
        Reject(source.Replace("Transform[1]", "Transform[0]"));
        Reject(source.Replace("c[94]", "c[90]"));
        Reject(source.Replace("c[13]", "c[bad]"));
        Reject("#var float4 Matrix : : c[0] : -1 : 1\n");
        Console.WriteLine("Physical vertex constant assignment and collision contracts passed.");
    }

    /// <summary>Requires invalid compiler binding metadata to fail before native publication.</summary>
    static void Reject(string source) {
        try {
            XboxVertexConstantBinding.Read(source);
        } catch (InvalidOperationException error) when (error.Message.Contains("XBOX_SHADER_BINDING")) { return; }
        throw new Exception("Invalid physical constant metadata was accepted.");
    }
}
