#pragma once

// Boot host for the native Xbox v0.1 probe.
//
// Sequences video begin-frame, the raster renderer draw and video
// end-frame/present, then drains the GPU and releases resources in reverse
// order. No engine runtime, authored content or Core is involved.

namespace helengine::xbox {

enum class ProbeMode { Clear, Triangle, Cube };

struct BootConfig {
    ProbeMode Mode = ProbeMode::Cube;
    unsigned StopAfterFrames = 0; // zero means continue presenting
};

class XboxBootHost {
public:
    // Returns 0 on a clean run, nonzero on any detected failure. A GPU drain
    // failure enters a terminal diagnostic state (no resource release) and
    // returns nonzero after a bounded delay.
    int Run(const BootConfig& config);
};

} // namespace helengine::xbox
