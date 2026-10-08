#include "XboxTextureStageState.hpp"
#include <pbkit/nv_regs.h>

namespace helengine::xbox {
bool BindTextureStage(const XboxTextureLayout* layout, std::uint32_t gpuOffset,
                      XboxTextureRegisterWriter writer, void* context) noexcept {
    XboxTextureStageState state;
    if (writer == nullptr || !BuildTextureStageState(layout, gpuOffset, state)) return false;
    for (unsigned stage = 0; stage < 4; ++stage)
        writer(context, NV097_SET_TEXTURE_CONTROL0 + stage * 64, 0);
    if (!state.Enabled) return true;
    writer(context, NV097_SET_TEXTURE_OFFSET, state.Offset);
    writer(context, NV097_SET_TEXTURE_FORMAT, 0x00011222);
    writer(context, NV097_SET_TEXTURE_ADDRESS, 0x00030303);
    // The mesh sample's 0x04074000 selects a Gaussian convolution kernel.
    // Coverage atlases need ordinary bilinear sampling; convolution smears
    // glyph edges across neighbouring texels even at their authored size.
    writer(context, NV097_SET_TEXTURE_FILTER, 0x02020000);
    writer(context, NV097_SET_TEXTURE_CONTROL1, state.Pitch << 16);
    writer(context, NV097_SET_TEXTURE_IMAGE_RECT, state.ImageRect);
    writer(context, NV097_SET_TEXTURE_CONTROL0, 0x4003ffc0);
    return true;
}
bool BuildTextureStageState(const XboxTextureLayout* layout, std::uint32_t gpuOffset,
                            XboxTextureStageState& state) noexcept {
    state = {};
    if (layout == nullptr) return gpuOffset == 0;
    constexpr std::uint32_t highestAddress = 0x03FFAFFF;
    if (gpuOffset == 0 || gpuOffset > highestAddress) return false;
    XboxTextureLayout expected{};
    if (BuildXboxTextureLayout(layout->RealWidth, layout->RealHeight, expected) != XboxTextureStatus::Ok ||
        layout->StorageWidth != expected.StorageWidth || layout->StorageHeight != expected.StorageHeight ||
        layout->BytesPerTexel != expected.BytesPerTexel || layout->HardwareFormat != expected.HardwareFormat ||
        layout->ByteCount != expected.ByteCount || layout->ByteCount > highestAddress - gpuOffset + 1u) return false;
    const auto pitch = GetXboxTextureRowPitchBytes(*layout);
    if (pitch == 0) return false;
    state.Enabled = true;
    state.Width = layout->RealWidth;
    state.Height = layout->RealHeight;
    state.Offset = gpuOffset;
    state.Pitch = pitch;
    state.ImageRect = (layout->RealWidth << 16) | layout->RealHeight;
    return true;
}
}
