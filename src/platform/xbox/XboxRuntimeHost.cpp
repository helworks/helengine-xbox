#include "XboxRuntimeHost.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxContentStreamSource.hpp"
#include "XboxDiagnostics.hpp"
#include "XboxInputBackend.hpp"
#include "XboxRasterRenderer.hpp"
#include "XboxRenderManager2D.hpp"
#include "XboxRenderManager3D.hpp"
#include "XboxVideo.hpp"

#include <cstdio>

#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "PlatformInfo.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "SceneLoadMode.hpp"
#include "SceneManager.hpp"
#include "ObjectManager.hpp"
#include "Entity.hpp"
#include "TextComponent.hpp"
#include "FontAsset.hpp"
#include "TextLayoutAlignmentUtils.hpp"
#include "runtime/array.hpp"
#include "runtime/native_list.hpp"
#include "system/math.hpp"
#include "runtime/runtime_scene_catalog_manifest.hpp"
#include "runtime/runtime_startup_manifest.hpp"

#include <windows.h>

namespace helengine::xbox {

// Host-visible, bounded samples from the same geometry and snapping used by
// Batch2DTextEmitter. Captured once after viewport layout and title updates;
// the entities retain ownership of every component inspected here.
struct TitleRightSample {
    float FontScale;
    float OutlineScale;
    float RightEffect;
    float BoxWidth;
    float WorldX;
    float LocalX;
    float ParentAnchor;
    float VisibleWidth;
    float LineOffset;
    float SnappedBase;
    float PaintedRight;
    float Margin;
    uint32_t Alignment;
};

TitleRightSample g_titleRightSamples[2]{};
uint32_t g_titleRightSampleMask = 0;

namespace {

void CaptureTitleRightSamples(::Core* core) {
    auto* entities = core->get_ObjectManager()->get_Entities();
    for (int32_t entityIndex = 0; entities != nullptr && entityIndex < entities->get_Count(); ++entityIndex) {
        auto* entity = entities->get_Item(entityIndex);
        auto* components = entity == nullptr ? nullptr : entity->get_Components();
        for (int32_t componentIndex = 0; components != nullptr && componentIndex < components->get_Count(); ++componentIndex) {
            auto* text = dynamic_cast<::TextComponent*>(components->get_Item(componentIndex));
            const uint32_t titleBit = text == nullptr ? 0 : text->get_Text() == "HELENGINE" ? 1 : text->get_Text() == "DEMO DISC" ? 2 : 0;
            if (titleBit == 0 || (g_titleRightSampleMask & titleBit) != 0 || text->get_Font() == nullptr || entity->get_Parent() == nullptr) {
                continue;
            }
            const auto position = entity->get_Position();
            const auto shadow = text->get_ShadowOffset();
            const auto size = text->get_Size();
            const ::RealEvaluation width = ::TextLayoutAlignmentUtils::MeasureVisibleLineWidth(text->get_Text(), text->get_Font(), text->get_FontScale(), text->get_Font()->get_AtlasWidth());
            const ::RealEvaluation effect = ::RealMath::Max(text->get_OutlineScale(), ::RealMath::Max(0, shadow.X));
            const ::RealEvaluation lineOffset = ::TextLayoutAlignmentUtils::ResolveHorizontalOffset(text->get_Alignment(), size.X, width);
            const ::RealEvaluation snappedBase = ::RealMath::Round(position.X);
            const ::RealEvaluation paintedRight = snappedBase + lineOffset + width + effect;
            const ::RealEvaluation parentAnchor = entity->get_Parent()->get_Position().X;
            auto& sample = g_titleRightSamples[titleBit == 1 ? 0 : 1];
            sample = {static_cast<float>(text->get_FontScale()), static_cast<float>(text->get_OutlineScale()), static_cast<float>(effect), static_cast<float>(size.X), static_cast<float>(position.X), static_cast<float>(entity->get_LocalPosition().X),
                      static_cast<float>(parentAnchor), static_cast<float>(width), static_cast<float>(lineOffset), static_cast<float>(snappedBase),
                      static_cast<float>(paintedRight), static_cast<float>(parentAnchor - paintedRight), static_cast<uint32_t>(text->get_Alignment())};
            g_titleRightSampleMask |= titleBit;
        }
    }
}

// Same background the diagnostic probe clears to, kept so a frame that renders
// nothing is visibly the same "nothing" as before rather than a new colour to
// interpret. A camera's own clear colour is scene state and is not read here;
// this milestone clears through video, not through the camera.
constexpr unsigned kClearColorArgb = 0xFF203040;

// Bounded wall-clock delta. The authored scene advances on real time, so a
// long frame must not take a component through an absurd step; the engine's
// own schedulers clamp similarly.
constexpr double kMaxFrameSeconds = 0.25;
constexpr double kDefaultFrameSeconds = 1.0 / 60.0;

double SecondsBetween(LARGE_INTEGER now, LARGE_INTEGER previous, LARGE_INTEGER frequency) {
    if (frequency.QuadPart <= 0 || now.QuadPart <= previous.QuadPart) {
        return kDefaultFrameSeconds;
    }
    const double delta = static_cast<double>(now.QuadPart - previous.QuadPart) /
                         static_cast<double>(frequency.QuadPart);
    return delta > kMaxFrameSeconds ? kMaxFrameSeconds : delta;
}

// Owns the catalog and the entries it was built from. RuntimeSceneCatalog does
// not take ownership of the entry objects, so both are released here.
struct SceneCatalogHandle {
    ::RuntimeSceneCatalog* Catalog = nullptr;
    Array<::RuntimeSceneCatalogEntry*>* Entries = nullptr;

