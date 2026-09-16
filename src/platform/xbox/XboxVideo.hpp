#pragma once

// Video and presentation owner for the native Xbox probe.
//
// Wraps the pinned nxdk HAL video mode selection and the pbkit push-buffer
// lifecycle: mode set, pb_init, per-frame clear/present, GPU drain and pb_kill.
// Nothing else in the probe touches presentation state.

namespace helengine::xbox {

class XboxVideo {
public:
    static constexpr unsigned RequiredWidth = 640;
    static constexpr unsigned RequiredHeight = 480;
    static constexpr unsigned RequiredBitsPerPixel = 32;

    // Selects the 640x480 32-bit mode, starts pbkit and validates the back
    // buffer extent. Returns false (with diagnostics) on any detected failure;
    // a failed Initialize leaves nothing to shut down.
    bool Initialize();

    unsigned Width() const { return width_; }
    unsigned Height() const { return height_; }
    bool IsInitialized() const { return initialized_; }

    // Waits for vertical blank, rewinds the push buffer, targets the back
    // buffer, clears depth/stencil to the far plane and colour to clearArgb.
    bool BeginFrame(unsigned clearArgb);

    // Queues one overlay text line for this frame (pbkit's bounded text
    // console, max 16 rows of 60 columns).
    void PrintOverlay(const char* text);

    // Draws the overlay, waits for the GPU and queues the swap. Returns false
    // if the swap table never freed up within the bounded wait.
    bool EndFrame();

    // Waits until the GPU has consumed every queued command. Returns false on
    // timeout; callers must then treat GPU-visible resources as still in use.
    bool Drain();

    // Restores the debug console and stops the push-buffer engine. Only call
    // after Drain() succeeded (pb_kill frees pbkit's own GPU buffers).
    void Shutdown();

    unsigned PresentedFrames() const { return presentedFrames_; }

private:
    bool initialized_ = false;
    unsigned width_ = 0;
    unsigned height_ = 0;
    unsigned presentedFrames_ = 0;
};

} // namespace helengine::xbox
