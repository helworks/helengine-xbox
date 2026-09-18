#include "XboxRenderManager3D.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxAuthoredTransform.hpp"
#include "XboxDiagnostics.hpp"
#include "XboxRasterRenderer.hpp"
#include "XboxRuntimeMaterial.hpp"
#include "XboxRuntimeModel.hpp"

#include <cmath>

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "CameraClearSettings.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable3D.hpp"
#include "ModelAsset.hpp"
#include "ObjectManager.hpp"
#include "PlatformMaterialAsset.hpp"
#include "float4.hpp"
#include "float4x4.hpp"
#include "runtime/array.hpp"
#include "system/io/stream.hpp"

namespace helengine::xbox {
namespace {

// The generated Asset base has no Dispose and no owned storage of its own
// beyond the former-id array, so releasing it is split from releasing whatever
// the concrete asset holds.
void ReleaseAssetStorage(::Asset* asset) {
    if (asset == nullptr) {
        return;
    }
    // Asset keeps a shared empty array for the common case; deleting that
    // would take the shared instance out from under every other asset, so only
    // an owned array is released here.
    if (asset->FormerAuthoringAssetIds != nullptr &&
        asset->FormerAuthoringAssetIds != ::Array<std::string>::Empty()) {
        delete asset->FormerAuthoringAssetIds;
    }
    asset->FormerAuthoringAssetIds = nullptr;
    delete asset; // Asset has a virtual destructor
}

// ModelAsset owns its position, normal and index arrays and releases them
// through a non-virtual Dispose; PlatformMaterialAsset holds only value fields.
void DestroyAsset(::ModelAsset* asset) {
    if (asset == nullptr) {
        return;
    }
    asset->Dispose();
    ReleaseAssetStorage(asset);
}

void DestroyAsset(::PlatformMaterialAsset* asset) {
    ReleaseAssetStorage(asset);
}

// Used on the type-mismatch path, where only the base type is known. The
// concrete type is recovered so an asset holding arrays still releases them.
void DestroyUnknownAsset(::Asset* asset) {
    if (::ModelAsset* model = dynamic_cast<::ModelAsset*>(asset)) {
        DestroyAsset(model);
        return;
    }
    ReleaseAssetStorage(asset);
}

void CloseStream(::Stream* stream) {
    if (stream == nullptr) {
        return;
    }
    stream->Close();
    stream->Dispose();
    delete stream;
}

// Reads one cooked asset and narrows it to the expected generated type. The
// dynamic_cast is why this platform needs RTTI and its own __RTDynamicCast.
// Returns null and sets reason; never throws.
template <typename TAsset>
TAsset* DeserializeAsset(const std::string& path,
                         ::IContentStreamSource* source,
                         const char*& reason) {
    reason = nullptr;
    if (source == nullptr) {
        reason = "content stream source is null";
        return nullptr;
    }
    ::Stream* stream = source->OpenRead(path);
    if (stream == nullptr) {
        // The source has already logged the concrete file and failure.
        reason = "content stream source returned no stream";
        return nullptr;
    }
    ::Asset* asset = ::AssetSerializer::Deserialize(stream);
    CloseStream(stream);
    if (asset == nullptr) {
        reason = "cooked asset failed to deserialize";
        return nullptr;
    }
    TAsset* typed = dynamic_cast<TAsset*>(asset);
    if (typed == nullptr) {
        DestroyUnknownAsset(asset);
        reason = "cooked asset has an unexpected generated type";
        return nullptr;
    }
    return typed;
}

// Reads an entity's world transform into this platform's convention.
bool TryEntityWorldMatrix(::Entity* entity, Matrix4& output) {
    if (entity == nullptr) {
        return false;
    }
    output = MatrixFromGenerated(entity->get_WorldTransformMatrix());
    return true;
}

} // namespace

XboxRenderManager3D::XboxRenderManager3D() : ::RenderManager3D() {
}

void XboxRenderManager3D::InstallRenderer(XboxRasterRenderer* renderer) {
    renderer_ = renderer;
}

void XboxRenderManager3D::SetViewport(unsigned width, unsigned height) {
    viewportWidth_ = width;
    viewportHeight_ = height;
}

bool XboxRenderManager3D::Skip(const char* reason) {
    ++lastFrame_.Skipped;
    lastFrame_.LastSkipReason = reason;
    // A scene that fails every frame would otherwise fill the log at frame
    // rate, so each distinct reason is reported once.
    if (reportedSkipReason_ != reason) {
        reportedSkipReason_ = reason;
        Diagnostics::Log("draw skipped: %s", reason);
    }
    return false;
}

void XboxRenderManager3D::Draw() {
    lastFrame_ = AuthoredFrameCounts{};

    if (renderer_ == nullptr) {
        Skip("no renderer installed");
        return;
    }
    if (viewportWidth_ == 0 || viewportHeight_ == 0) {
        Skip("viewport extent is unset");
        return;
    }
    ::Core* core = get_OwnerCore();
    if (core == nullptr) {
        Skip("render manager has no owning core");
        return;
    }
    ::ObjectManager* objects = core->get_ObjectManager();
    if (objects == nullptr) {
        Skip("core has no object manager");
        return;
    }
    List<::ICamera*>* cameras = objects->get_Cameras();
    List<::IDrawable3D*>* drawables = objects->get_Drawables3D();
    if (cameras == nullptr || drawables == nullptr) {
        Skip("object manager exposed no camera or drawable list");
        return;
    }
    lastFrame_.Cameras = static_cast<unsigned>(cameras->get_Count());
    lastFrame_.Drawables = static_cast<unsigned>(drawables->get_Count());
    if (lastFrame_.Cameras == 0) {
        Skip("scene has no camera");
        return;
    }
    if (lastFrame_.Drawables == 0) {
        // Not an error: an empty scene is a valid state, and the clear alone
        // is the correct frame for it.
        return;
    }
    if (!renderer_->BeginAuthoredFrame()) {
        Skip("renderer rejected the frame");
        return;
    }

    const float aspect = static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_);
    for (int32_t cameraIndex = 0; cameraIndex < cameras->get_Count(); ++cameraIndex) {
        ::ICamera* camera = cameras->get_Item(cameraIndex);
        if (camera == nullptr) {
            Skip("camera entry is null");
            continue;
        }
        Matrix4 cameraWorld{};
        if (!TryEntityWorldMatrix(camera->get_Parent(), cameraWorld)) {
            Skip("camera has no parent entity");
            continue;
        }
        Matrix4 view{};
        if (!TryViewFromCameraWorld(cameraWorld, view)) {
            Skip("camera transform is not a rigid motion");
            continue;
        }
        Matrix4 projection{};
        if (!TryPerspective(camera->get_FieldOfView(), aspect, camera->get_NearPlaneDistance(),
                            camera->get_FarPlaneDistance(), projection)) {
            Skip("camera projection parameters are out of range");
            continue;
        }
        const Matrix4 viewProjection = Multiply(projection, view);

        for (int32_t drawableIndex = 0; drawableIndex < drawables->get_Count(); ++drawableIndex) {
            ::IDrawable3D* drawable = drawables->get_Item(drawableIndex);
            if (drawable == nullptr) {
                Skip("drawable entry is null");
                continue;
            }
            Matrix4 world{};
            if (!TryEntityWorldMatrix(drawable->get_Parent(), world)) {
                Skip("drawable has no parent entity");
                continue;
            }
            if (!ValidateModelTransform(world)) {
                Skip("drawable transform is degenerate");
                continue;
            }
            XboxRuntimeModel* model = dynamic_cast<XboxRuntimeModel*>(drawable->get_Model());
            if (model == nullptr) {
                Skip("drawable has no native model");
                continue;
            }
            Array<::RuntimeMaterial*>* materials = drawable->get_Materials();
            XboxRuntimeMaterial* material = nullptr;
            if (materials != nullptr && materials->get_Length() > 0) {
                material = dynamic_cast<XboxRuntimeMaterial*>((*materials)[0]);
            }
            if (material == nullptr) {
                Skip("drawable has no native material");
                continue;
            }
            const float* color = material->Color();
            model->EnsureColor(color[0], color[1], color[2], color[3]);

            const Matrix4 clip = Multiply(viewProjection, world);
            if (!renderer_->DrawAuthored(clip, model->GpuVertices(), model->VertexCount(),
                                         model->Indices(), model->IndexCount())) {
                Skip("renderer rejected an authored mesh");
                continue;
            }
            ++lastFrame_.Submitted;
            lastFrame_.Triangles += model->IndexCount() / 3u;
        }
    }

