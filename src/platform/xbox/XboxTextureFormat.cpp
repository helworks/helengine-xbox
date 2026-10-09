#include "XboxTextureFormat.hpp"

#include <limits>
#include <cmath>

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

// New formats carry their intrinsic precision; do not silently reinterpret unsupported alpha metadata.
bool SupportsAlpha(XboxTextureColorFormat format, XboxTextureAlphaPrecision precision) noexcept {
    if (precision > XboxTextureAlphaPrecision::A8) return false;
    switch (format) {
        case XboxTextureColorFormat::Rgba5551:
        case XboxTextureColorFormat::Ia4:
        case XboxTextureColorFormat::Ps1Bgr555:
            return precision == XboxTextureAlphaPrecision::Opaque || precision == XboxTextureAlphaPrecision::Binary;
        case XboxTextureColorFormat::Ia8:
            return precision != XboxTextureAlphaPrecision::A8;
        case XboxTextureColorFormat::I4:
            return precision == XboxTextureAlphaPrecision::A4;
        case XboxTextureColorFormat::I8:
            return precision == XboxTextureAlphaPrecision::A8;
        case XboxTextureColorFormat::Yuv16:
            return precision == XboxTextureAlphaPrecision::Opaque;
        default:
            return true;
    }
}

// Generic packed RGB/IA words are little-endian regardless of CPU or eventual GPU upload format.
std::uint16_t ReadWord(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
}

// Matches the engine codec's nearest-integer expansion of an unsigned stored channel to eight bits.
std::uint8_t ExpandBits(unsigned value, unsigned maximum) noexcept {
    return static_cast<std::uint8_t>((value * 255u + maximum / 2u) / maximum);
}

// PS1 words use bit replication for their five-bit channels, preserving the separate PS1 codec contract.
std::uint8_t ExpandPs1Channel(unsigned value) noexcept {
    return static_cast<std::uint8_t>((value << 3) | (value >> 2));
}

// Clamps BT.601 matrix overshoot and rounds the same way as the engine's double-precision codec.
std::uint8_t RoundByte(double value) noexcept {
    if (value <= 0.0) return 0;
    if (value >= 255.0) return 255;
    return static_cast<std::uint8_t>(std::floor(value + 0.5));
}

} // namespace

bool ShouldPreserveAnonymousRgbaAlpha(bool hasStableIdentity, XboxTextureColorFormat colorFormat,
                                      XboxTextureAlphaPrecision alphaPrecision) noexcept {
    return !hasStableIdentity && colorFormat == XboxTextureColorFormat::Rgba32 &&
           alphaPrecision == XboxTextureAlphaPrecision::Opaque;
}

