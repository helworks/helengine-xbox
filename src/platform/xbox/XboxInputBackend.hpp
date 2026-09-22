#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Input backend stub.
//
// Core::Initialize requires an IInputBackend and the interface has one pure
// virtual, so one has to exist. This milestone reads no controllers: it renders
// a static authored scene and has nothing to steer. The backend reports a
// consistent empty frame - no pointer, no gamepads, no text - rather than
// leaving the state uninitialised, so a component that polls input sees
// "nothing pressed" instead of whatever was on the stack.
//
// Reading the Xbox controllers through nxdk's USB stack is a separate piece of
// work and is deliberately not started here.

#include "IInputBackend.hpp"

namespace helengine::xbox {

class XboxInputBackend final : public ::IInputBackend {
public:
    XboxInputBackend();

    bool get_ReceiveInputInBackground() override;
    void set_ReceiveInputInBackground(bool value) override;
    ::InputFrameState CaptureFrame() override;

    // Number of frames captured, so the host can show that input was polled.
    unsigned CapturedFrames() const { return capturedFrames_; }

private:
    unsigned capturedFrames_ = 0;
    bool receiveInputInBackground_ = false;
};

} // namespace helengine::xbox

#endif
