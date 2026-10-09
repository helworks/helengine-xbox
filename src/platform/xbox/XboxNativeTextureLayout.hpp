#pragma once
#include "XboxTextureUpload.hpp"

namespace helengine::xbox {
// Storage traits of the 42 hardware formats declared by the pinned nxdk nv_regs.h.
struct XboxNativeFormatInfo {
    std::uint32_t HardwareCode = 0;
    std::uint32_t BytesPerTexel = 0;
    std::uint32_t BlockBytes = 0;
    bool Linear = false;
    bool Paletted = false;
};

constexpr unsigned XboxNativeFormatCount = 42;
// Unknown codes are rejected, including holes in the NV2A hardware enum.
bool TryGetXboxNativeFormatInfo(std::uint32_t hardwareCode, XboxNativeFormatInfo& info) noexcept;
// Requires exact native storage dimensions, byte-aligned linear pitches and bounded P8 palette sizes.
XboxTextureStatus BuildXboxNativeTextureLayout(std::uint32_t width, std::uint32_t height,
    std::uint32_t storageWidth, std::uint32_t storageHeight, std::uint32_t pitchBytes,
    std::uint32_t hardwareCode, std::uint32_t paletteEntries, XboxTextureLayout& layout) noexcept;
// Recomputes the descriptor before GPU callbacks so forged layouts cannot bypass the payload parser.
bool ValidateXboxTextureLayout(const XboxTextureLayout& layout) noexcept;
// Builds DMA B, 2D, one mip level, native color code and POT exponents. Native borders use border color.
std::uint32_t GetXboxTextureFormatWord(const XboxTextureLayout& layout) noexcept;
// Maps authored normalized coordinates to linear texels or normalized POT storage without changing image extents.
float GetXboxTextureUvScaleX(const XboxTextureLayout& layout) noexcept;
float GetXboxTextureUvScaleY(const XboxTextureLayout& layout) noexcept;
}
