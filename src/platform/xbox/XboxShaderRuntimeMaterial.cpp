#include "XboxShaderRuntimeMaterial.hpp"
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
#include "ShaderMaterialAsset.hpp"
#include "ShaderAsset.hpp"
#include "ShaderBinaryAsset.hpp"
#include "ShaderProgramAsset.hpp"
#include "ShaderBindingAsset.hpp"
#include "ShaderConstantMemberAsset.hpp"
#include "MaterialLayoutBuilder.hpp"
#include "MaterialLayout.hpp"
#include "MaterialLayoutBinding.hpp"
#include "MaterialPropertyBlock.hpp"
#include "MaterialConstantBufferAsset.hpp"
#include "MaterialRenderState.hpp"
#include <cstring>
namespace helengine::xbox {
namespace {
::ShaderBinaryAsset* SelectBinary(const ::ShaderAsset& shader, const HeCppString& name, ::ShaderStage stage, const HeCppString& variant) {
    if (shader.Binaries == nullptr) return nullptr;
    ::ShaderBinaryAsset* selected = nullptr;
    for (int i = 0; i < shader.Binaries->Length; ++i) {
        auto* binary = (*shader.Binaries)[i];
        if (binary == nullptr || binary->ProgramName != name || binary->Stage != stage || binary->Variant != variant) continue;
        if (selected != nullptr || binary->TargetName != "xbox" || binary->Bytecode == nullptr || binary->Bytecode->Length <= 0) return nullptr;
        selected = binary;
    }
    return selected;
}
::ShaderProgramAsset* SelectProgram(const ::ShaderAsset& shader, const HeCppString& name, ::ShaderStage stage) {
    if (shader.Programs == nullptr) return nullptr;
    ::ShaderProgramAsset* selected = nullptr;
    for (int i = 0; i < shader.Programs->Length; ++i) {
        auto* program = (*shader.Programs)[i];
        if (program == nullptr || program->Name != name || program->Stage != stage) continue;
        if (selected != nullptr || program->Bindings == nullptr) return nullptr;
        selected = program;
    }
    return selected;
}
}
XboxShaderRuntimeMaterial* XboxShaderRuntimeMaterial::Create(const ::ShaderMaterialAsset& material, const ::ShaderAsset& shader, const char*& reason) {
    reason = "Shader material target, selection or render state is unsupported";
    if (shader.TargetName != "xbox" || material.RenderState == nullptr || material.ConstantBuffers == nullptr ||
        material.RenderState->BlendMode != ::MaterialBlendMode::Opaque || material.RenderState->CullMode != ::MaterialCullMode::Back ||
        !material.RenderState->DepthTestEnabled || !material.RenderState->DepthWriteEnabled ||
        !material.NormalTextureAssetId.empty() || !material.EmissiveTextureAssetId.empty() || !material.RoughnessTextureAssetId.empty()) return nullptr;
    auto* vs = SelectBinary(shader, material.VertexProgram, ::ShaderStage::Vertex, material.Variant);
    auto* ps = SelectBinary(shader, material.PixelProgram, ::ShaderStage::Pixel, material.Variant);
    auto* vertex = SelectProgram(shader, material.VertexProgram, ::ShaderStage::Vertex);
    auto* pixel = SelectProgram(shader, material.PixelProgram, ::ShaderStage::Pixel);
    if (vs == nullptr || ps == nullptr || vertex == nullptr || pixel == nullptr) return nullptr;
    std::shared_ptr<XboxShaderProgram> program = std::make_shared<XboxShaderProgram>();
    if (!program->Load(vs->Bytecode->Data, vs->Bytecode->Length, ps->Bytecode->Data, ps->Bytecode->Length, reason)) return nullptr;
    std::string matrix;
    std::vector<std::string> buffers;
    for (int i = 0; i < vertex->Bindings->Length; ++i) {
        auto* binding = (*vertex->Bindings)[i];
        reason = "Unsupported vertex material binding";
        if (binding == nullptr || binding->Type != ::ShaderResourceType::ConstantBuffer || binding->Size <= 0 || binding->Size % 16 != 0) return nullptr;
        if (binding->Size == 64 && binding->Members != nullptr && binding->Members->Length == 1 && (*binding->Members)[0] != nullptr &&
            (*binding->Members)[0]->Type == "float4x4" && (*binding->Members)[0]->Offset == 0) {
            if (!matrix.empty()) { reason = "Multiple engine transform buffers are unsupported"; return nullptr; }
            matrix = binding->Name.raw();
        } else {
            buffers.push_back(binding->Name.raw());
        }
    }
    XboxShaderArtifactView pixelArtifact{};
    if (!program->ReadPixelArtifact(pixelArtifact, reason)) return nullptr;
    std::vector<std::string> pixelBuffers;
    std::string textureName, samplerName;
    for (uint32_t i = 0; i < pixelArtifact.ResourceBindingCount; ++i) {
        const uint8_t* record = pixelArtifact.ResourceBindings + i * 80;
        if (ReadShaderWord(record) == 1) textureName = reinterpret_cast<const char*>(record + 4);
        if (ReadShaderWord(record) == 2) samplerName = reinterpret_cast<const char*>(record + 4);
    }
    bool matchedTexture = false, matchedSampler = false;
    for (int i = 0; i < pixel->Bindings->Length; ++i) {
        auto* binding = (*pixel->Bindings)[i];
        reason = "Unsupported fragment material binding";
        if (binding == nullptr) return nullptr;
        if (binding->Type == ::ShaderResourceType::ConstantBuffer) {
            // Initial colour contract: one unsigned float4 per material buffer.
            if (binding->Size != 16 || binding->Members == nullptr || binding->Members->Length != 1 ||
                (*binding->Members)[0] == nullptr || (*binding->Members)[0]->Type != "float4" ||
                (*binding->Members)[0]->Offset != 0 || (*binding->Members)[0]->Size != 16) return nullptr;
            unsigned matches = 0;
            for (uint32_t row = 0; row < pixelArtifact.ConstantBindingCount; ++row) {
                const uint8_t* record = pixelArtifact.ConstantBindings + row * 72;
                if (binding->Name == reinterpret_cast<const char*>(record) && ReadShaderWord(record + 64) == 0) ++matches;
            }
            if (matches != 1) { reason = "Fragment uniform metadata disagrees with compiler bindings"; return nullptr; }
            for (const std::string& previous : pixelBuffers) if (previous == binding->Name.raw()) return nullptr;
            pixelBuffers.push_back(binding->Name.raw());
        } else if (binding->Type == ::ShaderResourceType::Texture2D) {
            reason = "Texture metadata disagrees with compiler-assigned resource binding";
            if (matchedTexture || textureName.empty() || binding->Name.raw() != textureName) return nullptr;
            matchedTexture = true;
        } else if (binding->Type == ::ShaderResourceType::Sampler) {
            reason = "Sampler metadata disagrees with compiler-assigned resource binding";
            if (matchedSampler || samplerName.empty() || binding->Name.raw() != samplerName) return nullptr;
            matchedSampler = true;
        } else {
            return nullptr;
        }
    }
    if (pixelBuffers.size() != pixelArtifact.ConstantBindingCount) {
        reason = "Fragment compiler binding is absent from package metadata";
        return nullptr;
    }
    if (matchedTexture != !textureName.empty() || matchedSampler != !samplerName.empty()) {
        reason = "Compiler resource binding is absent from package metadata";
        return nullptr;
    }
    std::unique_ptr<XboxShaderRuntimeMaterial> result(new XboxShaderRuntimeMaterial());
    if (!result->Native.Initialize(program, matrix)) { reason = "Native matrix binding disagrees with package metadata"; return nullptr; }
    result->Program = std::move(program);
    result->MatrixBuffer = matrix;
    result->VertexBuffers = std::move(buffers);
    result->PixelBuffers = std::move(pixelBuffers);
    result->Textured = matchedTexture;
    result->set_Id(material.Id);
    result->SetLayout(::MaterialLayoutBuilder::Build(const_cast<::ShaderMaterialAsset*>(&material), const_cast<::ShaderAsset*>(&shader)));
    result->SetRenderState(material.RenderState->Clone());
    // Standard-schema assets retain defaults for other shader variants (e.g.
    // RoughnessBuffer). Only the selected pair's layout owns runtime slots.
    auto* layout = result->get_Layout();
    auto* layoutBuffers = layout->get_ConstantBufferBindings();
    std::vector<bool> applied(static_cast<size_t>(layoutBuffers->Length), false);
    for (int i = 0; i < material.ConstantBuffers->Length; ++i) {
        auto* value = (*material.ConstantBuffers)[i];
        reason = "Invalid authored constant-buffer default";
        if (value == nullptr || value->Name.empty()) return nullptr;
        const int slot = layout->FindConstantBufferBindingIndex(value->Name);
        if (slot < 0) continue;
        auto* binding = (*layoutBuffers)[slot];
        if (applied[slot] || value->Data == nullptr || value->Data->Length != binding->Size) return nullptr;
        result->get_Properties()->SetConstantBufferData(slot, value->Data);
        applied[slot] = true;
    }
    result->set_LightingModel(::RuntimeMaterialLightingModel::Unlit);
    result->set_CastsShadows(false);
    result->set_ReceivesShadows(false);
    reason = nullptr;
    return result.release();
}
bool XboxShaderRuntimeMaterial::Prepare(const char*& reason) {
    reason = "Shader material is disposed, invalidated or missing a constant buffer";
    if (Disposed || !Native.Initialize(Program, MatrixBuffer)) return false;
    for (unsigned stage = 0; stage < 2; ++stage) {
        const auto& buffers = stage == 0 ? VertexBuffers : PixelBuffers;
        for (const std::string& name : buffers) {
            Array<uint8_t>* data = nullptr;
            const bool resolved = TryResolveConstantBufferData__out1(name, data);
            // The generated property API returns an owned copy, including parent resolution.
            std::unique_ptr<Array<uint8_t>> owned(data);
            if (!resolved || data == nullptr || data->Length <= 0 || data->Length % 16 != 0) return false;
            std::vector<float> values(static_cast<size_t>(data->Length) / sizeof(float));
            std::memcpy(values.data(), data->Data, data->Length);
            if (!(stage == 0 ? Native.SetVertexConstant(name, values.data(), values.size()) :
                              Native.SetPixelConstant(name, values.data(), values.size()))) return false;
        }
    }
    reason = nullptr;
    return true;
}
XboxShaderRuntimeMaterial::~XboxShaderRuntimeMaterial() { Dispose(); }
void XboxShaderRuntimeMaterial::Dispose() {
    if (Disposed) return;
    Disposed = true;
    Native.Initialize(nullptr, "");
    Program.reset();
    ::ShaderRuntimeMaterial::Dispose();
}
}
#endif
