#pragma once

#include "XboxTextureFormat.hpp"

namespace helengine::xbox {

struct XboxTextureLayout {
    std::uint32_t RealWidth;
    std::uint32_t RealHeight;
    std::uint32_t StorageWidth;
    std::uint32_t StorageHeight;
    std::uint32_t BytesPerTexel;
    std::uint32_t HardwareFormat;
    std::size_t ByteCount;
};

XboxTextureStatus BuildXboxTextureLayout(std::uint32_t width, std::uint32_t height,
                                         XboxTextureLayout& layout) noexcept;
XboxTextureStatus ConvertTextureToNv2a(const XboxTextureSourceView& source, const XboxRgbaPixel* decodedPixels,
                                      const XboxTextureLayout& layout, std::uint8_t* output,
                                      std::size_t outputCapacity) noexcept;

} // namespace helengine::xbox
