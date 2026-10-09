#include "platform/xbox/XboxTextureFormat.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

using namespace helengine::xbox;

namespace {

// Builds a bounded source view; test byte vectors remain independent of any encoder implementation.
XboxTextureSourceView Source(std::uint32_t width, std::uint32_t height, XboxTextureColorFormat format,
    XboxTextureAlphaPrecision alpha, const std::uint8_t* bytes, std::size_t length,
    const std::uint8_t* palette = nullptr, std::size_t paletteLength = 0) {
    return {width, height, format, alpha, bytes, length, palette, paletteLength};
}

// Checks expanded channel values without depending on the struct's padding or memory representation.
void Pixel(const XboxRgbaPixel& pixel, unsigned r, unsigned g, unsigned b, unsigned a) {
    assert(pixel.R == r && pixel.G == g && pixel.B == b && pixel.A == a);
}

// Protects legacy channel ordering, continuous Indexed4 packing and anonymous font alpha behavior.
void LegacyFormats() {
    const std::uint8_t rgba[] = {11, 22, 33, 127, 44, 55, 66, 128};
    XboxRgbaPixel pixels[3]{};
    assert(DecodeTextureToRgba8(Source(2, 1, XboxTextureColorFormat::Rgba32, XboxTextureAlphaPrecision::Binary,
        rgba, sizeof(rgba)), pixels, 3) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 11, 22, 33, 0);
    Pixel(pixels[1], 44, 55, 66, 255);
    const std::uint8_t rgba4444[] = {0x21, 0x43};
    assert(DecodeTextureToRgba8(Source(1, 1, XboxTextureColorFormat::Rgba4444, XboxTextureAlphaPrecision::A4,
        rgba4444, sizeof(rgba4444)), pixels, 3) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 17, 34, 51, 68);
    const std::uint8_t indices[] = {0x10, 0};
    const std::uint8_t palette[] = {11, 12, 13, 14, 21, 22, 23, 24};
    assert(DecodeTextureToRgba8(Source(1, 3, XboxTextureColorFormat::Indexed4, XboxTextureAlphaPrecision::A8,
        indices, sizeof(indices), palette, sizeof(palette)), pixels, 3) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 11, 12, 13, 14);
    Pixel(pixels[1], 21, 22, 23, 24);
    Pixel(pixels[2], 11, 12, 13, 14);
    const std::uint8_t index8[] = {1};
    assert(DecodeTextureToRgba8(Source(1, 1, XboxTextureColorFormat::Indexed8, XboxTextureAlphaPrecision::A8,
        index8, sizeof(index8), palette, sizeof(palette)), pixels, 3) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 21, 22, 23, 24);
    assert(ShouldPreserveAnonymousRgbaAlpha(false, XboxTextureColorFormat::Rgba32, XboxTextureAlphaPrecision::Opaque));
    assert(!ShouldPreserveAnonymousRgbaAlpha(true, XboxTextureColorFormat::Rgba32, XboxTextureAlphaPrecision::Opaque));
    assert(!ShouldPreserveAnonymousRgbaAlpha(false, XboxTextureColorFormat::Ia16, XboxTextureAlphaPrecision::Opaque));
    pixels[0] = {0, 0, 0, 0};
    pixels[1] = {2, 3, 4, 5};
    SetTransparentFontAtlasTexelsToWhite(pixels, 2);
    Pixel(pixels[0], 255, 255, 255, 0);
    Pixel(pixels[1], 2, 3, 4, 5);
}

// RGBA5551 is red-high and alpha-low in little-endian words, independent of legacy RGBA4444 ordering.
void Rgba5551Golden() {
    const std::uint8_t bytes[] = {0, 0xF8, 0xC1, 0x07, 0x3F, 0};
    XboxRgbaPixel pixels[3]{};
    assert(DecodeTextureToRgba8(Source(3, 1, XboxTextureColorFormat::Rgba5551, XboxTextureAlphaPrecision::Binary,
        bytes, sizeof(bytes)), pixels, 3) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 255, 0, 0, 0);
    Pixel(pixels[1], 0, 255, 0, 255);
    Pixel(pixels[2], 0, 0, 255, 255);
}

// IA4 odd-width rows skip unused low nibbles; IA8/IA16 preserve their intrinsic channel layout.
void IntensityAlphaGolden() {
    const std::uint8_t ia4[] = {0xF0, 0x9F, 0x1E, 0x2F};
    XboxRgbaPixel pixels[6]{};
    assert(DecodeTextureToRgba8(Source(3, 2, XboxTextureColorFormat::Ia4, XboxTextureAlphaPrecision::Binary,
        ia4, sizeof(ia4)), pixels, 6) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 255, 255, 255, 255);
    Pixel(pixels[1], 0, 0, 0, 0);
    Pixel(pixels[2], 146, 146, 146, 255);
    Pixel(pixels[3], 0, 0, 0, 255);
    Pixel(pixels[4], 255, 255, 255, 0);
    Pixel(pixels[5], 36, 36, 36, 0);
    const std::uint8_t ia8[] = {0x87};
    assert(DecodeTextureToRgba8(Source(1, 1, XboxTextureColorFormat::Ia8, XboxTextureAlphaPrecision::A4,
        ia8, sizeof(ia8)), pixels, 6) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 136, 136, 136, 119);
    const std::uint8_t ia16[] = {117, 100};
    assert(DecodeTextureToRgba8(Source(1, 1, XboxTextureColorFormat::Ia16, XboxTextureAlphaPrecision::A4,
        ia16, sizeof(ia16)), pixels, 6) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 100, 100, 100, 117); // Already packed alpha must not be quantized again.
}

// Intensity channels are coupled RGBA; nine pixels require six bytes rather than continuous four-bit packing.
void IntensityGoldenOddRows() {
    const std::uint8_t i4[] = {0x1F, 0x8F, 0x02, 0x4F, 0x6A, 0xFF};
    const unsigned expected[] = {17, 255, 136, 0, 34, 68, 102, 170, 255};
    XboxRgbaPixel pixels[9]{};
    assert(DecodeTextureToRgba8(Source(3, 3, XboxTextureColorFormat::I4, XboxTextureAlphaPrecision::A4,
        i4, sizeof(i4)), pixels, 9) == XboxTextureStatus::Ok);
    for (unsigned i = 0; i < 9; ++i) Pixel(pixels[i], expected[i], expected[i], expected[i], expected[i]);
    const std::uint8_t i8[] = {76, 150, 29};
    assert(DecodeTextureToRgba8(Source(3, 1, XboxTextureColorFormat::I8, XboxTextureAlphaPrecision::A8,
        i8, sizeof(i8)), pixels, 9) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 76, 76, 76, 76);
    Pixel(pixels[1], 150, 150, 150, 150);
    Pixel(pixels[2], 29, 29, 29, 29);
}

// Neutral endpoints and a red primary fix the limited-range BT.601 YUYV conversion independently.
void YuvGoldenOddRows() {
    const std::uint8_t neutral[] = {16, 128, 235, 128, 16, 128, 235, 128,
        235, 128, 16, 128, 235, 128, 16, 128};
    XboxRgbaPixel pixels[6]{};
    assert(DecodeTextureToRgba8(Source(3, 2, XboxTextureColorFormat::Yuv16, XboxTextureAlphaPrecision::Opaque,
        neutral, sizeof(neutral)), pixels, 6) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 0, 0, 0, 255);
    Pixel(pixels[1], 255, 255, 255, 255);
    Pixel(pixels[2], 0, 0, 0, 255);
    Pixel(pixels[3], 255, 255, 255, 255);
    Pixel(pixels[4], 0, 0, 0, 255);
    Pixel(pixels[5], 255, 255, 255, 255);
    const std::uint8_t red[] = {81, 90, 81, 240};
    assert(DecodeTextureToRgba8(Source(2, 1, XboxTextureColorFormat::Yuv16, XboxTextureAlphaPrecision::Opaque,
        red, sizeof(red)), pixels, 6) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 254, 0, 0, 255);
    Pixel(pixels[1], 254, 0, 0, 255);
}

// PS1's STP bit controls blending separately; nonzero black and color words are visible regardless of bit 15.
void Ps1GoldenWords() {
    const std::uint8_t bytes[] = {0, 0, 0, 0x80, 0x1F, 0, 0, 0xFC};
    XboxRgbaPixel pixels[4]{};
    assert(DecodeTextureToRgba8(Source(4, 1, XboxTextureColorFormat::Ps1Bgr555, XboxTextureAlphaPrecision::Binary,
        bytes, sizeof(bytes)), pixels, 4) == XboxTextureStatus::Ok);
    Pixel(pixels[0], 0, 0, 0, 0);
    Pixel(pixels[1], 0, 0, 0, 255);
    Pixel(pixels[2], 255, 0, 0, 255);
    Pixel(pixels[3], 0, 0, 255, 255);
}

// New formats reject truncated/oversized payloads and incompatible alpha without accepting unknown IDs.
void MalformedAndPolicyChecks() {
    const std::uint8_t bytes[24]{};
    XboxRgbaPixel pixels[9]{};
    const XboxTextureColorFormat formats[] = {XboxTextureColorFormat::Rgba5551, XboxTextureColorFormat::Ia4,
        XboxTextureColorFormat::Ia8, XboxTextureColorFormat::Ia16, XboxTextureColorFormat::I4,
        XboxTextureColorFormat::I8, XboxTextureColorFormat::Yuv16, XboxTextureColorFormat::Ps1Bgr555};
    const XboxTextureAlphaPrecision valid[] = {XboxTextureAlphaPrecision::Binary, XboxTextureAlphaPrecision::Binary,
        XboxTextureAlphaPrecision::A4, XboxTextureAlphaPrecision::A8, XboxTextureAlphaPrecision::A4,
        XboxTextureAlphaPrecision::A8, XboxTextureAlphaPrecision::Opaque, XboxTextureAlphaPrecision::Binary};
    const XboxTextureAlphaPrecision invalid[] = {XboxTextureAlphaPrecision::A8, XboxTextureAlphaPrecision::A4,
        XboxTextureAlphaPrecision::A8, static_cast<XboxTextureAlphaPrecision>(255), XboxTextureAlphaPrecision::Opaque,
        XboxTextureAlphaPrecision::A4, XboxTextureAlphaPrecision::Binary, XboxTextureAlphaPrecision::A8};
    const std::size_t lengths[] = {18, 6, 9, 18, 6, 9, 24, 18};
    for (unsigned i = 0; i < 8; ++i) {
        assert(DecodeTextureToRgba8(Source(3, 3, formats[i], valid[i], bytes, lengths[i]), pixels, 9) == XboxTextureStatus::Ok);
        assert(DecodeTextureToRgba8(Source(3, 3, formats[i], valid[i], bytes, lengths[i] - 1), pixels, 9) == XboxTextureStatus::MalformedPayload);
        assert(DecodeTextureToRgba8(Source(3, 3, formats[i], valid[i], bytes, lengths[i] + 1), pixels, 9) == XboxTextureStatus::MalformedPayload);
        assert(DecodeTextureToRgba8(Source(3, 3, formats[i], invalid[i], bytes, lengths[i]), pixels, 9) == XboxTextureStatus::MalformedPayload);
    }
    assert(DecodeTextureToRgba8(Source(1, 1, XboxTextureColorFormat::GxRgb5A3, XboxTextureAlphaPrecision::A8,
        bytes, 2), pixels, 9) == XboxTextureStatus::UnsupportedFormat);
    assert(DecodeTextureToRgba8(Source(1, 1, static_cast<XboxTextureColorFormat>(255), XboxTextureAlphaPrecision::A8,
        bytes, 2), pixels, 9) == XboxTextureStatus::UnsupportedFormat);
    assert(DecodeTextureToRgba8(Source(0, 1, XboxTextureColorFormat::Rgba32, XboxTextureAlphaPrecision::A8,
        bytes, 4), pixels, 9) == XboxTextureStatus::InvalidExtent);
    assert(DecodeTextureToRgba8(Source(std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max(),
        XboxTextureColorFormat::Yuv16, XboxTextureAlphaPrecision::Opaque, bytes, 4), pixels, 9) == XboxTextureStatus::SizeOverflow);
    assert(DecodeTextureToRgba8(Source(1, 1, XboxTextureColorFormat::Ia16, XboxTextureAlphaPrecision::A8,
        bytes, 2), pixels, 0) == XboxTextureStatus::InsufficientOutput);
    assert(DecodeTextureToRgba8(Source(1, 1, XboxTextureColorFormat::Ia16, XboxTextureAlphaPrecision::A8,
        nullptr, 2), pixels, 9) == XboxTextureStatus::MalformedPayload);
}

// Palette entry counts and referenced indices are bounded even when the source claims a large palette allocation.
void PaletteLimits() {
    const std::uint8_t index[] = {1};
    const std::uint8_t palette[4]{};
    XboxRgbaPixel pixel{};
    XboxTextureSourceView source = Source(1, 1, XboxTextureColorFormat::Indexed8, XboxTextureAlphaPrecision::A8,
        index, 1, palette, sizeof(palette));
    assert(DecodeTextureToRgba8(source, &pixel, 1) == XboxTextureStatus::InvalidPalette);
    source.PaletteLength = 3;
    assert(DecodeTextureToRgba8(source, &pixel, 1) == XboxTextureStatus::InvalidPalette);
    source.PaletteLength = 257 * 4;
    assert(DecodeTextureToRgba8(source, &pixel, 1) == XboxTextureStatus::InvalidPalette);
    source.ColorFormat = XboxTextureColorFormat::Indexed4;
    source.PaletteLength = 17 * 4;
    assert(DecodeTextureToRgba8(source, &pixel, 1) == XboxTextureStatus::InvalidPalette);
}

} // namespace

int main() {
    static_assert(static_cast<unsigned>(XboxTextureColorFormat::Rgba32) == 0);
    static_assert(static_cast<unsigned>(XboxTextureColorFormat::Yuv16) == 11);
    static_assert(static_cast<unsigned>(XboxTextureColorFormat::Ps1Bgr555) == 12);
    LegacyFormats();
    Rgba5551Golden();
    IntensityAlphaGolden();
    IntensityGoldenOddRows();
    YuvGoldenOddRows();
    Ps1GoldenWords();
    MalformedAndPolicyChecks();
    PaletteLimits();
    std::puts("Xbox texture format tests: 8 groups passed");
}
