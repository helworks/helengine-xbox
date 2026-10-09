#include "XboxNativeTextureLayout.hpp"
#include <limits>

namespace helengine::xbox {
namespace {
// Code, bytes per texel, bytes per compressed block, linear sampling and palette usage.
constexpr XboxNativeFormatInfo Formats[] = {
    {0x00,1,0,false,false}, {0x01,1,0,false,false}, {0x02,2,0,false,false}, {0x03,2,0,false,false},
    {0x04,2,0,false,false}, {0x05,2,0,false,false}, {0x06,4,0,false,false}, {0x07,4,0,false,false},
    {0x0B,1,0,false,true}, {0x0C,0,8,false,false}, {0x0E,0,16,false,false}, {0x0F,0,16,false,false},
    {0x10,2,0,true,false}, {0x11,2,0,true,false}, {0x12,4,0,true,false}, {0x13,1,0,true,false},
    {0x16,2,0,true,false}, {0x17,2,0,true,false}, {0x19,1,0,false,false}, {0x1A,2,0,false,false},
    {0x1B,1,0,true,false}, {0x1C,2,0,true,false}, {0x1D,2,0,true,false}, {0x1E,4,0,true,false},
    {0x1F,1,0,true,false}, {0x20,2,0,true,false}, {0x24,2,0,true,false}, {0x25,2,0,true,false},
    {0x27,2,0,false,false}, {0x28,2,0,false,false}, {0x29,2,0,false,false}, {0x2C,2,0,false,false},
    {0x2E,4,0,true,false}, {0x30,2,0,true,false}, {0x31,2,0,true,false}, {0x35,2,0,true,false},
    {0x3A,4,0,false,false}, {0x3B,4,0,false,false}, {0x3C,4,0,false,false}, {0x3F,4,0,true,false},
    {0x40,4,0,true,false}, {0x41,4,0,true,false}
};
static_assert(sizeof(Formats) / sizeof(Formats[0]) == XboxNativeFormatCount);

// Accepts only powers of two; native non-linear payloads have no row pitch to repair irregular storage.
bool PowerOfTwo(std::uint32_t value) noexcept { return value != 0 && (value & (value - 1)) == 0; }
// Converts the validated POT storage extent to the exponent required by NV097_SET_TEXTURE_FORMAT.
std::uint32_t Log2(std::uint32_t value) noexcept {
    std::uint32_t exponent = 0;
    while (value > 1) { value >>= 1; ++exponent; }
    return exponent;
}
}

bool TryGetXboxNativeFormatInfo(std::uint32_t hardwareCode, XboxNativeFormatInfo& info) noexcept {
    info = {};
    for (const auto& candidate : Formats) {
        if (candidate.HardwareCode == hardwareCode) { info = candidate; return true; }
    }
    return false;
}

XboxTextureStatus BuildXboxNativeTextureLayout(std::uint32_t width, std::uint32_t height,
    std::uint32_t storageWidth, std::uint32_t storageHeight, std::uint32_t pitchBytes,
    std::uint32_t hardwareCode, std::uint32_t paletteEntries, XboxTextureLayout& layout) noexcept {
    layout = {};
    XboxNativeFormatInfo format;
    if (!TryGetXboxNativeFormatInfo(hardwareCode, format)) return XboxTextureStatus::UnsupportedFormat;
    if (!width || !height || !storageWidth || !storageHeight) return XboxTextureStatus::InvalidExtent;
    if (width > 4096 || height > 4096 || storageWidth > 4096 || storageHeight > 4096 ||
        width > storageWidth || height > storageHeight) return XboxTextureStatus::UnsupportedExtent;
    if (format.Paletted) {
        if (paletteEntries != 32 && paletteEntries != 64 && paletteEntries != 128 && paletteEntries != 256)
            return XboxTextureStatus::InvalidPalette;
    } else if (paletteEntries != 0) return XboxTextureStatus::InvalidPalette;
    std::uint64_t bytes;
    if (format.Linear) {
        if (storageWidth != width || storageHeight != height) return XboxTextureStatus::MalformedPayload;
        // Native YUV pairs share chroma and occupy a padded final pair on odd-width rows.
        const std::uint32_t minimumPitch = hardwareCode == 0x24 || hardwareCode == 0x25
            ? ((width + 1) / 2) * 4 : width * format.BytesPerTexel;
        const std::uint32_t expectedPitch = (minimumPitch + 63) & ~63u;
        if (pitchBytes != expectedPitch || pitchBytes > 0xffff) return XboxTextureStatus::MalformedPayload;
        bytes = static_cast<std::uint64_t>(pitchBytes) * height;
    } else {
        if (!PowerOfTwo(storageWidth) || !PowerOfTwo(storageHeight) || pitchBytes != 0)
            return XboxTextureStatus::MalformedPayload;
        if (format.BlockBytes) {
            if (storageWidth < 4 || storageHeight < 4) return XboxTextureStatus::UnsupportedExtent;
            bytes = static_cast<std::uint64_t>(storageWidth / 4) * (storageHeight / 4) * format.BlockBytes;
        } else bytes = static_cast<std::uint64_t>(storageWidth) * storageHeight * format.BytesPerTexel;
    }
    if (bytes > std::numeric_limits<std::size_t>::max()) return XboxTextureStatus::SizeOverflow;
    layout = {width,height,storageWidth,storageHeight,format.BytesPerTexel,hardwareCode,static_cast<std::size_t>(bytes),
        true,pitchBytes,paletteEntries};
    return XboxTextureStatus::Ok;
}

bool ValidateXboxTextureLayout(const XboxTextureLayout& layout) noexcept {
    XboxTextureLayout expected{};
    const XboxTextureStatus status = layout.NativePayload
        ? BuildXboxNativeTextureLayout(layout.RealWidth,layout.RealHeight,layout.StorageWidth,layout.StorageHeight,
            layout.PitchBytes,layout.HardwareFormat,layout.PaletteEntries,expected)
        : BuildXboxTextureLayout(layout.RealWidth,layout.RealHeight,expected);
    return status == XboxTextureStatus::Ok && layout.StorageWidth == expected.StorageWidth &&
        layout.StorageHeight == expected.StorageHeight && layout.BytesPerTexel == expected.BytesPerTexel &&
        layout.HardwareFormat == expected.HardwareFormat && layout.ByteCount == expected.ByteCount &&
        layout.PitchBytes == expected.PitchBytes && layout.PaletteEntries == expected.PaletteEntries;
}

std::uint32_t GetXboxTextureFormatWord(const XboxTextureLayout& layout) noexcept {
    if (!ValidateXboxTextureLayout(layout)) return 0;
    if (!layout.NativePayload) return 0x00011222;
    XboxNativeFormatInfo format;
    TryGetXboxNativeFormatInfo(layout.HardwareFormat,format);
    return 0x0001002A | (layout.HardwareFormat << 8) |
        (format.Linear ? 0 : ((Log2(layout.StorageWidth) << 20) | (Log2(layout.StorageHeight) << 24)));
}

float GetXboxTextureUvScaleX(const XboxTextureLayout& layout) noexcept {
    XboxNativeFormatInfo format;
    if (!layout.NativePayload) return static_cast<float>(layout.RealWidth);
    if (!TryGetXboxNativeFormatInfo(layout.HardwareFormat,format) || !layout.StorageWidth) return 0;
    return format.Linear ? static_cast<float>(layout.RealWidth) : static_cast<float>(layout.RealWidth) / layout.StorageWidth;
}
float GetXboxTextureUvScaleY(const XboxTextureLayout& layout) noexcept {
    XboxNativeFormatInfo format;
    if (!layout.NativePayload) return static_cast<float>(layout.RealHeight);
    if (!TryGetXboxNativeFormatInfo(layout.HardwareFormat,format) || !layout.StorageHeight) return 0;
    return format.Linear ? static_cast<float>(layout.RealHeight) : static_cast<float>(layout.RealHeight) / layout.StorageHeight;
}
}