    void Release() {
        if (Catalog != nullptr) {
            delete Catalog;
            Catalog = nullptr;
        }
        if (Entries != nullptr) {
            for (int32_t index = 0; index < Entries->get_Length(); ++index) {
                delete (*Entries)[index];
                (*Entries)[index] = nullptr;
            }
            delete Entries;
            Entries = nullptr;
        }
    }
};

// Builds the catalog from the manifest codegen already emits into the
// generated Core. Dreamcast supplies an equivalent through a builder-generated
// C ABI; that predates this manifest and is not needed here.
bool TryBuildSceneCatalog(SceneCatalogHandle& handle, const char*& reason) {
    std::size_t count = 0;
    const HERuntimeSceneCatalogEntry* entries = he_runtime_scene_catalog_entries(&count);
    if (entries == nullptr || count == 0) {
        reason = "generated scene catalog manifest is empty";
        return false;
    }
    handle.Entries = new Array<::RuntimeSceneCatalogEntry*>(static_cast<int32_t>(count));
    for (std::size_t index = 0; index < count; ++index) {
        const HERuntimeSceneCatalogEntry& entry = entries[index];
        if (entry.SceneId == nullptr || entry.SceneId[0] == '\0' ||
            entry.CookedRelativePath == nullptr || entry.CookedRelativePath[0] == '\0') {
            reason = "generated scene catalog manifest has a blank entry";
            handle.Release();
            return false;
        }
        (*handle.Entries)[static_cast<int32_t>(index)] =
            new ::RuntimeSceneCatalogEntry(entry.SceneId, entry.CookedRelativePath);
    }
    handle.Catalog = new ::RuntimeSceneCatalog(handle.Entries);
    return true;
}

// The startup manifest gives a cooked path; the catalog is keyed by scene id,
// so the id whose path matches is the one to load.
const char* FindStartupSceneId(const char*& reason) {
    const char* startupPath = he_get_runtime_startup_scene_relative_path();
    if (startupPath == nullptr || startupPath[0] == '\0') {
        reason = "generated startup manifest names no scene";
        return nullptr;
    }
    std::size_t count = 0;
    const HERuntimeSceneCatalogEntry* entries = he_runtime_scene_catalog_entries(&count);
    for (std::size_t index = 0; index < count; ++index) {
        const char* path = entries[index].CookedRelativePath;
        if (path != nullptr && std::string(path) == startupPath) {
            return entries[index].SceneId;
        }
    }
    reason = "startup scene path is not present in the scene catalog";
    return nullptr;
}

} // namespace

const char* RuntimeStageName(RuntimeStage stage) {
    switch (stage) {
    case RuntimeStage::Booting: return "booting";
    case RuntimeStage::VideoReady: return "video-ready";
    case RuntimeStage::RendererReady: return "renderer-ready";
    case RuntimeStage::CatalogReady: return "catalog-ready";
    case RuntimeStage::CoreReady: return "core-ready";
    case RuntimeStage::SceneLoaded: return "scene-loaded";
    case RuntimeStage::Running: return "running";
    case RuntimeStage::Drained: return "drained";
    case RuntimeStage::Shutdown: return "shutdown";
    }
    return "unknown";
}

int RunAuthoredScene(unsigned stopAfterFrames, RuntimeResult& result) {
    result = RuntimeResult{};

    XboxVideo video;
    if (!video.Initialize()) {
        result.Reason = "video initialization failed";
        return 1;
    }
    result.Stage = RuntimeStage::VideoReady;

    XboxRasterRenderer renderer;
    if (!renderer.Initialize(video.Width(), video.Height())) {
        result.Reason = "renderer initialization failed";
        video.Shutdown();
        return 1;
    }
    result.Stage = RuntimeStage::RendererReady;

    SceneCatalogHandle catalog;
    const char* reason = nullptr;
    if (!TryBuildSceneCatalog(catalog, reason)) {
        result.Reason = reason;
        renderer.Shutdown();
        video.Shutdown();
        return 1;
    }
    const char* startupSceneId = FindStartupSceneId(reason);
    if (startupSceneId == nullptr) {
        result.Reason = reason;
        catalog.Release();
        renderer.Shutdown();
        video.Shutdown();
        return 1;
    }
    result.Stage = RuntimeStage::CatalogReady;
    Diagnostics::Log("catalog: startup scene '%s'", startupSceneId);

    XboxContentStreamSource content;
    XboxInputBackend input;
    XboxRenderManager3D render3D;
    XboxRenderManager2D render2D;

    render3D.AddWindow(0, static_cast<int32_t>(video.Width()), static_cast<int32_t>(video.Height()));
    render3D.SetViewport(video.Width(), video.Height());
    render3D.InstallRenderer(&renderer);
    render2D.InstallRenderer(&renderer, &content);

    // Owned by Core once handed over; Core's Dispose releases them.
    ::CoreInitializationOptions* options = new ::CoreInitializationOptions();
    options->ContentStreamSource = &content;
    options->SceneCatalog = catalog.Catalog;
    options->DefaultUpdateDeltaSeconds = kDefaultFrameSeconds;
    options->CommitPendingSceneOperationsDuringDraw = false;
    ::PlatformInfo* platformInfo =
        new ::PlatformInfo(he_get_runtime_platform_name(), he_get_runtime_platform_version());

    // From here on a managed failure does not return: it reaches
    // he_cpp_custom::Fail, which reports and halts. Anything reported after
    // this point means the step before it completed.
    ::Core* core = new ::Core(options);
    core->Initialize(&render3D, &render2D, &input, platformInfo, options);
    result.Stage = RuntimeStage::CoreReady;
    Diagnostics::Log("core-ready");

    ::SceneManager* sceneManager = core->get_SceneManager();
    if (sceneManager == nullptr) {
        result.Reason = "core exposed no scene manager";
        return 1;
    }
    sceneManager->LoadScene(startupSceneId, ::SceneLoadMode::Single);
    // The load is queued; one update plus a frame boundary commits it before
    // anything asks the object manager what the scene contains.
    core->Update(0.0);
    core->CompleteFrameBoundary();
    result.Stage = RuntimeStage::SceneLoaded;
    result.ContentServed = content.ServedCount();
    result.ContentFailures = content.FailureCount();
    Diagnostics::Log("scene-loaded: %u assets served, %u failed", result.ContentServed,
                     result.ContentFailures);

    result.Stage = RuntimeStage::Running;
    LARGE_INTEGER frequency{};
    LARGE_INTEGER previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    bool running = true;
    while (running) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double elapsed = SecondsBetween(now, previous, frequency);
        previous = now;

        core->Update(elapsed);
        if (result.Frames >= 3 && g_titleRightSampleMask != 3) {
            CaptureTitleRightSamples(core);
        }

        if (!video.BeginFrame(kClearColorArgb)) {
            result.Reason = "begin frame failed";
            return 1;
        }
        core->Draw();
        render2D.Draw();
        const AuthoredFrameCounts& counts = render3D.LastFrame();
        if (!video.EndFrame()) {
            result.Reason = "end frame failed";
            return 1;
        }
        core->CompleteFrameBoundary();

        ++result.Frames;
        result.SubmittedDrawables = counts.Submitted;
        result.Triangles = counts.Triangles;
        if (result.Frames == 1) {
            Diagnostics::Log("first frame: cam %u drawables %u submitted %u tris %u",
                             counts.Cameras, counts.Drawables, counts.Submitted, counts.Triangles);
            if (counts.Submitted == 0 && counts.LastSkipReason[0] != '\0') {
                Diagnostics::Log("first frame drew nothing: %s", counts.LastSkipReason);
            }
        }
        if (stopAfterFrames != 0 && result.Frames >= stopAfterFrames) {
            running = false;
        }
    }

    // Drain before releasing anything the GPU may still be reading: the models
    // own GPU-visible vertex memory that Core's dispose path frees.
    if (!video.Drain()) {
        result.Reason = "gpu drain failed";
        return 1;
    }
    result.Stage = RuntimeStage::Drained;

    core->Dispose();
    delete core;
    catalog.Release();
    renderer.Shutdown();
    video.Shutdown();
    result.Stage = RuntimeStage::Shutdown;
    Diagnostics::Log("shutdown: %u frames, %u tris/frame", result.Frames, result.Triangles);
    return 0;
}

} // namespace helengine::xbox

#endif
