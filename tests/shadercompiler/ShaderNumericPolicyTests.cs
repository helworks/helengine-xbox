using System.Reflection;
using System.Runtime.ExceptionServices;
using helengine;

/// <summary>Regresses the actual Xbox cooker's package envelope independently of expensive native shader compilation.</summary>
internal static class ShaderNumericPolicyTests {
    /// <summary>Requires requested 32/32 and 64/64 policies to survive real serialization and strict runtime validation.</summary>
    public static void Run(string builderPath) {
        Assembly assembly = Assembly.LoadFrom(builderPath);
        Type cooker = assembly.GetType("helengine.xbox.builder.XboxShaderArtifactCooker", true);
        MethodInfo serialize = cooker.GetMethod("SerializePackage", BindingFlags.NonPublic | BindingFlags.Static);
        if (serialize == null) throw new Exception("Xbox shader cooker does not expose its production package serialization boundary.");
        foreach (int bits in new[] { 32, 64 }) {
            NumericPrecisionPolicy requested = new(bits, bits);
            ShaderProgramDefinition program = new("Numeric.vs", ShaderStage.Vertex, "VS", [], [], [], [new ShaderVariant("default", [])]);
            using ShaderModuleDefinition module = new("Numeric", [program],
                [new ShaderProgramBinary("Numeric.vs", ShaderStage.Vertex, "xbox", "default", [1, 2, 3, 4])]);
            byte[] bytes;
            try { bytes = (byte[])serialize.Invoke(null, [module, requested]); }
            catch (TargetInvocationException error) { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
            using MemoryStream stream = new(bytes, false);
            EngineBinaryHeader header = EngineBinaryHeaderSerializer.Read(stream);
            if (header.Version != PackagedAssetBinarySerializer.CurrentVersion ||
                header.ValueKind != (ushort)EditorAssetBinaryValueKind.ShaderAsset) throw new Exception("Not a current shader package envelope.");
            using EngineBinaryReader reader = EngineBinaryReader.Create(stream, header.Endianness);
            RealBinaryCodec.ReadAndValidatePolicy(reader, requested);
            stream.Position = 12;
            try {
                RealBinaryCodec.ReadAndValidatePolicy(reader, new(bits == 32 ? 64 : 32, bits == 32 ? 64 : 32));
                throw new Exception("Runtime accepted a mismatched numeric shader envelope.");
            } catch (InvalidOperationException) { }
            ShaderAsset decoded = (ShaderAsset)helengine.files.AssetSerializer.DeserializeFromBytes(bytes);
            if (decoded.TargetName != "xbox" || decoded.Binaries.Length != 1 ||
                !decoded.Binaries[0].Bytecode.SequenceEqual(new byte[] { 1, 2, 3, 4 })) throw new Exception("Numeric policy changed the compiled shader payload.");
            Console.WriteLine($"Actual Xbox shader package preserves {requested.Identity}; opposite runtime policy rejects; compiled bytes unchanged.");
        }
    }
}
