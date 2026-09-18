#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Native material built from one cooked PlatformMaterialAsset.
//
// This milestone renders unlit, opaque, single-colour geometry, so the only
// state carried is the base colour. The contract the cooker writes is checked
// rather than assumed: a textured, translucent, double-sided or vertex-coloured
// material would silently render as flat opaque white otherwise, which looks
// like a working renderer rather than an unimplemented feature.
//
// Nothing here throws. Create returns null and sets reason.

#include "PlatformMaterialAsset.hpp"
#include "RuntimeMaterial.hpp"

namespace helengine::xbox {

class XboxRuntimeMaterial final : public ::RuntimeMaterial {
public:
    // Renderer family the Xbox builder's material cooker stamps into the asset.
    static constexpr const char* RendererFamilyId = "xbox-standard";

    // Copies and validates the borrowed asset. The caller retains the asset.
    static XboxRuntimeMaterial* Create(const ::PlatformMaterialAsset& asset, const char*& reason);

    ~XboxRuntimeMaterial() override;

    void Dispose() override;

    const float* Color() const { return color_; }

private:
    XboxRuntimeMaterial();

    float color_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool disposed_ = false;
};

} // namespace helengine::xbox

#endif
