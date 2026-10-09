#pragma once
#include "XboxNativeTextureLayout.hpp"

namespace helengine::xbox {
constexpr std::size_t XboxNativeTextureHeaderBytes = 48;
// All spans remain borrowed from the validated cooked asset until its direct GPU copy completes.
struct XboxNativeTexturePayload {
    XboxTextureLayout Layout{};
    const std::uint8_t* Texels = nullptr;
    const std::uint8_t* Palette = nullptr;
    std::size_t PaletteByteCount = 0;
};
// Parses XTX1 v1, requires exact dimensions/lengths/reserved flags and validates every native P8 index.
XboxTextureStatus ParseXboxNativeTexturePayload(const XboxTextureSourceView& source,
    XboxNativeTexturePayload& payload) noexcept;
}
