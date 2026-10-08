#include "XboxShaderTextureBinding.hpp"
namespace helengine::xbox {
bool BindShaderMaterialTexture(const XboxShaderMaterial& material, const Matrix4& clip,
    unsigned width, unsigned height, const XboxTextureLayout* texture, uint32_t offset,
    XboxShaderInstructionWriter vertexWriter, XboxShaderRegisterWriter registerWriter,
    XboxShaderConstantWriter constantWriter, void* context, const char*& reason) {
    reason = "Texture does not match the compiled material or has an invalid allocation";
    XboxTextureStageState state;
    XboxShaderArtifactView pixel{};
    const auto* program = material.Program();
    if (!BuildTextureStageState(texture, offset, state) || program == nullptr ||
        !program->ReadPixelArtifact(pixel, reason)) return false;
    bool requiresTexture = false;
    for (uint32_t i = 0; i < pixel.ResourceBindingCount; ++i)
        if (ReadShaderWord(pixel.ResourceBindings + i * 80) == 1) requiresTexture = true;
    if (requiresTexture != state.Enabled) {
        reason = "Texture presence does not match the compiled material resource map";
        return false;
    }
    // Copy the descriptor before callbacks; command emission must use the validated snapshot.
    const XboxTextureLayout snapshot = texture == nullptr ? XboxTextureLayout{} : *texture;
    if (!material.Bind(clip, width, height, state.Width, state.Height,
        vertexWriter, registerWriter, constantWriter, context, reason)) return false;
    return BindTextureStage(state.Enabled ? &snapshot : nullptr, offset, registerWriter, context);
}
}
