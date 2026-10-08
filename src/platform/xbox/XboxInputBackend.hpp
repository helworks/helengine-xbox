#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "IInputBackend.hpp"
#include "InputGamepadState.hpp"
#include "runtime/array.hpp"

namespace helengine::xbox {

class XboxInputBackend final : public ::IInputBackend {
public:
    XboxInputBackend();
    ~XboxInputBackend();

    ::InputFrameState CaptureFrame() override;

    // Number of frames captured, so the host can show that input was polled.
    unsigned CapturedFrames() const { return capturedFrames_; }

private:
    unsigned capturedFrames_ = 0;
    // InputSystem retains the previous frame's array until the next capture.
    // Alternate bounded storage so pressed/released comparisons stay valid.
    ::Array<::InputGamepadState> frameA_{4};
    ::Array<::InputGamepadState> frameB_{4};
};

} // namespace helengine::xbox

#endif
