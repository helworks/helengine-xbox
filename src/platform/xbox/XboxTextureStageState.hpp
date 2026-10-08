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
};

// A null layout selects an untextured draw. Failure clears previous state.
bool BuildTextureStageState(const XboxTextureLayout* layout, std::uint32_t gpuOffset,
                            XboxTextureStageState& state) noexcept;
using XboxTextureRegisterWriter = void (*)(void*, std::uint32_t, std::uint32_t);
// Validates before the first callback, disables all units, then binds stage zero.
bool BindTextureStage(const XboxTextureLayout* layout, std::uint32_t gpuOffset,
                      XboxTextureRegisterWriter writer, void* context) noexcept;
}
