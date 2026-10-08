#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Generated 3D render manager adapter.
//
// Two jobs. Asset construction turns cooked models and materials into the
// native resources in XboxRuntimeModel/XboxRuntimeMaterial. Draw gathers the
// scene's cameras and drawables from the generated ObjectManager, builds the
// clip matrix for each, and submits them through XboxRasterRenderer.
//
// The generated base is a stub - Draw is empty and BuildModelFromCooked raises
// NotSupported - so everything that actually renders lives here.
//
// Nothing here throws. A malformed scene is reported once, with its reason,
// and the frame is skipped rather than half-drawn: a partially submitted frame
// would leave the depth buffer inconsistent and read as a rendering bug.

#include "XboxMath.hpp"

#include "RenderManager3D.hpp"
#include "XboxShaderRuntimeConfig.hpp"
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
#include "IShaderRenderManager3D.hpp"
#include "XboxShaderProgram.hpp"
#include <map>
#include <memory>
#include <vector>
#endif

class PlatformMaterialAsset;

namespace helengine::xbox {

class XboxRasterRenderer;

// Counts from the most recent Draw, so the host can report what was actually
// submitted instead of inferring it from the screen.
struct AuthoredFrameCounts {
    unsigned Cameras = 0;
    unsigned Drawables = 0;
    unsigned Submitted = 0;
    unsigned Triangles = 0;
    unsigned Skipped = 0;
    const char* LastSkipReason = "";
};

class XboxRenderManager3D final : public ::RenderManager3D
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    , public ::IShaderRenderManager3D
#endif
{
public:
    XboxRenderManager3D();

    // Installs the renderer the gathered frame is submitted through. Without
    // one, a nonempty scene fails rather than silently drawing nothing.
    void InstallRenderer(XboxRasterRenderer* renderer);

    // Back-buffer extent, used for the projection aspect ratio.
    void SetViewport(unsigned width, unsigned height);

    void Draw() override;

    void Dispose() override;

#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    ::ShaderCompileTarget get_ShaderCompileTarget() override;
    ::RuntimeMaterial* BuildMaterialFromRaw(::ShaderMaterialAsset* materialAsset, ::ShaderAsset* shaderAsset) override;
    ::RuntimeMaterial* BuildMaterialFromRawAsset(::ContentManager* content, std::string path) override;
    void InvalidateShaderResources(std::string shaderAssetId, ::ShaderAsset* shaderAsset) override;
#endif
    ::RuntimeMaterial* BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset)
#if !defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
        override
#endif
        ;

    ::RuntimeMaterial* BuildMaterialFromCooked(std::string cookedAssetPath,
                                               ::IContentStreamSource* contentStreamSource)
#if !defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
        override
#endif
        ;

    ::RuntimeModel* BuildModelFromCooked(std::string cookedAssetPath,
                                         ::IContentStreamSource* contentStreamSource) override;

    ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;

    void ReleaseMaterial(::RuntimeMaterial* material) override;

    void ReleaseModel(::RuntimeModel* model) override;

    const AuthoredFrameCounts& LastFrame() const { return lastFrame_; }
    unsigned PresentedFrames() const { return presentedFrames_; }

private:
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
    std::map<std::string, std::vector<std::weak_ptr<XboxShaderProgram>>> ShaderPrograms;
#endif
    // Records a skip reason on the current frame and returns false.
    bool Skip(const char* reason);

    XboxRasterRenderer* renderer_ = nullptr;
    unsigned viewportWidth_ = 0;
    unsigned viewportHeight_ = 0;
    AuthoredFrameCounts lastFrame_{};
    unsigned presentedFrames_ = 0;
    bool disposed_ = false;
    // Set once a skip reason has been logged, so a scene that fails every
    // frame reports once rather than sixty times a second.
    const char* reportedSkipReason_ = nullptr;
};

} // namespace helengine::xbox

#endif
