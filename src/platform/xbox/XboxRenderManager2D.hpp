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
#include "XboxShaderRuntimeConfig.hpp"
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
#include "Batch2DSink.hpp"
#include "IRenderVisitor2D.hpp"
#include "XboxShaderMaterial.hpp"
#include "XboxRoundedTextureCache.hpp"
#include "XboxRuntimeTexture.hpp"
#include "Batch2DWriter.hpp"
#include "Batch2DDrawableEmitter.hpp"
#include "ClipRegionStackBuilder2D.hpp"
#include "IClipRegion2D.hpp"
#include "runtime/native_list.hpp"
#include <memory>
#endif

namespace helengine::xbox { class XboxRasterRenderer; }

namespace helengine::xbox {

class XboxRenderManager2D final : public ::RenderManager2D
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    , public ::Batch2DSink, public ::IRenderVisitor2D
#endif
{
public:
    XboxRenderManager2D();

    void Draw() override;
    void Dispose() override;
    void InstallRenderer(::helengine::xbox::XboxRasterRenderer* renderer, ::IContentStreamSource* source = nullptr);

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
    unsigned QueuedLastFrame() const { return queuedLastFrame_; }
    unsigned SpritesLastFrame() const { return spritesLastFrame_; }
    unsigned BatchesLastFrame() const { return batchesLastFrame_; }
    unsigned RoundedLastFrame() const { return roundedLastFrame_; }
    unsigned TextLastFrame() const { return textLastFrame_; }

private:
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    void DrawBatched();
    void SetCamera(::float4x4 projection, ::float4 viewport) override;
    void Submit(::Batch2DBuffer* buffer, ::Batch2DRun run) override;
    void Visit(::IDrawable2D* drawable) override;
    std::unique_ptr<::Batch2DWriter> batchWriter_;
    std::unique_ptr<::Batch2DDrawableEmitter> batchEmitter_;
    XboxShaderMaterial batchMaterial_;
    Matrix4 batchProjection_{};
    ::float4 batchViewport_{};
    ::float4 batchScissor_{};
    ::ClipRegionStackBuilder2D clipBuilder_;
    ::List<::IClipRegion2D*> clipChain_;
    ::IContentStreamSource* shaderSource_ = nullptr;
    bool batchActive_ = false;
    XboxRoundedTextureCache<XboxRuntimeTexture> roundedCache_;
#endif
    void RecordUnsupported(const char* what);
    void SubmitSprite(::ISpriteDrawable2D* sprite);

    unsigned unsupportedCalls_ = 0;
    unsigned queuedLastFrame_ = 0;
    unsigned spritesLastFrame_ = 0;
    unsigned batchesLastFrame_ = 0;
    unsigned roundedLastFrame_ = 0;
    unsigned textLastFrame_ = 0;
    unsigned texturesLoaded_ = 0;
    const char* lastUnsupported_ = "";
    const char* reportedUnsupported_ = nullptr;
    bool disposed_ = false;
    ::helengine::xbox::XboxRasterRenderer* renderer_ = nullptr;
};

} // namespace helengine::xbox

#endif
