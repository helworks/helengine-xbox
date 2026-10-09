#include "XboxTextureStageState.hpp"
#include "XboxNativeTextureLayout.hpp"
#include <pbkit/nv_regs.h>

namespace helengine::xbox {
bool BindTextureStage(const XboxTextureLayout* layout, std::uint32_t gpuOffset,
                      XboxTextureRegisterWriter writer, void* context, std::uint32_t paletteGpuOffset) noexcept {
    XboxTextureStageState state;
    if (writer == nullptr || !BuildTextureStageState(layout, gpuOffset, state, paletteGpuOffset)) return false;
    for (unsigned stage = 0; stage < 4; ++stage) {
        writer(context, NV097_SET_TEXTURE_CONTROL0 + stage * 64, 0);
        writer(context, NV097_SET_TEXTURE_PALETTE + stage * 64, 0);
    }
    if (!state.Enabled) return true;
    writer(context, NV097_SET_TEXTURE_OFFSET, state.Offset);
    writer(context, NV097_SET_TEXTURE_FORMAT, state.FormatWord);
    if (state.PaletteWord) writer(context,NV097_SET_TEXTURE_PALETTE,state.PaletteWord);
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
                            XboxTextureStageState& state, std::uint32_t paletteGpuOffset) noexcept {
    state = {};
    if (layout == nullptr) return gpuOffset == 0 && paletteGpuOffset == 0;
    constexpr std::uint32_t highestAddress = 0x03FFAFFF;
    if (gpuOffset == 0 || gpuOffset > highestAddress) return false;
    if (!ValidateXboxTextureLayout(*layout) || layout->ByteCount > highestAddress - gpuOffset + 1u) return false;
    std::uint32_t paletteWord = 0;
    if (layout->PaletteEntries) {
        if (!paletteGpuOffset || paletteGpuOffset % 64 || paletteGpuOffset > highestAddress ||
            layout->PaletteEntries * 4 > highestAddress - paletteGpuOffset + 1u) return false;
        const unsigned lengthCode = layout->PaletteEntries == 256 ? 0 : layout->PaletteEntries == 128 ? 1
            : layout->PaletteEntries == 64 ? 2 : 3;
        paletteWord = paletteGpuOffset | (lengthCode << 2) | 1u; // DMA B, matching texture DMA B.
    } else if (paletteGpuOffset != 0) return false;
    const auto pitch = GetXboxTextureRowPitchBytes(*layout);
    state.Enabled = true;
    state.Width = layout->RealWidth;
    state.Height = layout->RealHeight;
    state.Offset = gpuOffset;
    state.Pitch = pitch;
    state.ImageRect = (layout->StorageWidth << 16) | layout->StorageHeight;
    state.FormatWord = GetXboxTextureFormatWord(*layout);
    state.PaletteWord = paletteWord;
    state.UvScaleX = GetXboxTextureUvScaleX(*layout);
    state.UvScaleY = GetXboxTextureUvScaleY(*layout);
    return true;
}
}
