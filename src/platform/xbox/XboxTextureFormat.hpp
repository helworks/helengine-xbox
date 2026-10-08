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

// Engine-generated inline font atlases may have no asset identity and retain
// Opaque as the default metadata even though their RGBA pixels carry glyph
// coverage in alpha. Preserve that channel only for this anonymous RGBA case.
bool ShouldPreserveAnonymousRgbaAlpha(bool hasStableIdentity, XboxTextureColorFormat colorFormat,
                                      XboxTextureAlphaPrecision alphaPrecision) noexcept;

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

// Keep zero-alpha font-atlas texels white so linear filtering does not darken
// the sampled RGB before the coverage alpha is applied by sprite blending.
void SetTransparentFontAtlasTexelsToWhite(XboxRgbaPixel* pixels, std::size_t pixelCount) noexcept;

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
