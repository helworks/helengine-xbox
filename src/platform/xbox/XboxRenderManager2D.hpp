#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Generated 2D render manager adapter.
//
// Core::Initialize requires a 2D manager, and RenderManager2D has five pure
// virtuals, so one has to exist even though this milestone draws no 2D. The
// authored cube_test scene holds no 2D drawables, so none of the draw entries
// is reached; each records that it was called rather than doing nothing
// quietly, so a scene that does contain 2D content reports why nothing
// appeared instead of looking like a renderer fault.
//
// Nothing here throws.

#include "RenderManager2D.hpp"

namespace helengine::xbox {

class XboxRenderManager2D final : public ::RenderManager2D {
public:
    XboxRenderManager2D();

    void Draw() override;
    void Dispose() override;

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

private:
    void RecordUnsupported(const char* what);

    unsigned unsupportedCalls_ = 0;
    const char* lastUnsupported_ = "";
    const char* reportedUnsupported_ = nullptr;
    bool disposed_ = false;
};

} // namespace helengine::xbox

#endif
