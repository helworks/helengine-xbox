#include "XboxRenderManager3D.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxAuthoredTransform.hpp"
#include "XboxCookedAsset.hpp"
#include "XboxDiagnostics.hpp"
#include "XboxLighting.hpp"
#include "XboxMaterialFormat.hpp"
#include "XboxRasterRenderer.hpp"
#include "XboxRuntimeMaterial.hpp"
#include "XboxRuntimeModel.hpp"
#include "XboxRuntimeTexture.hpp"
#include "XboxShaderRuntimeMaterial.hpp"
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
#include "ShaderRuntimeMaterialLoader.hpp"
#include "ShaderMaterialAsset.hpp"
#include "ShaderAsset.hpp"
#include "ContentManager.hpp"
#include "system/io/stream.hpp"
#endif

#include <cmath>

#include "CameraClearSettings.hpp"
#include "Core.hpp"
#include "DirectionalLightComponent.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IDrawable3D.hpp"
#include "ModelAsset.hpp"
#include "ObjectManager.hpp"
#include "PlatformMaterialAsset.hpp"
#include "float4.hpp"
#include "float4x4.hpp"
#include "runtime/array.hpp"

namespace helengine::xbox {
namespace {

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
    List<::DirectionalLightComponent*>* directionalLights = objects->get_DirectionalLights();
    float lightDirection[3]{};
    float lightColor[3] = {1.0f, 1.0f, 1.0f};
    float lightIntensity = 0.0f;
    if (directionalLights != nullptr && directionalLights->get_Count() > 0) {
        ::DirectionalLightComponent* light = directionalLights->get_Item(0);
        Matrix4 lightWorld{};
        if (light != nullptr && TryEntityWorldMatrix(light->get_Parent(), lightWorld)) {
            // Authored directional lights emit along their owner's local -Z.
            lightDirection[0] = -lightWorld.Values[2];
            lightDirection[1] = -lightWorld.Values[6];
            lightDirection[2] = -lightWorld.Values[10];
            const ::float4 color = light->get_Color();
            lightColor[0] = color.X;
            lightColor[1] = color.Y;
            lightColor[2] = color.Z;
            lightIntensity = light->get_Intensity();
        }
    }
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
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
            XboxShaderRuntimeMaterial* material = nullptr;
            if (materials != nullptr && materials->get_Length() > 0)
                material = dynamic_cast<XboxShaderRuntimeMaterial*>((*materials)[0]);
            if (material != nullptr) {
                const char* reason = nullptr;
                if (!material->Prepare(reason)) {
                    Skip(reason == nullptr ? "drawable has no compiled material" : reason);
                    continue;
                }
                XboxRuntimeTexture* texture = nullptr;
                if (material->RequiresTexture()) {
                    texture = dynamic_cast<XboxRuntimeTexture*>(material->ResolveTexture());
                    if (texture == nullptr || texture->IsDisposedNative() || !model->HasTextureCoordinates()) {
                        Skip("compiled mesh requires a live native texture and authored UVs");
                        continue;
                    }
                }
                model->EnsureColor(1, 1, 1, 1);
                const Matrix4 clip = Multiply(viewProjection, world);
                if (!renderer_->DrawAuthored(clip, model->GpuVertices(), model->VertexCount(),
                                             model->Indices(), model->IndexCount(), &material->NativeMaterial(), texture)) {
                    Skip("renderer rejected compiled authored mesh");
                    continue;
                }
            } else
#endif
            {
                XboxRuntimeMaterial* material = nullptr;
                if (materials != nullptr && materials->get_Length() > 0) {
                    material = dynamic_cast<XboxRuntimeMaterial*>((*materials)[0]);
                }
                if (material == nullptr) {
                    Skip("drawable has no native material");
                    continue;
                }
                const float* color = material->Color();
                bool lightingReady = true;
                if (lightIntensity > 0.0f) {
                    lightingReady = model->ApplyDirectionalLighting(color[0], color[1], color[2], color[3], world,
                                                                    lightDirection, lightColor, lightIntensity);
                } else {
                    model->EnsureColor(color[0], color[1], color[2], color[3]);
                }
                if (!lightingReady) {
                    Skip("model normal could not be transformed");
                    continue;
                }

                const Matrix4 clip = Multiply(viewProjection, world);
                if (!renderer_->DrawAuthored(clip, model->GpuVertices(), model->VertexCount(),
                                             model->Indices(), model->IndexCount())) {
                    Skip("renderer rejected an authored mesh");
                    continue;
                }
            }
            ++lastFrame_.Submitted;
            lastFrame_.Triangles += model->IndexCount() / 3u;
        }
    }

    if (lastFrame_.Submitted != 0) {
        ++presentedFrames_;
    }
}

