#include "XboxRuntimeTexture.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

namespace helengine::xbox {

XboxRuntimeTexture::XboxRuntimeTexture() : ::RuntimeTexture() {
}

XboxRuntimeTexture::~XboxRuntimeTexture() {
    Dispose();
}

XboxRuntimeTexture* XboxRuntimeTexture::Create(const ::TextureAsset& asset, const char*& reason) {
    reason = nullptr;
    if (asset.Width == 0 || asset.Height == 0) {
        reason = "texture has a zero extent";
        return nullptr;
    }

    XboxRuntimeTexture* texture = new XboxRuntimeTexture();
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
    ::RuntimeTexture::Dispose();
}

} // namespace helengine::xbox

#endif
