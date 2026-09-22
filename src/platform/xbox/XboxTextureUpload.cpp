#include "XboxTextureUpload.hpp"

#include <limits>

namespace helengine::xbox {
namespace {

constexpr std::uint32_t kNvTextureFormatA8R8G8B8 = 0x12;
constexpr std::uint32_t kMaxLinearPitchBytes = 0xffff;
constexpr std::uint32_t kMaxTextureDimension = 0xffff;

} // namespace

XboxTextureStatus BuildXboxTextureLayout(std::uint32_t width, std::uint32_t height,
                                         XboxTextureLayout& layout) noexcept {
    if (width == 0 || height == 0) return XboxTextureStatus::InvalidExtent;
    if (width > kMaxTextureDimension || height > kMaxTextureDimension || width > kMaxLinearPitchBytes / 4) {
        return XboxTextureStatus::UnsupportedExtent;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    if (height > std::numeric_limits<std::size_t>::max() / rowBytes) return XboxTextureStatus::SizeOverflow;

    const XboxTextureLayout candidate{width, height, width, height, 4, kNvTextureFormatA8R8G8B8,
                                      rowBytes * static_cast<std::size_t>(height)};
    layout = candidate;
    return XboxTextureStatus::Ok;
}

XboxTextureStatus ConvertTextureToNv2a(const XboxTextureSourceView& source, const XboxRgbaPixel* decodedPixels,
                                       const XboxTextureLayout& layout, std::uint8_t* output,
                                       std::size_t outputCapacity) noexcept {
    if (source.Width == 0 || source.Height == 0 || source.Width != layout.RealWidth ||
        source.Height != layout.RealHeight || layout.StorageWidth != layout.RealWidth ||
        layout.StorageHeight != layout.RealHeight || layout.BytesPerTexel != 4 ||
        layout.HardwareFormat != kNvTextureFormatA8R8G8B8) {
        return XboxTextureStatus::MalformedPayload;
    }
    if (decodedPixels == nullptr) return XboxTextureStatus::MalformedPayload;
    if (outputCapacity < layout.ByteCount || output == nullptr) return XboxTextureStatus::InsufficientOutput;

    if (layout.RealWidth > std::numeric_limits<std::size_t>::max() / layout.RealHeight) {
        return XboxTextureStatus::SizeOverflow;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(layout.RealWidth) * layout.RealHeight;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4 || layout.ByteCount != pixelCount * 4) {
        return XboxTextureStatus::SizeOverflow;
    }
    for (std::size_t i = 0; i < pixelCount; ++i) {
        output[i * 4] = decodedPixels[i].B;
        output[i * 4 + 1] = decodedPixels[i].G;
        output[i * 4 + 2] = decodedPixels[i].R;
        output[i * 4 + 3] = decodedPixels[i].A;
    }
    return XboxTextureStatus::Ok;
}

} // namespace helengine::xbox
