using System.Text;
using Silk.NET.SPIRV;
using Silk.NET.SPIRV.Cross;

namespace helengine.xbox.shaderprobe;

/// <summary>Wraps unchanged compiler output with the supported authored Xbox Cg interface.</summary>
public static unsafe class XboxShaderEntryAdapter {
    /// <summary>Builds a separate entry point from reflected names and types after emission.</summary>
    public static string Create(Cross api, Silk.NET.SPIRV.Cross.Compiler* compiler, string entryPoint, bool vertexStage) {
        Resources* resources = null;
        if (api.CompilerCreateShaderResources(compiler, &resources) != Result.Success)
            throw new InvalidOperationException("Cannot reflect emitted interface.");
        StringBuilder declarations = new();
        StringBuilder inputs = new();
        StringBuilder outputs = new();
        bool hasInputs = false;
        if (vertexStage) {
            declarations.AppendLine("uniform float4 Xbox_TextureExtent : register(c93);");
            declarations.AppendLine("uniform float4 Xbox_ViewportScale : register(c94);");
            declarations.AppendLine("uniform float4 Xbox_ViewportOffset : register(c95);");
        }
        foreach (ResourceType kind in new[] { ResourceType.StageInput, ResourceType.StageOutput }) {
            bool input = kind == ResourceType.StageInput;
            ReflectedResource* entries = null;
            nuint count = 0;
            if (api.ResourcesGetResourceListForType(resources, kind, &entries, &count) != Result.Success)
                throw new InvalidOperationException("Cannot enumerate emitted interface.");
            if (input) {
                hasInputs = count != 0;
                if (!hasInputs) continue;
            } else if (!vertexStage && count == 0) {
                throw new InvalidOperationException("Xbox pixel shaders require an output colour.");
            }
            declarations.AppendLine(input ? "struct XboxInput {" : "struct XboxOutput {");
            HashSet<string> semantics = new(StringComparer.OrdinalIgnoreCase);
            for (nuint i = 0; i < count; ++i) {
                uint id = entries[i].Id;
                string name = api.CompilerGetNameS(compiler, id);
                string semantic = api.CompilerGetDecorationStringS(compiler, id, (Decoration)5635).ToUpperInvariant();
                if (vertexStage && input && semantic == "POSITION0") semantic = "POSITION";
                if (!vertexStage && !input && semantic == "SV_TARGET") semantic = "COLOR0";
                if (semantic != "COLOR0" && semantic != "TEXCOORD0" && !(vertexStage && input && semantic == "POSITION"))
                    throw new InvalidOperationException("Unsupported Xbox interface semantic: " + semantic);
                if (!semantics.Add(semantic)) throw new InvalidOperationException("Duplicate Xbox interface semantic: " + semantic);
                if (string.IsNullOrEmpty(name) || name.Any(c => !char.IsAsciiLetterOrDigit(c) && c != '_'))
                    throw new InvalidOperationException("Emitter did not expose a usable member name: " + name);
                var type = api.CompilerGetTypeHandle(compiler, entries[i].TypeId);
                uint size = api.TypeGetVectorSize(type);
                if (api.TypeGetBasetype(type) != Basetype.FP32 || api.TypeGetColumns(type) != 1 ||
                    api.TypeGetNumArrayDimensions(type) != 0 || size < 1 || size > 4)
                    throw new InvalidOperationException("Unsupported Xbox interface type for " + semantic);
                declarations.AppendLine($"    float{(size == 1 ? "" : size.ToString())} {name} : {semantic};");
                (input ? inputs : outputs).AppendLine(input
                    ? $"    coreInput.{name} = stageInput.{name};"
                    : $"    stageOutput.{name} = coreOutput.{name};");
                if (vertexStage && !input && semantic == "TEXCOORD0") {
                    if (size < 2) throw new InvalidOperationException("Xbox normalized texture coordinates require at least two components.");
                    outputs.AppendLine($"    stageOutput.{name}.xy *= Xbox_TextureExtent.xy;");
                }
            }
            // SM3's position output is a compiler-owned builtin, absent from StageOutput.
            if (vertexStage && !input) {
                declarations.AppendLine("    float4 gl_Position : POSITION;");
                outputs.AppendLine("    stageOutput.gl_Position = coreOutput.gl_Position;");
                outputs.AppendLine("    stageOutput.gl_Position.xyz = (coreOutput.gl_Position.xyz / coreOutput.gl_Position.w) * Xbox_ViewportScale.xyz + Xbox_ViewportOffset.xyz;");
            }
            declarations.AppendLine("};");
        }
        declarations.AppendLine(hasInputs ? "XboxOutput XboxMain(XboxInput stageInput) {" : "XboxOutput XboxMain() {");
        if (hasInputs) declarations.AppendLine("    SPIRV_Cross_Input coreInput;");
        declarations.Append(inputs);
        declarations.AppendLine($"    SPIRV_Cross_Output coreOutput = {entryPoint}({(hasInputs ? "coreInput" : "")});");
        declarations.AppendLine("    XboxOutput stageOutput;");
        declarations.Append(outputs);
        declarations.AppendLine("    return stageOutput;\n}");
        return declarations.ToString();
    }
}
