#include "XboxRenderManager2D.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxDiagnostics.hpp"

namespace helengine::xbox {

XboxRenderManager2D::XboxRenderManager2D() : ::RenderManager2D() {
}

void XboxRenderManager2D::RecordUnsupported(const char* what) {
    ++unsupportedCalls_;
    lastUnsupported_ = what;
    // Reported once per distinct entry: a scene with 2D content would
    // otherwise log at frame rate.
    if (reportedUnsupported_ != what) {
        reportedUnsupported_ = what;
        Diagnostics::Log("2d unsupported: %s", what);
    }
}

void XboxRenderManager2D::Draw() {
    // Reached every frame with nothing queued, which is the expected state for
    // this milestone, so it is silent.
}

::RuntimeTexture* XboxRenderManager2D::BuildTextureFromCooked(
    std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    (void)cookedAssetPath;
    (void)contentStreamSource;
    RecordUnsupported("cooked texture");
    return nullptr;
}

::RuntimeTexture* XboxRenderManager2D::BuildTextureFromRaw(::TextureAsset* data) {
    (void)data;
    RecordUnsupported("raw texture");
    return nullptr;
}

void XboxRenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D* shape) {
    (void)shape;
    RecordUnsupported("rounded rect");
}

void XboxRenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
    (void)sprite;
    RecordUnsupported("sprite");
}

void XboxRenderManager2D::DrawText(::ITextDrawable2D* text) {
    (void)text;
    RecordUnsupported("text");
}

void XboxRenderManager2D::UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x, int32_t y,
                                                  int32_t width, int32_t height,
                                                  Array<uint8_t>* rgba8, int32_t sourceRowPitch) {
    (void)texture;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)rgba8;
    (void)sourceRowPitch;
    RecordUnsupported("texture region update");
}

void XboxRenderManager2D::Dispose() {
    if (disposed_) {
        return;
    }
    disposed_ = true;
    ::RenderManager2D::Dispose();
}

} // namespace helengine::xbox

#endif