void SetTransparentFontAtlasTexelsToWhite(XboxRgbaPixel* pixels, std::size_t pixelCount) noexcept {
    if (pixels == nullptr) return;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        if (pixels[i].A != 0) continue;
        pixels[i].R = 0xff;
        pixels[i].G = 0xff;
        pixels[i].B = 0xff;
    }
}

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
    std::size_t rowStride = 0;
    switch (source.ColorFormat) {
        case XboxTextureColorFormat::Rgba32:
            if (!MultiplySize(pixelCount, 4, requiredBytes)) return XboxTextureStatus::SizeOverflow;
            break;
        case XboxTextureColorFormat::Rgba4444:
        case XboxTextureColorFormat::Rgba5551:
        case XboxTextureColorFormat::Ia16:
        case XboxTextureColorFormat::Ps1Bgr555:
            if (!MultiplySize(pixelCount, 2, requiredBytes)) return XboxTextureStatus::SizeOverflow;
            break;
        case XboxTextureColorFormat::Indexed4:
            requiredBytes = pixelCount / 2 + pixelCount % 2;
            break;
        case XboxTextureColorFormat::Indexed8:
        case XboxTextureColorFormat::Ia8:
        case XboxTextureColorFormat::I8:
            requiredBytes = pixelCount;
            break;
        case XboxTextureColorFormat::Ia4:
        case XboxTextureColorFormat::I4:
            rowStride = static_cast<std::size_t>(source.Width) / 2 + source.Width % 2;
            if (!MultiplySize(rowStride, source.Height, requiredBytes)) return XboxTextureStatus::SizeOverflow;
            break;
        case XboxTextureColorFormat::Yuv16:
            if (!MultiplySize(static_cast<std::size_t>(source.Width) / 2 + source.Width % 2, 4, rowStride) ||
                !MultiplySize(rowStride, source.Height, requiredBytes)) return XboxTextureStatus::SizeOverflow;
            break;
        default:
            return XboxTextureStatus::UnsupportedFormat;
    }
    if (source.Colors == nullptr || source.ColorsLength < requiredBytes) return XboxTextureStatus::MalformedPayload;
    const bool newFormat = source.ColorFormat >= XboxTextureColorFormat::Rgba5551;
    if (newFormat && source.ColorsLength != requiredBytes) return XboxTextureStatus::MalformedPayload;

    if (source.AlphaPrecision != XboxTextureAlphaPrecision::Opaque &&
        source.AlphaPrecision != XboxTextureAlphaPrecision::Binary &&
        source.AlphaPrecision != XboxTextureAlphaPrecision::A4 &&
        source.AlphaPrecision != XboxTextureAlphaPrecision::A8) {
        return XboxTextureStatus::MalformedPayload;
    }
    if (!SupportsAlpha(source.ColorFormat, source.AlphaPrecision)) return XboxTextureStatus::MalformedPayload;

    const bool indexed = source.ColorFormat == XboxTextureColorFormat::Indexed4 ||
                         source.ColorFormat == XboxTextureColorFormat::Indexed8;
    const std::size_t paletteEntries = source.PaletteLength / 4;
    if (indexed && (source.PaletteColors == nullptr || source.PaletteLength == 0 || source.PaletteLength % 4 != 0 ||
        paletteEntries > (source.ColorFormat == XboxTextureColorFormat::Indexed4 ? 16u : 256u))) {
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
            case XboxTextureColorFormat::Rgba5551: {
                const std::uint16_t word = ReadWord(source.Colors + i * 2);
                pixel = {ExpandBits(word >> 11, 31), ExpandBits((word >> 6) & 31, 31),
                         ExpandBits((word >> 1) & 31, 31), static_cast<std::uint8_t>((word & 1) * 255)};
                break;
            }
            case XboxTextureColorFormat::Ia4:
            case XboxTextureColorFormat::I4: {
                const std::size_t column = i % source.Width;
                const std::uint8_t packed = source.Colors[(i / source.Width) * rowStride + column / 2];
                const unsigned nibble = (packed >> ((column % 2 == 0) ? 4 : 0)) & 15;
                const std::uint8_t intensity = source.ColorFormat == XboxTextureColorFormat::Ia4
                    ? ExpandBits(nibble >> 1, 7) : ExpandNibble(static_cast<std::uint8_t>(nibble));
                const std::uint8_t alpha = source.ColorFormat == XboxTextureColorFormat::Ia4
                    ? static_cast<std::uint8_t>((nibble & 1) * 255) : intensity;
                pixel = {intensity, intensity, intensity, alpha};
                break;
            }
            case XboxTextureColorFormat::Ia8: {
                const std::uint8_t intensity = ExpandNibble(source.Colors[i] >> 4);
                pixel = {intensity, intensity, intensity, ExpandNibble(source.Colors[i] & 15)};
                break;
            }
            case XboxTextureColorFormat::Ia16: {
                const std::uint8_t intensity = source.Colors[i * 2 + 1];
                pixel = {intensity, intensity, intensity, source.Colors[i * 2]};
                break;
            }
            case XboxTextureColorFormat::I8: {
                const std::uint8_t intensity = source.Colors[i];
                pixel = {intensity, intensity, intensity, intensity};
                break;
            }
            case XboxTextureColorFormat::Yuv16: {
                const std::size_t column = i % source.Width;
                const std::size_t pair = (i / source.Width) * rowStride + (column / 2) * 4;
                const double y = source.Colors[pair + (column % 2) * 2] - 16.0;
                const double u = source.Colors[pair + 1] - 128.0;
                const double v = source.Colors[pair + 3] - 128.0;
                pixel = {RoundByte(1.1643835616438356 * y + 1.5960267857142858 * v),
                         RoundByte(1.1643835616438356 * y - 0.39176229009491365 * u - 0.8129676472377708 * v),
                         RoundByte(1.1643835616438356 * y + 2.017232142857143 * u), 255};
                break;
            }
            case XboxTextureColorFormat::Ps1Bgr555: {
                const std::uint16_t word = ReadWord(source.Colors + i * 2);
                pixel = {ExpandPs1Channel(word & 31), ExpandPs1Channel((word >> 5) & 31),
                         ExpandPs1Channel((word >> 10) & 31), static_cast<std::uint8_t>(word == 0 ? 0 : 255)};
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
        if (!newFormat) pixel.A = ApplyAlpha(pixel.A, source.AlphaPrecision);
        output[i] = pixel;
    }
    return XboxTextureStatus::Ok;
}

} // namespace helengine::xbox
