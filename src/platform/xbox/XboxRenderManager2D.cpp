#include "XboxRenderManager2D.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxCookedAsset.hpp"
#include "XboxDiagnostics.hpp"
#include "XboxRuntimeTexture.hpp"
#include "XboxRasterRenderer.hpp"
#include "XboxSpriteGeometry.hpp"
#include "XboxBatch2DProjection.hpp"

#include "CameraComponent.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "Batch2DGeometry.hpp"
#include "SpriteQuad2D.hpp"
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
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
#include "Batch2DBuffer.hpp"
#include "Batch2DVertex.hpp"
#include "Batch2DCameraProjection.hpp"
#include "CameraViewportResolver.hpp"
#include "ShaderAsset.hpp"
#include "ShaderBinaryAsset.hpp"
#include "ShaderAssetBinarySerializer.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "FontAsset.hpp"
#include "XboxRoundedBatch.hpp"
#include <cstring>
#include "system/io/stream.hpp"
#include "XboxCoreRuntimeProvider.hpp"
#include <algorithm>
#include <cstddef>
#endif

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

void XboxRenderManager2D::InstallRenderer(XboxRasterRenderer* renderer, ::IContentStreamSource* source) {
    renderer_ = renderer;
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    shaderSource_ = source;
#else
    (void)source;
#endif
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
    queuedLastFrame_ = spritesLastFrame_ = batchesLastFrame_ = roundedLastFrame_ = textLastFrame_ = 0;
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    DrawBatched();
    return;
#endif
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
    HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    // Scene loading resolves sprite textures before the render queue is drawn.
    // Keep the decoded pixels in GPU-visible memory; returning null here would
    // fail scene loading, while retaining only the extent would yield blank or
    // opaque-looking sprite quads.
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
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    if (!batchActive_ || !batchWriter_ || !shape) { RecordUnsupported("rounded outside shared batch pass"); return; }
    auto* owner = shape->get_Parent();
    if (!owner || !owner->get_IsHierarchyEnabled() || batchScissor_.Z <= 0 || batchScissor_.W <= 0) return;
    const auto size = shape->get_Size();
    if (size.X <= 0 || size.Y <= 0) return;
    const auto fill = shape->get_FillColor(), border = shape->get_BorderColor();
    XboxRoundedStyle style;
    style.Width = static_cast<unsigned>(size.X); style.Height = static_cast<unsigned>(size.Y);
    style.Radius = static_cast<float>(shape->get_Radius());
    style.Border = static_cast<float>(shape->get_BorderThickness());
    style.Corners = static_cast<unsigned>(shape->get_Corners());
    style.Fill = {fill.X,fill.Y,fill.Z,fill.W}; style.Outline = {border.X,border.Y,border.Z,border.W};
    if (!NormalizeRoundedStyle(style)) { RecordUnsupported("invalid rounded shape"); return; }
    const char* reason = nullptr;
    auto* texture = roundedCache_.Get(style, [&](const XboxRoundedStyle& normalized) {
        std::vector<XboxRgbaPixel> pixels;
        if (!RasterizeRounded(normalized, pixels)) return std::unique_ptr<XboxRuntimeTexture>();
        static_assert(sizeof(XboxRgbaPixel) == 4);
        ::TextureAsset asset;
        asset.Width = static_cast<uint16_t>(RoundedRasterExtent(normalized.Width));
        asset.Height = static_cast<uint16_t>(RoundedRasterExtent(normalized.Height));
        asset.ColorFormat = ::TextureAssetColorFormat::Rgba32;
        asset.AlphaPrecision = ::TextureAssetAlphaPrecision::A8;
        asset.Colors = new ::Array<uint8_t>(static_cast<int32_t>(pixels.size()*4));
        std::memcpy(asset.Colors->Data, pixels.data(), pixels.size()*4);
        std::unique_ptr<XboxRuntimeTexture> result(XboxRuntimeTexture::Create(asset, reason));
        asset.Dispose();
        return result;
    });
    if (!texture) { RecordUnsupported(reason ? reason : "rounded cache frame budget exhausted"); return; }
    const auto position = owner->get_Position();
    ::Batch2DRun run(::Batch2DVariant::Textured, texture,
        static_cast<int>(batchScissor_.X), static_cast<int>(batchScissor_.Y),
        static_cast<int>(batchScissor_.Z), static_cast<int>(batchScissor_.W));
    // Same position, size and Rotation contract as the shared EmitRoundedRect.
    if (!EmitRoundedTexturedQuad(*batchWriter_, run, position.X, position.Y, size.X, size.Y, shape->get_Rotation()))
        RecordUnsupported("invalid rounded transform");
    else ++roundedLastFrame_;
#else
    (void)shape;
    RecordUnsupported("rounded rect");
#endif
}

void XboxRenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    if (!batchActive_ || !batchEmitter_ || !sprite) { RecordUnsupported("sprite outside shared batch pass"); return; }
    ++spritesLastFrame_;
    batchEmitter_->EmitSprite(sprite, ::Batch2DRun(::Batch2DVariant::Textured, sprite->get_Texture(),
        static_cast<int>(batchScissor_.X), static_cast<int>(batchScissor_.Y),
        static_cast<int>(batchScissor_.Z), static_cast<int>(batchScissor_.W)));
#else
    SubmitSprite(sprite);
#endif
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
    const ::SpriteQuad2D geometry = ::Batch2DGeometry::ResolveSpriteQuad(sprite);
    const ::float4 source = sprite->get_SourceRect();
    const ::byte4 tint = sprite->get_Color();
    XboxSpriteInput input{};
    input.HasResolvedCorners = true;
    const ::float2 corners[]{geometry.TopLeft, geometry.TopRight, geometry.BottomRight, geometry.BottomLeft};
    for (unsigned i = 0; i < 4; ++i) {
        input.CornerX[i] = static_cast<float>(corners[i].X);
        input.CornerY[i] = static_cast<float>(corners[i].Y);
    }
    input.SourceX = static_cast<float>(source.X);
    input.SourceY = static_cast<float>(source.Y);
    input.SourceWidth = static_cast<float>(source.Z);
    input.SourceHeight = static_cast<float>(source.W);
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
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    if (!batchActive_ || !batchWriter_ || !batchEmitter_ || !text) {
        RecordUnsupported("text outside shared batch pass");
        return;
    }
    auto* owner = text->get_Parent();
    if (!owner || !owner->get_IsHierarchyEnabled()) return;
    if (text->get_Text().empty()) return;
    auto* font = text->get_Font();
    auto* atlas = font ? dynamic_cast<XboxRuntimeTexture*>(font->get_Texture()) : nullptr;
    if (!atlas || atlas->IsDisposedNative() || atlas->GpuOffset() == 0 ||
        atlas->Layout().RealWidth == 0 || atlas->Layout().RealHeight == 0) {
        RecordUnsupported("missing, foreign or invalid font atlas");
        return;
    }
    ::Batch2DRun run(::Batch2DVariant::Textured, atlas,
        static_cast<int>(batchScissor_.X), static_cast<int>(batchScissor_.Y),
        static_cast<int>(batchScissor_.Z), static_cast<int>(batchScissor_.W));
    batchEmitter_->EmitText(text, run);
    ++textLastFrame_;
#else
    (void)text;
    RecordUnsupported("text");
#endif
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

