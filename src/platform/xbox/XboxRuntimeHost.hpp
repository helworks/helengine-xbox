#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Authored runtime host.
//
// Brings up video and the raster renderer, builds the scene catalog from the
// generated manifest, constructs and initializes Core, loads the startup
// scene, and runs the per-frame Update/Draw/CompleteFrameBoundary lifecycle.
// This is the piece that was missing: every earlier authored image linked Core
// but never called into it, so it kept rendering the v0.1 diagnostic cube.
//
// Nothing here throws, and nothing catches: this Core is generated with
// codegen-use-exceptions=false, so a managed failure reaches
// he_cpp_custom::Fail in XboxCoreRuntimeProvider and never returns. Failures
// this host can see itself are returned as a stage plus a reason instead.

#include <cstdint>

namespace helengine::xbox {

// How far the host got. Reported on screen so a failed boot says where it
// stopped rather than leaving a blank frame to interpret.
enum class RuntimeStage : unsigned {
    Booting = 0,
    VideoReady = 1,
    RendererReady = 2,
    CatalogReady = 3,
    CoreReady = 4,
    SceneLoaded = 5,
    Running = 6,
    Drained = 7,
    Shutdown = 8,
};

struct RuntimeResult {
    RuntimeStage Stage = RuntimeStage::Booting;
    const char* Reason = "";
    unsigned Frames = 0;
    unsigned SubmittedDrawables = 0;
    unsigned Triangles = 0;
    unsigned ContentServed = 0;
    unsigned ContentFailures = 0;
};

// Runs the authored scene until stopAfterFrames have been presented, or
// forever when it is zero. Returns 0 on a clean run.
int RunAuthoredScene(unsigned stopAfterFrames, RuntimeResult& result);

const char* RuntimeStageName(RuntimeStage stage);

} // namespace helengine::xbox

#endif