    if (lastFrame_.Submitted != 0) {
        ++presentedFrames_;
    }
}

::RuntimeMaterial* XboxRenderManager3D::BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) {
    if (materialAsset == nullptr) {
        Diagnostics::Log("material: null platform asset");
        return nullptr;
    }
    const char* reason = nullptr;
    XboxRuntimeMaterial* material = XboxRuntimeMaterial::Create(*materialAsset, reason);
    if (material == nullptr) {
        Diagnostics::Log("material: %s (%s)", reason, materialAsset->Id.c_str());
    }
    return material;
}

::RuntimeMaterial* XboxRenderManager3D::BuildMaterialFromCooked(
    std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    const char* reason = nullptr;
    ::PlatformMaterialAsset* asset =
        DeserializeAsset<::PlatformMaterialAsset>(cookedAssetPath, contentStreamSource, reason);
    if (asset == nullptr) {
        Diagnostics::Log("material: %s (%s)", reason, cookedAssetPath.c_str());
        return nullptr;
    }
    ::RuntimeMaterial* runtime = BuildMaterialFromCooked(asset);
    DestroyAsset(asset);
    return runtime;
}

::RuntimeModel* XboxRenderManager3D::BuildModelFromCooked(
    std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    const char* reason = nullptr;
    ::ModelAsset* asset = DeserializeAsset<::ModelAsset>(cookedAssetPath, contentStreamSource, reason);
    if (asset == nullptr) {
        Diagnostics::Log("model: %s (%s)", reason, cookedAssetPath.c_str());
        return nullptr;
    }
    XboxRuntimeModel* model = XboxRuntimeModel::Create(*asset, reason);
    if (model == nullptr) {
        Diagnostics::Log("model: %s (%s)", reason, cookedAssetPath.c_str());
    }
    // The native model copies what it needs, so the cooked asset does not
    // outlive this call.
    DestroyAsset(asset);
    return model;
}

::RuntimeModel* XboxRenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
    // Raw construction would bypass the cooker, whose output this platform's
    // conversion is written against. Refuse it rather than accept an asset in
    // an unvalidated shape.
    (void)data;
    Diagnostics::Log("model: raw construction is unsupported; use cooked assets");
    return nullptr;
}

void XboxRenderManager3D::ReleaseMaterial(::RuntimeMaterial* material) {
    if (material == nullptr) {
        return;
    }
    material->Dispose();
    delete material;
}

void XboxRenderManager3D::ReleaseModel(::RuntimeModel* model) {
    if (model == nullptr) {
        return;
    }
    model->Dispose();
    delete model;
}

void XboxRenderManager3D::Dispose() {
    if (disposed_) {
        return;
    }
    disposed_ = true;
    renderer_ = nullptr;
    ::RenderManager3D::Dispose();
}

} // namespace helengine::xbox

#endif
