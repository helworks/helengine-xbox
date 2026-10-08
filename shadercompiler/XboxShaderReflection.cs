using Silk.NET.SPIRV;
using Silk.NET.SPIRV.Cross;

namespace helengine.xbox.shaderprobe;

/// <summary>Copies active typed interfaces and resource layout before flattening or renaming by the emitter.</summary>
internal static unsafe class XboxShaderReflection {
    /// <summary>Reflects the selected entry only and rejects unsupported storage resources.</summary>
    public static XboxShaderReflectionItem[] Read(Cross api, Silk.NET.SPIRV.Cross.Compiler* compiler) {
        Set* active = null;
        Resources* resources = null;
        Check(api.CompilerGetActiveInterfaceVariables(compiler, &active));
        Check(api.CompilerCreateShaderResourcesForActiveVariables(compiler, &resources, active));
        List<XboxShaderReflectionItem> result = [];
        foreach (ResourceType kind in new[] { ResourceType.StageInput, ResourceType.StageOutput, ResourceType.UniformBuffer,
            ResourceType.SeparateImage, ResourceType.SeparateSamplers, ResourceType.StorageBuffer, ResourceType.StorageImage }) {
            ReflectedResource* entries = null;
            nuint count = 0;
            Check(api.ResourcesGetResourceListForType(resources, kind, &entries, &count));
            if (count > 0 && (kind == ResourceType.StorageBuffer || kind == ResourceType.StorageImage))
                throw new NotSupportedException("XBOX_SHADER_RESOURCE: storage resources are unsupported.");
            for (nuint i = 0; i < count; ++i) {
                uint id = entries[i].Id;
                CrossType* type = api.CompilerGetTypeHandle(compiler, entries[i].BaseTypeId);
                string name = api.CompilerGetNameS(compiler, id);
                if (string.IsNullOrEmpty(name)) name = api.CompilerGetNameS(compiler, entries[i].BaseTypeId);
                string format = kind.ToString();
                nuint size = 0;
                List<XboxShaderReflectionItem> members = [];
                if (kind == ResourceType.UniformBuffer) {
                    Check(api.CompilerGetDeclaredStructSize(compiler, type, &size));
                    for (uint member = 0; member < api.TypeGetNumMemberTypes(type); ++member) {
                        uint offset = 0;
                        nuint memberSize = 0;
                        Check(api.CompilerTypeStructMemberOffset(compiler, type, member, &offset));
                        Check(api.CompilerGetDeclaredStructMemberSize(compiler, type, member, &memberSize));
                        members.Add(new XboxShaderReflectionItem { Kind = "Member", Name = api.CompilerGetMemberNameS(compiler, entries[i].BaseTypeId, member),
                            Format = NumericType(api, api.CompilerGetTypeHandle(compiler, api.TypeGetMemberType(type, member))),
                            Offset = checked((int)offset), Size = checked((int)memberSize) });
                    }
                } else if (kind == ResourceType.StageInput || kind == ResourceType.StageOutput) {
                    format = NumericType(api, type);
                } else if (kind == ResourceType.SeparateImage) {
                    if ((uint)api.TypeGetImageDimension(type) != 1 || api.TypeGetImageArrayed(type) != 0 || api.TypeGetImageMultisampled(type) != 0)
                        throw new NotSupportedException("XBOX_SHADER_RESOURCE: only ordinary 2D textures are supported.");
                }
                result.Add(new XboxShaderReflectionItem { Kind = kind.ToString(), Name = name,
                    Semantic = api.CompilerGetDecorationStringS(compiler, id, (Decoration)5635), Format = format,
                    Set = checked((int)api.CompilerGetDecoration(compiler, id, Decoration.DescriptorSet)),
                    Binding = checked((int)api.CompilerGetDecoration(compiler, id, Decoration.Binding)),
                    Size = checked((int)size), Members = members.ToArray() });
            }
        }
        api.CompilerUpdateActiveBuiltins(compiler);
        if (api.CompilerHasActiveBuiltin(compiler, BuiltIn.Position, StorageClass.Output) != 0)
            result.Add(new XboxShaderReflectionItem { Kind = "StageOutput", Name = "gl_Position", Semantic = "SV_POSITION", Format = "float4" });
        return result.ToArray();
    }
    /// <summary>Formats supported float scalar/vector/matrix layout without approximating unsupported aggregate types.</summary>
    static string NumericType(Cross api, CrossType* type) {
        if (api.TypeGetBasetype(type) != Basetype.FP32 || api.TypeGetNumArrayDimensions(type) != 0)
            throw new NotSupportedException("XBOX_SHADER_TYPE: only float scalars, vectors and matrices are supported.");
        uint rows = api.TypeGetVectorSize(type), columns = api.TypeGetColumns(type);
        return columns > 1 ? $"float{rows}x{columns}" : rows > 1 ? $"float{rows}" : "float";
    }
    /// <summary>Stops reflection immediately when the native API fails.</summary>
    static void Check(Result result) {
        if (result != Result.Success) throw new InvalidOperationException("Xbox native reflection failed: " + result);
    }
}
