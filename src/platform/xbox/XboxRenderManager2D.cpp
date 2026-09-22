#include "XboxRenderManager2D.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxCookedAsset.hpp"
#include "XboxDiagnostics.hpp"
#include "XboxRuntimeTexture.hpp"
#include "XboxRasterRenderer.hpp"
#include "XboxSpriteGeometry.hpp"

#include "CameraComponent.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IDrawable2D.hpp"
#include "IRenderQueue2D.hpp"
#include "IRenderVisitor2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ObjectManager.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"
#include "byte4.hpp"
#include "runtime/array.hpp"
#include <cmath>
#include "TextureAsset.hpp"

namespace helengine::xbox {
namespace {

class SpriteQueueVisitor final : public ::IRenderVisitor2D {
public:
    void Visit(::IDrawable2D* drawable) override {
        if (drawable == nullptr) return;
        ::Entity* owner = drawable->get_Parent();
        if (owner == nullptr || !owner->get_IsHierarchyEnabled()) return;
        drawable->Draw();
    }
};

} // namespace

XboxRenderManager2D::XboxRenderManager2D() : ::RenderManager2D() {
}

void XboxRenderManager2D::InstallRenderer(XboxRasterRenderer* renderer) {
    renderer_ = renderer;
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
    ::Core* core = get_OwnerCore();
    ::ObjectManager* objects = core != nullptr ? core->get_ObjectManager() : nullptr;
    List<::ICamera*>* cameras = objects != nullptr ? objects->get_Cameras() : nullptr;
    if (cameras == nullptr) return;

    bool hasEntries = false;
    for (int32_t i = 0; i < cameras->get_Count(); ++i) {
        ::ICamera* camera = cameras->get_Item(i);
        ::IRenderQueue2D* queue = camera != nullptr ? camera->get_RenderQueue2D() : nullptr;
        if (queue != nullptr && queue->get_Count() > 0) hasEntries = true;
    }
    if (!hasEntries) return;
    if (renderer_ == nullptr) {
        RecordUnsupported("2d renderer not installed");
        return;
    }
    if (!renderer_->BeginSpritePass()) {
        RecordUnsupported("sprite pass initialization failed");
        return;
    }
    SpriteQueueVisitor visitor;
    for (int32_t i = 0; i < cameras->get_Count(); ++i) {
        ::ICamera* camera = cameras->get_Item(i);
        ::IRenderQueue2D* queue = camera != nullptr ? camera->get_RenderQueue2D() : nullptr;
        if (queue != nullptr && queue->get_Count() > 0) queue->VisitOrdered(&visitor);
    }
}

::RuntimeTexture* XboxRenderManager2D::BuildTextureFromCooked(
    std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    // Scene loading resolves every sprite's texture while loading, long before
    // anything is drawn, so returning null here fails the whole load - the 3D
    // cube never appears even though it needs no texture at all. The extent is
    // decoded and kept; no pixels reach the GPU, because this milestone draws
    // no 2D.
    const char* reason = nullptr;
    ::TextureAsset* asset =
        ReadCookedAssetAs<::TextureAsset>(cookedAssetPath, contentStreamSource, reason);
    if (asset == nullptr) {
        Diagnostics::Log("texture: %s (%s)", reason, cookedAssetPath.c_str());
        return nullptr;
    }
    XboxRuntimeTexture* texture = XboxRuntimeTexture::Create(*asset, reason);
    if (texture == nullptr) {
        Diagnostics::Log("texture: %s (%s)", reason, cookedAssetPath.c_str());
    } else {
        ++texturesLoaded_;
    }
    DestroyAsset(asset);
    return texture;
}

::RuntimeTexture* XboxRenderManager2D::BuildTextureFromRaw(::TextureAsset* data) {
    if (data == nullptr) {
        Diagnostics::Log("texture: raw texture asset is null");
        return nullptr;
    }
    const char* reason = nullptr;
    XboxRuntimeTexture* texture = XboxRuntimeTexture::Create(*data, reason);
    if (texture == nullptr) {
        Diagnostics::Log("texture: %s (raw)", reason != nullptr ? reason : "creation failed");
        return nullptr;
    }
    ++texturesLoaded_;
    return texture;
}

void XboxRenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D* shape) {
    (void)shape;
    RecordUnsupported("rounded rect");
}

void XboxRenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
    SubmitSprite(sprite);
}

void XboxRenderManager2D::SubmitSprite(::ISpriteDrawable2D* sprite) {
    if (renderer_ == nullptr) {
        RecordUnsupported("2d renderer not installed");
        return;
    }
    if (sprite == nullptr) {
        RecordUnsupported("null sprite");
        return;
    }
    ::Entity* owner = sprite->get_Parent();
    if (owner == nullptr || !owner->get_IsHierarchyEnabled()) return;
    XboxRuntimeTexture* texture = dynamic_cast<XboxRuntimeTexture*>(sprite->get_Texture());
    if (texture == nullptr) {
        RecordUnsupported("foreign sprite texture");
        return;
    }
    const XboxTextureLayout& layout = texture->Layout();
    if (texture->IsDisposedNative() || texture->GpuOffset() == 0 || layout.RealWidth == 0 || layout.RealHeight == 0) {
        RecordUnsupported("disposed or invalid sprite texture");
        return;
    }
    const ::float3 position = owner->get_Position();
    const ::float3 scale = owner->get_Scale();
    const ::float4 orientation = owner->get_Orientation();
    const ::int2 size = sprite->get_Size();
    const ::float4 source = sprite->get_SourceRect();
    const ::byte4 tint = sprite->get_Color();
    const bool explicitSize = size.X > 0 && size.Y > 0;
    XboxSpriteInput input{};
    input.X = position.X;
    input.Y = position.Y;
    input.Width = static_cast<float>(explicitSize ? size.X : static_cast<int32_t>(layout.RealWidth)) * scale.X;
    input.Height = static_cast<float>(explicitSize ? size.Y : static_cast<int32_t>(layout.RealHeight)) * scale.Y;
    input.RotationRadians = 2.0f * std::atan2(orientation.Z, orientation.W);
    input.SourceX = source.X;
    input.SourceY = source.Y;
    input.SourceWidth = source.Z;
    input.SourceHeight = source.W;
    input.RealTextureWidth = layout.RealWidth;
    input.RealTextureHeight = layout.RealHeight;
    input.StoredTextureWidth = layout.StorageWidth;
    input.StoredTextureHeight = layout.StorageHeight;
    input.Tint[0] = tint.X;
    input.Tint[1] = tint.Y;
    input.Tint[2] = tint.Z;
    input.Tint[3] = tint.W;
    XboxSpriteQuad quad{};
    if (TryBuildXboxSpriteQuad(input, quad) != XboxSpriteStatus::Ok) {
        RecordUnsupported("invalid sprite geometry");
        return;
    }
    if (!renderer_->DrawSprite(quad, *texture)) RecordUnsupported("sprite renderer rejected draw");
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
    renderer_ = nullptr;
    ::RenderManager2D::Dispose();
}

} // namespace helengine::xbox

#endif
