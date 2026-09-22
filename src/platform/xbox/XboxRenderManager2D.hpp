#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Generated 2D render manager adapter.
//
// Core::Initialize requires a 2D manager. This adapter owns accepted runtime
// textures and visits each camera's ordered 2D queue after the 3D pass,
// submitting supported sprites through the shared raster renderer.
//
// Nothing here throws.

#include "RenderManager2D.hpp"

namespace helengine::xbox { class XboxRasterRenderer; }

namespace helengine::xbox {

class XboxRenderManager2D final : public ::RenderManager2D {
public:
    XboxRenderManager2D();

    void Draw() override;
    void Dispose() override;
    void InstallRenderer(::helengine::xbox::XboxRasterRenderer* renderer);

    ::RuntimeTexture* BuildTextureFromCooked(std::string cookedAssetPath,
                                             ::IContentStreamSource* contentStreamSource) override;
    ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;

    void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;
    void DrawSprite(::ISpriteDrawable2D* sprite) override;
    void DrawText(::ITextDrawable2D* text) override;

    void UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x, int32_t y, int32_t width,
                                 int32_t height, Array<uint8_t>* rgba8,
                                 int32_t sourceRowPitch) override;

    // Number of unsupported 2D requests seen, and what the last one was.
    unsigned UnsupportedCalls() const { return unsupportedCalls_; }
    const char* LastUnsupported() const { return lastUnsupported_; }

    // Cooked textures decoded for scene loading. They carry an extent only.
    unsigned TexturesLoaded() const { return texturesLoaded_; }

private:
    void RecordUnsupported(const char* what);
    void SubmitSprite(::ISpriteDrawable2D* sprite);

    unsigned unsupportedCalls_ = 0;
    unsigned texturesLoaded_ = 0;
    const char* lastUnsupported_ = "";
    const char* reportedUnsupported_ = nullptr;
    bool disposed_ = false;
    ::helengine::xbox::XboxRasterRenderer* renderer_ = nullptr;
};

} // namespace helengine::xbox

#endif