#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
void XboxRenderManager2D::DrawBatched() {
    if (disposed_ || !renderer_) return;
    // RuntimeHost calls Draw once per frame, after the previous EndFrame drain.
    roundedCache_.BeginFrame();
    auto* core = get_OwnerCore();
    auto* objects = core ? core->get_ObjectManager() : nullptr;
    auto* cameras = objects ? objects->get_Cameras() : nullptr;
    if (!cameras) return;
    bool hasEntries = false;
    for (int i = 0; i < cameras->get_Count(); ++i) {
        auto* camera = cameras->get_Item(i);
        auto* queue = camera ? camera->get_RenderQueue2D() : nullptr;
        if (queue && queue->get_Count() > 0) {
            hasEntries = true;
            queuedLastFrame_ += static_cast<unsigned>(queue->get_Count());
        }
    }
    if (!hasEntries) return;
    if (!batchWriter_) {
        const char* reason = nullptr;
        auto* stream = shaderSource_ ? shaderSource_->OpenRead("cooked/shaders/spritebatchshader.xbox.hasset") : nullptr;
        if (!stream) he_cpp_custom::Fail("Shared batch shader package is missing");
        auto* asset = ::ShaderAssetBinarySerializer::Deserialize(stream);
        stream->Close();
        stream->Dispose();
        delete stream;
        if (!asset) he_cpp_custom::Fail(reason ? reason : "Shared batch shader package is missing");
        ::ShaderBinaryAsset* vertex = nullptr;
        ::ShaderBinaryAsset* pixel = nullptr;
        bool valid = asset->TargetName == "xbox" && asset->Binaries != nullptr;
        if (valid) for (int i = 0; i < asset->Binaries->Length; ++i) {
            auto* binary = (*asset->Binaries)[i];
            if (!binary || binary->Variant != "Textured") continue;
            if (binary->TargetName != "xbox" || !binary->Bytecode || binary->Bytecode->Length <= 0) { valid = false; break; }
            if (binary->Stage == ::ShaderStage::Vertex && binary->ProgramName == "SpriteBatchShader.vs") {
                if (vertex) { valid = false; break; }
                vertex = binary;
            } else if (binary->Stage == ::ShaderStage::Pixel && binary->ProgramName == "SpriteBatchShader.ps") {
                if (pixel) { valid = false; break; }
                pixel = binary;
            }
        }
        auto program = std::make_shared<XboxShaderProgram>();
        valid = valid && vertex && pixel && program->Load(vertex->Bytecode->Data, vertex->Bytecode->Length,
            pixel->Bytecode->Data, pixel->Bytecode->Length, reason) && batchMaterial_.Initialize(program, "BatchCameraBuffer");
        asset->Dispose();
        ReleaseAssetStorage(asset);
        if (!valid) he_cpp_custom::Fail(reason ? reason : "Shared batch shader package is invalid");
        batchWriter_ = std::make_unique<::Batch2DWriter>(this, 128);
        batchEmitter_ = std::make_unique<::Batch2DDrawableEmitter>(batchWriter_.get(), this);
        Diagnostics::Log("shared Textured batch ready");
    }
    if (!renderer_->BeginSpritePass()) he_cpp_custom::Fail("Shared batch pass initialization failed");
    for (int i = 0; i < cameras->get_Count(); ++i) {
        auto* camera = cameras->get_Item(i);
        auto* queue = camera ? camera->get_RenderQueue2D() : nullptr;
        if (!queue || queue->get_Count() == 0) continue;
        if (camera->get_RenderTarget()) he_cpp_custom::Fail("Xbox batch render targets are unsupported");
        const auto resolved = ::CameraViewportResolver::ResolveViewport(camera->get_Viewport(), renderer_->Width(), renderer_->Height());
        const ::RealEvaluation left = ::RealMath::Ceiling(resolved.X - .5);
        const ::RealEvaluation top = ::RealMath::Ceiling(resolved.Y - .5);
        const ::float4 viewport(left, top,
            ::RealMath::Ceiling(resolved.X + resolved.Z - .5) - left,
            ::RealMath::Ceiling(resolved.Y + resolved.W - .5) - top);
        const auto projection = ::Batch2DCameraProjection::Create(viewport);
        batchWriter_->ConfigureCamera(projection, viewport);
        batchActive_ = true;
        queue->VisitOrdered(this);
        batchWriter_->Flush();
        batchActive_ = false;
    }
}

void XboxRenderManager2D::SetCamera(::float4x4 projection, ::float4 viewport) {
    if (!::RealEvaluation::IsFinite(viewport.X) || !::RealEvaluation::IsFinite(viewport.Y) ||
        !::RealEvaluation::IsFinite(viewport.Z) || !::RealEvaluation::IsFinite(viewport.W) ||
        viewport.X < 0 || viewport.Y < 0 || viewport.Z <= 0 || viewport.W <= 0 ||
        viewport.X + viewport.Z > renderer_->Width() || viewport.Y + viewport.W > renderer_->Height())
        he_cpp_custom::Fail("Invalid shared batch viewport");
    // Preserve Core rows here; the material packs the column-major HLSL buffer.
    const float rows[] = {
        static_cast<float>(projection.M11), static_cast<float>(projection.M12), static_cast<float>(projection.M13), static_cast<float>(projection.M14),
        static_cast<float>(projection.M21), static_cast<float>(projection.M22), static_cast<float>(projection.M23), static_cast<float>(projection.M24),
        static_cast<float>(projection.M31), static_cast<float>(projection.M32), static_cast<float>(projection.M33), static_cast<float>(projection.M34),
        static_cast<float>(projection.M41), static_cast<float>(projection.M42), static_cast<float>(projection.M43), static_cast<float>(projection.M44)};
    batchProjection_ = BatchProjectionForMaterial(rows);
    batchViewport_ = viewport;
}

