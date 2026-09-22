#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::xbox {

enum class XboxTextureColorFormat : std::uint8_t {
    Rgba32 = 0,
    Rgba4444 = 1,
    Indexed4 = 2,
    Indexed8 = 3,
};

enum class XboxTextureAlphaPrecision : std::uint8_t {
    Opaque = 0,
    Binary = 1,
    A4 = 2,
    A8 = 3,
};

struct XboxTextureSourceView {
    std::uint32_t Width;
    std::uint32_t Height;
    XboxTextureColorFormat ColorFormat;
    XboxTextureAlphaPrecision AlphaPrecision;
    const std::uint8_t* Colors;
    std::size_t ColorsLength;
    const std::uint8_t* PaletteColors;
    std::size_t PaletteLength;
};

struct XboxRgbaPixel {
    std::uint8_t R;
    std::uint8_t G;
    std::uint8_t B;
    std::uint8_t A;
};

enum class XboxTextureStatus : std::uint8_t {
    Ok,
    InvalidExtent,
    SizeOverflow,
    UnsupportedFormat,
    MalformedPayload,
    InvalidPalette,
    InsufficientOutput,
    UnsupportedExtent,
};

XboxTextureStatus DecodeTextureToRgba8(const XboxTextureSourceView& source, XboxRgbaPixel* output,
                                       std::size_t outputPixelCapacity) noexcept;

} // namespace helengine::xbox
