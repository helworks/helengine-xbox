#include "XboxNativeTexturePayload.hpp"

namespace helengine::xbox {
namespace {
// Header integers are always little-endian, independently of the serialized container's endianness.
std::uint32_t ReadHeaderWord(const std::uint8_t* data, unsigned offset) noexcept {
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset+1])<<8) |
        (std::uint32_t(data[offset+2])<<16) | (std::uint32_t(data[offset+3])<<24);
}
}
XboxTextureStatus ParseXboxNativeTexturePayload(const XboxTextureSourceView& source,
    XboxNativeTexturePayload& payload) noexcept {
    payload = {};
    if (source.ColorFormat != XboxTextureColorFormat::XboxNative) return XboxTextureStatus::UnsupportedFormat;
    if (source.AlphaPrecision > XboxTextureAlphaPrecision::A8) return XboxTextureStatus::MalformedPayload;
    if (!source.Colors || source.ColorsLength < XboxNativeTextureHeaderBytes) return XboxTextureStatus::MalformedPayload;
    const auto* header = source.Colors;
    if (ReadHeaderWord(header,0) != 0x31585458 || ReadHeaderWord(header,4) != 1 || ReadHeaderWord(header,12) != 0)
        return XboxTextureStatus::MalformedPayload;
    if (ReadHeaderWord(header,16) != source.Width || ReadHeaderWord(header,20) != source.Height)
        return XboxTextureStatus::MalformedPayload;
    XboxTextureLayout layout{};
    const auto status = BuildXboxNativeTextureLayout(source.Width,source.Height,ReadHeaderWord(header,24),
        ReadHeaderWord(header,28),ReadHeaderWord(header,32),ReadHeaderWord(header,8),ReadHeaderWord(header,40),layout);
    if (status != XboxTextureStatus::Ok) return status;
    if (ReadHeaderWord(header,36) != layout.ByteCount || source.ColorsLength - XboxNativeTextureHeaderBytes != layout.ByteCount)
        return XboxTextureStatus::MalformedPayload;
    const std::size_t paletteBytes = static_cast<std::size_t>(layout.PaletteEntries) * 4;
    if (ReadHeaderWord(header,44) != paletteBytes || source.PaletteLength != paletteBytes ||
        (paletteBytes && !source.PaletteColors)) return XboxTextureStatus::InvalidPalette;
    const auto* texels = source.Colors + XboxNativeTextureHeaderBytes;
    if (paletteBytes) {
        for (std::size_t pixel=0;pixel<layout.ByteCount;++pixel)
            if (texels[pixel] >= layout.PaletteEntries) return XboxTextureStatus::InvalidPalette;
    }
    payload.Layout = layout;
    payload.Texels = texels;
    payload.Palette = source.PaletteColors;
    payload.PaletteByteCount = paletteBytes;
    return XboxTextureStatus::Ok;
}
}
