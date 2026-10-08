using System.Buffers.Binary;

namespace helengine.xbox.shaderprobe;

/// <summary>Validates the initial NV2A backend subset before invoking Cg, without claiming universal fp20 support.</summary>
internal static class XboxShaderCapabilities {
    /// <summary>Rejects surviving flow control, derivatives, discard and nonordinary/multiple texture sample sites.</summary>
    public static void Validate(byte[] spirv, bool vertexStage) {
        if (spirv.Length < 20 || spirv.Length % 4 != 0 || BinaryPrimitives.ReadUInt32LittleEndian(spirv) != 0x07230203)
            throw new InvalidOperationException("Invalid SPIR-V module.");
        int samples = 0;
        for (int offset = 20; offset < spirv.Length;) {
            uint instruction = BinaryPrimitives.ReadUInt32LittleEndian(spirv.AsSpan(offset));
            int count = checked((int)(instruction >> 16));
            uint opcode = instruction & 0xffff;
            if (count == 0 || count > (spirv.Length - offset) / 4) throw new InvalidOperationException("Malformed SPIR-V instruction.");
            // SPIR-V core opcode values: LoopMerge, BranchConditional, Switch, Kill;
            // derivative family; TerminateInvocation and DemoteToHelperInvocation.
            if (opcode == 246 || opcode == 250 || opcode == 251 || opcode == 252 ||
                opcode is >= 207 and <= 215 || opcode == 4416 || opcode == 5380)
                throw new NotSupportedException("XBOX_SHADER_OPERATION: unsupported surviving operation " + (Silk.NET.SPIRV.Op)opcode);
            // OpImageSampleImplicitLod is the initial ordinary 2D sample contract.
            if (opcode == 87) {
                if (vertexStage || ++samples > 1) throw new NotSupportedException("XBOX_SHADER_OPERATION: this subset permits one ordinary pixel texture sample site.");
            } else if (opcode is >= 88 and <= 107) {
                throw new NotSupportedException("XBOX_SHADER_OPERATION: unsupported image operation " + (Silk.NET.SPIRV.Op)opcode);
            }
            offset += count * 4;
        }
    }
}
