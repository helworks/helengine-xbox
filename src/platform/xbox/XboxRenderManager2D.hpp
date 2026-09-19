#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Generated 2D render manager adapter.
//
// Core::Initialize requires a 2D manager, and RenderManager2D has five pure
// virtuals, so one has to exist even though this milestone draws no 2D.
//
// Cooked texture construction is implemented, and has to be: the authored
// cube_test scene carries sprites, and the scene loader resolves every
// sprite's texture during loading. Refusing there fails the load outright and
// the 3D cube never appears, even though the cube needs no texture. The
// textures carry an extent and no pixels.
//
// The draw entries are reached only by a scene with 2D content. Each records
// that it was called rather than doing nothing quietly, so such a scene
// reports why nothing appeared instead of looking like a renderer fault.
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

    // Cooked textures decoded for scene loading. They carry an extent only.
    unsigned TexturesLoaded() const { return texturesLoaded_; }

private:
    void RecordUnsupported(const char* what);

    unsigned unsupportedCalls_ = 0;
    unsigned texturesLoaded_ = 0;
    const char* lastUnsupported_ = "";
    const char* reportedUnsupported_ = nullptr;
    bool disposed_ = false;
};

} // namespace helengine::xbox

#endif
