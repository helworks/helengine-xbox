#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Native texture built from one cooked TextureAsset.
//
// Owns validated texels and an optional native BGRA palette in Xbox GPU-visible memory.

#include "RuntimeTexture.hpp"
#include "TextureAsset.hpp"
#include "XboxTextureUpload.hpp"

namespace helengine::xbox {

class XboxRuntimeTexture final : public ::RuntimeTexture {
public:
    // Copies the borrowed asset's extent. The caller retains the asset.
    static XboxRuntimeTexture* Create(const ::TextureAsset& asset, const char*& reason);

    ~XboxRuntimeTexture() override;

    void Dispose() override;

    std::uint32_t GpuOffset() const { return gpuOffset_; }
    std::uint32_t PaletteGpuOffset() const { return paletteGpuOffset_; }
    const XboxTextureLayout& Layout() const { return layout_; }
    bool IsDisposedNative() const { return disposed_; }

private:
    XboxRuntimeTexture();

    void* gpuMemory_ = nullptr;
    std::uint32_t gpuOffset_ = 0;
    void* paletteGpuMemory_ = nullptr;
    std::uint32_t paletteGpuOffset_ = 0;
    XboxTextureLayout layout_{};
    bool disposed_ = false;
};

} // namespace helengine::xbox

#endif
