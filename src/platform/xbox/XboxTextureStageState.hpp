#pragma once
#include "XboxTextureUpload.hpp"

namespace helengine::xbox {
// CPU preparation only. Texture-object state remains separate from shader commands.
struct XboxTextureStageState {
    bool Enabled = false;
    unsigned Width = 1;
    unsigned Height = 1;
    std::uint32_t Offset = 0;
    std::uint32_t Pitch = 0;
    std::uint32_t ImageRect = 0;
    std::uint32_t FormatWord = 0;
    std::uint32_t PaletteWord = 0;
    float UvScaleX = 1.0f;
    float UvScaleY = 1.0f;
};

// A null layout selects an untextured draw. Failure clears previous state.
bool BuildTextureStageState(const XboxTextureLayout* layout, std::uint32_t gpuOffset,
                            XboxTextureStageState& state, std::uint32_t paletteGpuOffset = 0) noexcept;
using XboxTextureRegisterWriter = void (*)(void*, std::uint32_t, std::uint32_t);
// Validates before the first callback, disables all units, then binds stage zero.
bool BindTextureStage(const XboxTextureLayout* layout, std::uint32_t gpuOffset,
                      XboxTextureRegisterWriter writer, void* context, std::uint32_t paletteGpuOffset = 0) noexcept;
}