#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
::ShaderCompileTarget XboxRenderManager3D::get_ShaderCompileTarget() { return ::ShaderCompileTarget::Xbox; }
::RuntimeMaterial* XboxRenderManager3D::BuildMaterialFromRaw(::ShaderMaterialAsset* materialAsset, ::ShaderAsset* shaderAsset) {
    if (materialAsset == nullptr || shaderAsset == nullptr) return nullptr;
    const char* reason = nullptr;
    auto* material = XboxShaderRuntimeMaterial::Create(*materialAsset, *shaderAsset, reason);
    if (material == nullptr) {
        Diagnostics::Log("shader material rejected: %s", reason);
    } else {
        ShaderPrograms[materialAsset->ShaderAssetId.raw()].push_back(material->SharedProgram());
        Diagnostics::Log("shader material loaded: %s", materialAsset->Id.c_str());
    }
    return material;
}
::RuntimeMaterial* XboxRenderManager3D::BuildMaterialFromRawAsset(::ContentManager* content, HeCppString path) {
    if (content == nullptr || content->get_ContentStreamSource() == nullptr) return nullptr;
    ::Stream* stream = content->get_ContentStreamSource()->OpenRead(path);
    if (stream == nullptr) return nullptr;
    uint8_t header[12]{};
    size_t length = 0;
    while (length < sizeof(header)) {
        int value = stream->ReadByte();
        if (value < 0) break;
        header[length++] = static_cast<uint8_t>(value);
    }
    stream->Close();
    stream->Dispose();
    delete stream;
    switch (ClassifyMaterialHeader(header, length)) {
        case XboxMaterialFormat::Classic:
            return BuildMaterialFromCooked(path, content->get_ContentStreamSource());
        case XboxMaterialFormat::Shader:
            return ::ShaderRuntimeMaterialLoader::BuildMaterialFromRawAsset(this, content, path);
        default:
            Diagnostics::Log("material: unsupported packaged material format (%s)", path.c_str());
            return nullptr;
    }
}
void XboxRenderManager3D::InvalidateShaderResources(HeCppString shaderAssetId, ::ShaderAsset*) {
    auto found = ShaderPrograms.find(shaderAssetId.raw());
    if (found == ShaderPrograms.end()) return;
    for (auto& reference : found->second) {
        auto program = reference.lock();
        if (program) {
            const char* reason = nullptr;
            program->Load(nullptr, 0, nullptr, 0, reason);
        }
    }
    ShaderPrograms.erase(found);
}
#endif
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
    HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    const char* reason = nullptr;
    ::PlatformMaterialAsset* asset =
        ReadCookedAssetAs<::PlatformMaterialAsset>(cookedAssetPath, contentStreamSource, reason);
    if (asset == nullptr) {
        Diagnostics::Log("material: %s (%s)", reason, cookedAssetPath.c_str());
        return nullptr;
    }
    ::RuntimeMaterial* runtime = BuildMaterialFromCooked(asset);
    DestroyAsset(asset);
    return runtime;
}

::RuntimeModel* XboxRenderManager3D::BuildModelFromCooked(
    HeCppString cookedAssetPath, ::IContentStreamSource* contentStreamSource) {
    const char* reason = nullptr;
    ::ModelAsset* asset = ReadCookedAssetAs<::ModelAsset>(cookedAssetPath, contentStreamSource, reason);
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
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    // The shader-backed generated resolver deserializes the packaged ModelAsset
    // before calling this API. Reuse the cooked path's full validation and copy;
    // the resolver releases its borrowed source immediately after we return.
    if (data == nullptr) {
        Diagnostics::Log("model: missing deserialized asset");
        return nullptr;
    }
    const char* reason = nullptr;
    XboxRuntimeModel* model = XboxRuntimeModel::Create(*data, reason);
    if (model == nullptr) Diagnostics::Log("model: %s (%s)", reason, data->Id.c_str());
    return model;
#else
    // Raw construction would bypass the cooker, whose output this platform's
    // conversion is written against. Refuse it rather than accept an asset in
    // an unvalidated shape.
    (void)data;
    Diagnostics::Log("model: raw construction is unsupported; use cooked assets");
    return nullptr;
#endif
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
