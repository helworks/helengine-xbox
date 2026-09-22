#include "XboxTextureFormat.hpp"

#include <limits>

namespace helengine::xbox {
namespace {

bool MultiplySize(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right) return false;
    result = left * right;
    return true;
}

std::uint8_t ExpandNibble(std::uint8_t value) noexcept {
    value &= 0x0f;
    return static_cast<std::uint8_t>((value << 4) | value);
}

std::uint8_t ApplyAlpha(std::uint8_t alpha, XboxTextureAlphaPrecision precision) noexcept {
    switch (precision) {
        case XboxTextureAlphaPrecision::Opaque: return 0xff;
        case XboxTextureAlphaPrecision::Binary: return alpha >= 128 ? 0xff : 0;
        case XboxTextureAlphaPrecision::A4: return ExpandNibble(alpha);
        case XboxTextureAlphaPrecision::A8: return alpha;
    }
    return alpha;
}

} // namespace

XboxTextureStatus DecodeTextureToRgba8(const XboxTextureSourceView& source, XboxRgbaPixel* output,
                                       std::size_t outputPixelCapacity) noexcept {
    if (source.Width == 0 || source.Height == 0) return XboxTextureStatus::InvalidExtent;

    std::size_t pixelCount = 0;
    if (!MultiplySize(source.Width, source.Height, pixelCount) ||
        pixelCount > std::numeric_limits<std::size_t>::max() / sizeof(XboxRgbaPixel)) {
        return XboxTextureStatus::SizeOverflow;
    }
    if (pixelCount > outputPixelCapacity || output == nullptr) return XboxTextureStatus::InsufficientOutput;

    std::size_t requiredBytes = 0;
    switch (source.ColorFormat) {
        case XboxTextureColorFormat::Rgba32:
            if (!MultiplySize(pixelCount, 4, requiredBytes)) return XboxTextureStatus::SizeOverflow;
            break;
        case XboxTextureColorFormat::Rgba4444:
            if (!MultiplySize(pixelCount, 2, requiredBytes)) return XboxTextureStatus::SizeOverflow;
            break;
        case XboxTextureColorFormat::Indexed4:
            requiredBytes = pixelCount / 2 + pixelCount % 2;
            break;
        case XboxTextureColorFormat::Indexed8:
            requiredBytes = pixelCount;
            break;
        default:
            return XboxTextureStatus::UnsupportedFormat;
    }
    if (source.Colors == nullptr || source.ColorsLength < requiredBytes) return XboxTextureStatus::MalformedPayload;

    if (source.AlphaPrecision != XboxTextureAlphaPrecision::Opaque &&
        source.AlphaPrecision != XboxTextureAlphaPrecision::Binary &&
        source.AlphaPrecision != XboxTextureAlphaPrecision::A4 &&
        source.AlphaPrecision != XboxTextureAlphaPrecision::A8) {
        return XboxTextureStatus::MalformedPayload;
    }

    const bool indexed = source.ColorFormat == XboxTextureColorFormat::Indexed4 ||
                         source.ColorFormat == XboxTextureColorFormat::Indexed8;
    const std::size_t paletteEntries = source.PaletteLength / 4;
    if (indexed && (source.PaletteColors == nullptr || source.PaletteLength == 0 || source.PaletteLength % 4 != 0)) {
        return XboxTextureStatus::InvalidPalette;
    }

    for (std::size_t i = 0; i < pixelCount; ++i) {
        XboxRgbaPixel pixel{};
        switch (source.ColorFormat) {
            case XboxTextureColorFormat::Rgba32: {
                const std::size_t offset = i * 4;
                pixel = {source.Colors[offset], source.Colors[offset + 1], source.Colors[offset + 2],
                         source.Colors[offset + 3]};
                break;
            }
            case XboxTextureColorFormat::Rgba4444: {
                const std::size_t offset = i * 2;
                const std::uint8_t rg = source.Colors[offset];
                const std::uint8_t ba = source.Colors[offset + 1];
                pixel = {ExpandNibble(rg & 0x0f), ExpandNibble(rg >> 4), ExpandNibble(ba & 0x0f),
                         ExpandNibble(ba >> 4)};
                break;
            }
            case XboxTextureColorFormat::Indexed4:
            case XboxTextureColorFormat::Indexed8: {
                std::uint8_t index = 0;
                if (source.ColorFormat == XboxTextureColorFormat::Indexed4) {
                    const std::uint8_t packed = source.Colors[i / 2];
                    index = static_cast<std::uint8_t>((i & 1) == 0 ? packed & 0x0f : packed >> 4);
                } else {
                    index = source.Colors[i];
                }
                if (index >= paletteEntries) return XboxTextureStatus::InvalidPalette;
                const std::size_t offset = static_cast<std::size_t>(index) * 4;
                pixel = {source.PaletteColors[offset], source.PaletteColors[offset + 1],
                         source.PaletteColors[offset + 2], source.PaletteColors[offset + 3]};
                break;
            }
            default:
                return XboxTextureStatus::UnsupportedFormat;
        }
        pixel.A = ApplyAlpha(pixel.A, source.AlphaPrecision);
        output[i] = pixel;
    }
    return XboxTextureStatus::Ok;
}

} // namespace helengine::xbox
