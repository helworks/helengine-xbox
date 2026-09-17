#include "XboxBootHost.hpp"

#include "XboxAbiChecks.hpp"
#include "XboxDiagnostics.hpp"
#include "XboxRasterRenderer.hpp"
#include "XboxVideo.hpp"

#include <cstdio>

#include <windows.h>

namespace helengine::xbox {
namespace {

constexpr unsigned kClearColorArgb = 0xFF203040; // dark blue-grey background

// AbiChecks reports through the clear colour as well as the overlay. Reading
// text back out of a captured framebuffer depends on guessing the buffer's
// exact alignment, whereas a whole-screen colour survives any misalignment, so
// the pass/fail signal stays legible even when the capture is sheared.
constexpr unsigned kAbiPassColorArgb = 0xFF107C10; // green
constexpr unsigned kAbiFailColorArgb = 0xFFA01010; // red
constexpr unsigned kMilestoneFrames = 3600;      // ~60 s at 60 Hz
constexpr DWORD kExitDisplayDelayMs = 5000;      // keep diagnostics readable before the CRT reboots

const char* ModeName(ProbeMode mode) {
    switch (mode) {
    case ProbeMode::Clear: return "Clear";
    case ProbeMode::Triangle: return "Triangle";
    case ProbeMode::Cube: return "Cube";
    case ProbeMode::AbiChecks: return "AbiChecks";
    }
    return "Unknown";
}

bool IsValidMode(ProbeMode mode) {
    return mode == ProbeMode::Clear || mode == ProbeMode::Triangle || mode == ProbeMode::Cube ||
           mode == ProbeMode::AbiChecks;
}

void HoldForReading() {
    Sleep(kExitDisplayDelayMs);
}

} // namespace

int XboxBootHost::Run(const BootConfig& config) {
    if (!IsValidMode(config.Mode)) {
        Diagnostics::Log("boot error: unsupported probe mode %d", static_cast<int>(config.Mode));
        HoldForReading();
        return 2;
    }
    Diagnostics::Log("boot: helengine-xbox v0.1 probe=%s stopAfterFrames=%u", ModeName(config.Mode),
                     config.StopAfterFrames);

    XboxVideo video;
    if (!video.Initialize()) {
        Diagnostics::Log("boot error: video initialization failed");
        HoldForReading();
        return 3;
    }

    // The ABI checks run once, before any frame, so a failure is visible even if
    // presentation later goes wrong.
    AbiCheckResults abiResults;
    const bool runsAbiChecks = config.Mode == ProbeMode::AbiChecks;
    if (runsAbiChecks) {
        abiResults = RunDynamicCastChecks();
    }

    XboxRasterRenderer renderer;
    const bool needsRenderer = config.Mode != ProbeMode::Clear && config.Mode != ProbeMode::AbiChecks;
    if (needsRenderer && !renderer.Initialize(video.Width(), video.Height())) {
        Diagnostics::Log("boot error: renderer initialization failed");
        // Partial startup: only video succeeded. The renderer releases whatever
        // it allocated itself; nothing has reached the GPU yet.
        if (video.Drain()) {
            renderer.Shutdown();
            video.Shutdown();
        } else {
            Diagnostics::Log("boot error: drain failed; leaving GPU resources allocated");
        }
        HoldForReading();
        return 4;
    }

    int exitCode = 0;
    unsigned frame = 0;
    char overlayLine[XboxVideo::RequiredWidth / 8];
    while (config.StopAfterFrames == 0 || frame < config.StopAfterFrames) {
        unsigned clearColor = kClearColorArgb;
        if (runsAbiChecks) {
            clearColor = abiResults.AllPassed() ? kAbiPassColorArgb : kAbiFailColorArgb;
        }
        if (!video.BeginFrame(clearColor)) {
            Diagnostics::Log("frame error: BeginFrame failed at frame %u", frame);
            exitCode = 5;
            break;
        }
        if (needsRenderer && !renderer.Draw(config.Mode)) {
            Diagnostics::Log("frame error: Draw failed at frame %u", frame);
            exitCode = 6;
            break;
        }
        std::snprintf(overlayLine, sizeof(overlayLine), "helengine-xbox v0.1 %s frame %u", ModeName(config.Mode),
                      frame + 1);
        video.PrintOverlay(overlayLine);
        if (runsAbiChecks) {
            std::snprintf(overlayLine, sizeof(overlayLine), "dynamic_cast checks: %u passed %u failed - %s",
                          abiResults.Passed, abiResults.Failed, abiResults.AllPassed() ? "PASS" : "FAIL");
            video.PrintOverlay(overlayLine);
            if (!abiResults.AllPassed()) {
                std::snprintf(overlayLine, sizeof(overlayLine), "first failure: %s", abiResults.FirstFailure);
                video.PrintOverlay(overlayLine);
            }
        }
        if (needsRenderer) {
            const RendererCounters counters = renderer.Counters();
            std::snprintf(overlayLine, sizeof(overlayLine), "gpu alloc %u free %u draws %u tris %u",
                          counters.Allocations, counters.Releases, counters.DrawCalls, counters.Triangles);
            video.PrintOverlay(overlayLine);
        }
        video.PrintOverlay(Diagnostics::Last());
        if (!video.EndFrame()) {
            Diagnostics::Log("frame error: EndFrame failed at frame %u", frame);
            exitCode = 7;
            break;
        }
        ++frame;
        if (frame == kMilestoneFrames) {
            if (needsRenderer) {
                const RendererCounters counters = renderer.Counters();
                Diagnostics::Log("milestone: %u frames presented, gpu alloc %u free %u draws %u", frame,
                                 counters.Allocations, counters.Releases, counters.DrawCalls);
            } else {
                Diagnostics::Log("milestone: %u frames presented", frame);
            }
        }
    }

    Diagnostics::Log("shutdown: %u frames presented, exit code %d", frame, exitCode);
    if (!video.Drain()) {
        // Terminal diagnostic state: the GPU may still reference the renderer's
        // storage, so nothing is freed.
        Diagnostics::Log("shutdown error: GPU drain failed; resources intentionally retained");
        HoldForReading();
        return exitCode != 0 ? exitCode : 8;
    }
    if (needsRenderer) {
        renderer.Shutdown();
    }
    video.Shutdown();
    HoldForReading();
    return exitCode;
}

} // namespace helengine::xbox
