#include "XboxInputBackend.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "InputFrameState.hpp"

namespace helengine::xbox {

XboxInputBackend::XboxInputBackend() = default;

::InputFrameState XboxInputBackend::CaptureFrame() {
    ++capturedFrames_;
    // The generated default constructor already zeroes the pointer and text
    // state; the gamepad count is set explicitly so a consumer reading it does
    // not walk a null array.
    ::InputFrameState state;
    state.set_GamepadCount(0);
    return state;
}

} // namespace helengine::xbox

#endif