void XboxRenderManager2D::Visit(::IDrawable2D* drawable) {
    if (!drawable) return;
    auto* owner = drawable->get_Parent();
    if (!owner || !owner->get_IsHierarchyEnabled()) return;
    ::RealEvaluation left = batchViewport_.X, top = batchViewport_.Y;
    ::RealEvaluation right = left + batchViewport_.Z, bottom = top + batchViewport_.W;
    clipChain_.clear();
    clipBuilder_.BuildClipChain(drawable, &clipChain_);
    for (auto* region : clipChain_) {
        if (!region) he_cpp_custom::Fail("Null batch clip region");
        auto clip = region->GetClipRect();
        if (!::RealEvaluation::IsFinite(clip.X) || !::RealEvaluation::IsFinite(clip.Y) ||
            !::RealEvaluation::IsFinite(clip.Z) || !::RealEvaluation::IsFinite(clip.W) || clip.Z < 0 || clip.W < 0)
            he_cpp_custom::Fail("Invalid batch clip region");
        left = ::RealMath::Max(left, clip.X); top = ::RealMath::Max(top, clip.Y);
        right = ::RealMath::Min(right, clip.X + clip.Z); bottom = ::RealMath::Min(bottom, clip.Y + clip.W);
    }
    left = ::RealMath::Clamp(::RealMath::Ceiling(left - .5), 0, renderer_->Width());
    top = ::RealMath::Clamp(::RealMath::Ceiling(top - .5), 0, renderer_->Height());
    right = ::RealMath::Clamp(::RealMath::Ceiling(right - .5), left, renderer_->Width());
    bottom = ::RealMath::Clamp(::RealMath::Ceiling(bottom - .5), top, renderer_->Height());
    batchScissor_ = ::float4(left, top, right - left, bottom - top);
    drawable->Draw();
}

void XboxRenderManager2D::Submit(::Batch2DBuffer* buffer, ::Batch2DRun run) {
    static_assert(sizeof(::Batch2DVertex) == 96, "Shared batch vertex ABI changed");
    static_assert(offsetof(::Batch2DVertex, Position) == 0 && offsetof(::Batch2DVertex, TexLocal) == 16 &&
        offsetof(::Batch2DVertex, Color) == 32, "Shared batch attribute offsets changed");
    if (!buffer || !buffer->Vertices || !buffer->Indices || buffer->VertexCount <= 0 || buffer->IndexCount <= 0 ||
        buffer->VertexCount > buffer->Vertices->Length || buffer->IndexCount > buffer->Indices->Length ||
        run.Variant != ::Batch2DVariant::Textured) he_cpp_custom::Fail("Invalid or unsupported shared batch submission");
    auto* texture = dynamic_cast<XboxRuntimeTexture*>(run.Texture);
    if (!texture) he_cpp_custom::Fail("Shared batch requires an Xbox texture");
    XboxBatch2DSubmission submission{reinterpret_cast<const float*>(buffer->Vertices->Data),
        static_cast<unsigned>(buffer->VertexCount), buffer->Indices->Data, static_cast<unsigned>(buffer->IndexCount),
        run.ScissorX, run.ScissorY, run.ScissorWidth, run.ScissorHeight};
    // SetCamera already validated these nonnegative pixel extents; Real exposes the signed integer boundary explicitly.
    if (!renderer_->DrawCompiledBatch(submission, batchMaterial_, batchProjection_, *texture,
        static_cast<unsigned>(static_cast<int32_t>(batchViewport_.X)), static_cast<unsigned>(static_cast<int32_t>(batchViewport_.Y)),
        static_cast<unsigned>(static_cast<int32_t>(batchViewport_.Z)), static_cast<unsigned>(static_cast<int32_t>(batchViewport_.W))))
        he_cpp_custom::Fail("Xbox rejected shared batch GPU submission");
    ++batchesLastFrame_;
}
#endif

void XboxRenderManager2D::Dispose() {
    if (disposed_) {
        return;
    }
    disposed_ = true;
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    batchEmitter_.reset();
    if (batchWriter_) batchWriter_->Dispose();
    batchWriter_.reset();
    roundedCache_.Clear();
    shaderSource_ = nullptr;
#endif
    renderer_ = nullptr;
    ::RenderManager2D::Dispose();
}

} // namespace helengine::xbox

#endif
