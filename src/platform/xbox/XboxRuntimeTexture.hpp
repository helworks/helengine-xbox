#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Native texture built from one cooked TextureAsset.
//
// This milestone does not draw 2D, so nothing is uploaded to the GPU: the
// texture carries its decoded extent and no pixels. That is not a shortcut
// around a missing feature, it is what scene loading actually needs. The
// authored cube_test scene contains sprites, and the scene loader resolves
// every sprite's texture while loading, long before anything is drawn -
// returning null there fails the load outright and the cube never appears,
// even though the cube itself needs no texture at all.
//
// When 2D rendering arrives, this is where the swizzled NV2A upload belongs.

#include "RuntimeTexture.hpp"
#include "TextureAsset.hpp"

namespace helengine::xbox {

class XboxRuntimeTexture final : public ::RuntimeTexture {
public:
    // Copies the borrowed asset's extent. The caller retains the asset.
    static XboxRuntimeTexture* Create(const ::TextureAsset& asset, const char*& reason);

    ~XboxRuntimeTexture() override;

    void Dispose() override;

private:
    XboxRuntimeTexture();

    bool disposed_ = false;
};

} // namespace helengine::xbox

#endif
