#pragma once
#include "XboxShaderMaterial.hpp"
#include "XboxTextureStageState.hpp"
namespace helengine::xbox {
// Validates texture requirements and all material values before any GPU callback.
bool BindShaderMaterialTexture(const XboxShaderMaterial& material, const Matrix4& clip,
    unsigned width, unsigned height, const XboxTextureLayout* texture, uint32_t offset,
    XboxShaderInstructionWriter vertexWriter, XboxShaderRegisterWriter registerWriter,
    XboxShaderConstantWriter constantWriter, void* context, const char*& reason);
}
