#include "XboxRuntimeTexture.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include <cstring>
#include <new>

#include "runtime/array.hpp"
#include <xboxkrnl/xboxkrnl.h>

namespace helengine::xbox {
namespace {

constexpr ULONG_PTR kGpuHighestAddress = 0x03FFAFFF;
constexpr std::uint32_t kGpuAddressMask = 0x03FFFFFF;

const char* TextureStatusReason(XboxTextureStatus status, XboxTextureColorFormat format) {
    if (format == static_cast<XboxTextureColorFormat>(4)) {
        return "GX RGB5A3 tiled texture format is unsupported on Xbox";
    }
    switch (status) {
        case XboxTextureStatus::InvalidExtent: return "texture has a zero extent";
        case XboxTextureStatus::SizeOverflow: return "texture size overflows addressable memory";
        case XboxTextureStatus::UnsupportedFormat: return "texture color format is unsupported";
        case XboxTextureStatus::MalformedPayload: return "texture color payload is malformed";
        case XboxTextureStatus::InvalidPalette: return "texture palette is malformed or contains an invalid index";
        case XboxTextureStatus::InsufficientOutput: return "texture conversion buffer is insufficient";
        case XboxTextureStatus::UnsupportedExtent: return "texture extent exceeds the NV2A linear texture limits";
        case XboxTextureStatus::Ok: break;
    }
    return "texture conversion failed";
}

} // namespace

XboxRuntimeTexture::XboxRuntimeTexture() : ::RuntimeTexture() {
}

XboxRuntimeTexture::~XboxRuntimeTexture() {
    Dispose();
}

XboxRuntimeTexture* XboxRuntimeTexture::Create(const ::TextureAsset& asset, const char*& reason) {
    reason = nullptr;
    const XboxTextureColorFormat colorFormat = static_cast<XboxTextureColorFormat>(asset.ColorFormat);
    XboxTextureAlphaPrecision alphaPrecision = static_cast<XboxTextureAlphaPrecision>(asset.AlphaPrecision);
    const bool hasStableIdentity = !asset.Id.empty() || asset.RuntimeAssetId != 0 || !asset.AuthoringAssetId.empty();
    const bool isAnonymousFontCoverage =
        ShouldPreserveAnonymousRgbaAlpha(hasStableIdentity, colorFormat, alphaPrecision);
    if (isAnonymousFontCoverage) {
        // FontAssetBinarySerializer supplies the engine's generated default
        // font atlas as an anonymous RGBA texture. Its Opaque value is only
        // the TextureAsset default; alpha contains the glyph coverage mask.
        alphaPrecision = XboxTextureAlphaPrecision::A8;
    }
    const std::uint8_t* colors = asset.Colors != nullptr ? asset.Colors->Data : nullptr;
    const std::size_t colorsLength = asset.Colors != nullptr && asset.Colors->Length > 0
                                         ? static_cast<std::size_t>(asset.Colors->Length)
                                         : 0;
    const std::uint8_t* palette = asset.PaletteColors != nullptr ? asset.PaletteColors->Data : nullptr;
    const std::size_t paletteLength = asset.PaletteColors != nullptr && asset.PaletteColors->Length > 0
                                          ? static_cast<std::size_t>(asset.PaletteColors->Length)
                                          : 0;
    const XboxTextureSourceView source{asset.Width, asset.Height, colorFormat, alphaPrecision,
                                       colors, colorsLength, palette, paletteLength};

    XboxTextureLayout layout{};
    XboxTextureStatus status = BuildXboxTextureLayout(asset.Width, asset.Height, layout);
    if (status != XboxTextureStatus::Ok) {
        reason = TextureStatusReason(status, colorFormat);
        return nullptr;
    }

    std::size_t pixelCount = static_cast<std::size_t>(asset.Width) * asset.Height;
    XboxRgbaPixel* decoded = new (std::nothrow) XboxRgbaPixel[pixelCount];
    if (decoded == nullptr) {
        reason = "texture CPU decode allocation failed";
        return nullptr;
    }
    status = DecodeTextureToRgba8(source, decoded, pixelCount);
    if (status != XboxTextureStatus::Ok) {
        reason = TextureStatusReason(status, colorFormat);
        delete[] decoded;
        return nullptr;
    }
    if (isAnonymousFontCoverage) {
        // The NV2A linearly filters this atlas. White RGB in transparent texels
        // prevents coverage from darkening RGB before SRC_ALPHA blending.
        SetTransparentFontAtlasTexelsToWhite(decoded, pixelCount);
    }

    XboxRuntimeTexture* texture = new (std::nothrow) XboxRuntimeTexture();
    if (texture == nullptr) {
        reason = "runtime texture allocation failed";
        delete[] decoded;
        return nullptr;
    }
    texture->gpuMemory_ = MmAllocateContiguousMemoryEx(static_cast<SIZE_T>(layout.ByteCount), 0,
                                                       kGpuHighestAddress, 0,
                                                       PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (texture->gpuMemory_ == nullptr) {
        reason = "NV2A contiguous texture allocation failed";
        delete[] decoded;
        delete texture;
        return nullptr;
    }
    // Populate the GPU allocation directly. A full-size background must not
    // require an additional texture-sized upload buffer alongside its cooked
    // payload, decoded pixels and contiguous GPU storage.
    status = ConvertTextureToNv2a(source, decoded, layout,
        static_cast<std::uint8_t*>(texture->gpuMemory_), layout.ByteCount);
    delete[] decoded;
    if (status != XboxTextureStatus::Ok) {
        reason = TextureStatusReason(status, colorFormat);
        delete texture;
        return nullptr;
    }

    texture->layout_ = layout;
    texture->gpuOffset_ = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(texture->gpuMemory_) & kGpuAddressMask);
    texture->set_Id(asset.Id);
    texture->set_Width(static_cast<int32_t>(asset.Width));
    texture->set_Height(static_cast<int32_t>(asset.Height));
    texture->set_IsEngineOwned(asset.IsEngineOwned);
    return texture;
}

void XboxRuntimeTexture::Dispose() {
    if (disposed_) {
        return;
    }
    disposed_ = true;
    if (gpuMemory_ != nullptr) {
        MmFreeContiguousMemory(gpuMemory_);
        gpuMemory_ = nullptr;
        gpuOffset_ = 0;
    }
    ::RuntimeTexture::Dispose();
}

} // namespace helengine::xbox

#endif
