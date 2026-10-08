#include "platform/xbox/XboxShaderRuntimeMaterial.hpp"
#include "ShaderMaterialAsset.hpp"
#include "ShaderAsset.hpp"
#include "ShaderProgramAsset.hpp"
#include "ShaderBinaryAsset.hpp"
#include "ShaderBindingAsset.hpp"
#include "ShaderConstantMemberAsset.hpp"
#include "MaterialConstantBufferAsset.hpp"
#include "MaterialRenderState.hpp"
#include "MaterialPropertyBlock.hpp"
#include <pbkit/nv_regs.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
using namespace helengine::xbox;
static Array<uint8_t>* ReadBinary(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    auto* result = new Array<uint8_t>(bytes.size());
    std::memcpy(result->Data, bytes.data(), bytes.size());
    return result;
}
static ShaderBindingAsset* Buffer(const char* name, const char* type, int size) {
    auto* binding = new ShaderBindingAsset();
    binding->Name = name;
    binding->Type = ShaderResourceType::ConstantBuffer;
    binding->Size = size;
    delete binding->Members;
    binding->Members = new Array<ShaderConstantMemberAsset*>(1);
    auto* member = new ShaderConstantMemberAsset();
    member->Name = "Value"; member->Type = type; member->Size = size; member->Offset = 0;
    (*binding->Members)[0] = member;
    return binding;
}
struct Draw { unsigned Writes = 0; uint32_t General = 0, Final = 0; };
static void Vertex(void* context, unsigned, const uint32_t*) { ++static_cast<Draw*>(context)->Writes; }
static void Pixel(void* context, uint32_t method, uint32_t value) {
    auto& draw = *static_cast<Draw*>(context);
    ++draw.Writes;
    if (method == NV097_SET_COMBINER_FACTOR0) draw.General = value;
    if (method == NV097_SET_SPECULAR_FOG_FACTOR) draw.Final = value;
}
static void Constants(void* context, const float (*)[4]) { ++static_cast<Draw*>(context)->Writes; }
int main(int argc, char** argv) {
    assert(argc == 3 || argc == 5);
    ShaderAsset shader;
    shader.TargetName = "xbox";
    delete shader.Programs; delete shader.Binaries;
    shader.Programs = new Array<ShaderProgramAsset*>(2);
    shader.Binaries = new Array<ShaderBinaryAsset*>(2);
    for (int i = 0; i < 2; ++i) {
        auto* program = new ShaderProgramAsset();
        program->Name = i == 0 ? "uniform.vs" : "uniform.ps";
        program->Stage = i == 0 ? ShaderStage::Vertex : ShaderStage::Pixel;
        delete program->Bindings;
        program->Bindings = new Array<ShaderBindingAsset*>(1);
        (*program->Bindings)[0] = i == 0 ? Buffer("TransformBuffer", "float4x4", 64) : Buffer("BaseColorBuffer", "float4", 16);
        (*shader.Programs)[i] = program;
        auto* binary = new ShaderBinaryAsset();
        binary->ProgramName = program->Name; binary->Stage = program->Stage;
        binary->TargetName = "xbox"; binary->Variant = "default";
        delete binary->Bytecode;
        binary->Bytecode = ReadBinary(argv[i + 1]);
        (*shader.Binaries)[i] = binary;
    }
    ShaderMaterialAsset authored;
    authored.ShaderAssetId = "uniform";
    authored.VertexProgram = "uniform.vs"; authored.PixelProgram = "uniform.ps"; authored.Variant = "default";
    delete authored.ConstantBuffers;
    authored.ConstantBuffers = new Array<MaterialConstantBufferAsset*>(2);
    auto* colour = new MaterialConstantBufferAsset();
    colour->Name = "BaseColorBuffer";
    delete colour->Data;
    colour->Data = new Array<uint8_t>(16);
    const float cyan[] = {.2f,.8f,1,1};
    std::memcpy(colour->Data->Data, cyan, 16);
    (*authored.ConstantBuffers)[0] = colour;
    // The real standard-schema material retains defaults unused by custom shaders.
    auto* roughness = new MaterialConstantBufferAsset();
    roughness->Name = "RoughnessBuffer";
    delete roughness->Data;
    roughness->Data = new Array<uint8_t>(16);
    (*authored.ConstantBuffers)[1] = roughness;
    const char* reason = nullptr;
    std::unique_ptr<XboxShaderRuntimeMaterial> runtime(XboxShaderRuntimeMaterial::Create(authored, shader, reason));
    if (!runtime) std::fprintf(stderr, "Create: %s\n", reason);
    assert(runtime);
    std::unique_ptr<XboxShaderRuntimeMaterial> sibling(XboxShaderRuntimeMaterial::Create(authored, shader, reason));
    assert(sibling);
    assert(authored.ConstantBuffers->Length == 2 && (*authored.ConstantBuffers)[1] == roughness);
    assert(runtime->Prepare(reason));
    Matrix4 identity{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    Draw first;
    assert(runtime->NativeMaterial().Bind(identity, 640, 480, 1, 1, Vertex, Pixel, Constants, &first, reason));
    assert(first.General == 0xff33ccff && first.Final == 0xff33ccff);
    const float red[] = {1,0,0,.5f};
    std::memcpy(colour->Data->Data, red, 16);
    runtime->get_Properties()->SetConstantBufferData("BaseColorBuffer", colour->Data);
    assert(runtime->Prepare(reason));
    Draw second;
    assert(runtime->NativeMaterial().Bind(identity, 640, 480, 1, 1, Vertex, Pixel, Constants, &second, reason));
    assert(second.General == 0x7fff0000 && second.Final == 0x7fff0000);
    const float invalid[] = {2,0,0,1};
    std::memcpy(colour->Data->Data, invalid, 16);
    runtime->get_Properties()->SetConstantBufferData("BaseColorBuffer", colour->Data);
    assert(!runtime->Prepare(reason));
    Draw rejected;
    assert(!runtime->NativeMaterial().Bind(identity, 640, 480, 1, 1, Vertex, Pixel, Constants, &rejected, reason));
    assert(rejected.Writes == 0);
    auto* pixelBinding = (*(*shader.Programs)[1]->Bindings)[0];
    pixelBinding->Name = "WrongBuffer";
    assert(XboxShaderRuntimeMaterial::Create(authored, shader, reason) == nullptr);
    pixelBinding->Name = "BaseColorBuffer";
    pixelBinding->Size = 32;
    assert(XboxShaderRuntimeMaterial::Create(authored, shader, reason) == nullptr);
    pixelBinding->Size = 16;
    auto* validColourData = colour->Data;
    colour->Data = new Array<uint8_t>(8);
    assert(XboxShaderRuntimeMaterial::Create(authored, shader, reason) == nullptr);
    assert(reason != nullptr);
    delete colour->Data; colour->Data = validColourData;
    colour->Data = nullptr;
    assert(XboxShaderRuntimeMaterial::Create(authored, shader, reason) == nullptr);
    colour->Data = validColourData;
    (*authored.ConstantBuffers)[1] = colour;
    assert(XboxShaderRuntimeMaterial::Create(authored, shader, reason) == nullptr);
    (*authored.ConstantBuffers)[1] = roughness;
    runtime->Dispose();
    assert(!runtime->Prepare(reason));
    runtime.reset();
    assert(sibling->Prepare(reason));
    Draw surviving;
    assert(sibling->NativeMaterial().Bind(identity, 640, 480, 1, 1, Vertex, Pixel, Constants, &surviving, reason));
    assert(surviving.General == 0xff33ccff && surviving.Final == 0xff33ccff);
    auto invalidatedProgram = sibling->SharedProgram();
    assert(!invalidatedProgram->Load(nullptr, 0, nullptr, 0, reason));
    assert(!sibling->Prepare(reason));
    Draw invalidated;
    assert(!sibling->NativeMaterial().Bind(identity, 640, 480, 1, 1, Vertex, Pixel, Constants, &invalidated, reason));
    assert(invalidated.Writes == 0);
    sibling.reset();
    if (argc == 5) {
        for (int i = 0; i < 2; ++i) {
            auto* binary = (*shader.Binaries)[i];
            delete binary->Bytecode;
            binary->Bytecode = ReadBinary(argv[i + 3]);
        }
        (*(*shader.Programs)[0]->Bindings)[0]->Name = "Transform";
        auto* pixel = (*shader.Programs)[1];
        for (int i = 0; i < pixel->Bindings->Length; ++i) { (*pixel->Bindings)[i]->Dispose(); delete (*pixel->Bindings)[i]; }
        delete pixel->Bindings;
        pixel->Bindings = new Array<ShaderBindingAsset*>(2);
        auto* image = new ShaderBindingAsset(); image->Type = ShaderResourceType::Texture2D; image->Name = "Albedo";
        auto* sampler = new ShaderBindingAsset(); sampler->Type = ShaderResourceType::Sampler; sampler->Name = "AlbedoSampler";
        (*pixel->Bindings)[0] = image; (*pixel->Bindings)[1] = sampler;
        colour->Dispose(); delete colour;
        roughness->Dispose(); delete roughness;
        delete authored.ConstantBuffers; authored.ConstantBuffers = new Array<MaterialConstantBufferAsset*>(0);
        runtime.reset(XboxShaderRuntimeMaterial::Create(authored, shader, reason));
        assert(runtime && runtime->RequiresTexture() && runtime->Prepare(reason));
        runtime.reset();
        image->Name = "WrongAlbedo";
        std::unique_ptr<XboxShaderRuntimeMaterial> wrongImage(XboxShaderRuntimeMaterial::Create(authored, shader, reason));
        assert(!wrongImage);
        image->Name = "Albedo"; sampler->Name = "WrongSampler";
        std::unique_ptr<XboxShaderRuntimeMaterial> wrongSampler(XboxShaderRuntimeMaterial::Create(authored, shader, reason));
        assert(!wrongSampler);
        std::puts("Generated material rejects texture/sampler metadata that disagrees with compiled physical bindings.");
    }
    authored.Dispose(); shader.Dispose();
    std::puts("Generated material defaults and changed pixel properties reached native combiner writes.");
}
