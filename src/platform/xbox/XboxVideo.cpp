#include "XboxVideo.hpp"

#include "XboxDiagnostics.hpp"

#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <xboxkrnl/xboxkrnl.h>

namespace helengine::xbox {
namespace {

// pbkit returns swap-table-full from pb_finished until the VBlank interrupt
// retires a frame; KeTickCount advances once per millisecond.
constexpr DWORD kSwapWaitTimeoutMs = 2000;
constexpr DWORD kDrainTimeoutMs = 5000;

bool WaitForGpuIdle(DWORD timeoutMs) {
    const DWORD start = KeTickCount;
    while (pb_busy()) {
        if (KeTickCount - start > timeoutMs) {
            return false;
        }
    }
    return true;
}

} // namespace

bool XboxVideo::Initialize() {
    if (initialized_) {
        Diagnostics::Log("video-init error: already initialized");
        return false;
    }
    Diagnostics::Log("video-init: requesting %ux%u %u-bit", RequiredWidth, RequiredHeight, RequiredBitsPerPixel);

    // XVideoSetMode reports failure for unsupported modes (BOOL), and
    // XVideoGetMode exposes what the encoder actually selected.
    if (!XVideoSetMode(static_cast<int>(RequiredWidth), static_cast<int>(RequiredHeight),
                       static_cast<int>(RequiredBitsPerPixel), REFRESH_DEFAULT)) {
        Diagnostics::Log("video-init error: XVideoSetMode rejected the mode");
        return false;
    }
    const VIDEO_MODE mode = XVideoGetMode();
    if (mode.width != static_cast<int>(RequiredWidth) || mode.height != static_cast<int>(RequiredHeight) ||
        mode.bpp != static_cast<int>(RequiredBitsPerPixel)) {
        Diagnostics::Log("video-init error: mode is %dx%d %d-bit", mode.width, mode.height, mode.bpp);
        return false;
    }
    Diagnostics::Log("video-init: mode %dx%d %d-bit refresh %d", mode.width, mode.height, mode.bpp, mode.refresh);

    // pb_init allocates the triple-buffered A8R8G8B8 colour surfaces and the
    // shared Z24S8 depth/stencil surface, installs the GPU interrupt and
    // starts the DMA engine. Nonzero is a documented failure code.
    const int status = pb_init();
    if (status != 0) {
        Diagnostics::Log("video-init error: pb_init returned %d", status);
        return false;
    }

    width_ = static_cast<unsigned>(pb_back_buffer_width());
    height_ = static_cast<unsigned>(pb_back_buffer_height());
    if (width_ != RequiredWidth || height_ != RequiredHeight) {
        Diagnostics::Log("video-init error: back buffer is %ux%u", width_, height_);
        pb_kill();
        width_ = 0;
        height_ = 0;
        return false;
    }

    initialized_ = true;
    Diagnostics::Log("video-ready: back buffer %ux%u pitch %lu, colour A8R8G8B8, depth Z24S8",
                     width_, height_, pb_back_buffer_pitch());
    pb_show_front_screen();
    return true;
}

bool XboxVideo::BeginFrame(unsigned clearArgb) {
    if (!initialized_) {
        return false;
    }
    pb_wait_for_vbl();
    pb_reset();
    pb_target_back_buffer();
    // Depth/stencil clear writes the maximum 24-bit depth (0xFFFFFF) and zero
    // stencil, which pairs with the renderer's less-or-equal depth test.
    pb_erase_depth_stencil_buffer(0, 0, static_cast<int>(width_), static_cast<int>(height_));
    pb_fill(0, 0, static_cast<int>(width_), static_cast<int>(height_), clearArgb);
    pb_erase_text_screen();
    return true;
}

void XboxVideo::PrintOverlay(const char* text) {
    if (!initialized_ || text == nullptr) {
        return;
    }
    pb_print("%s\n", text);
}

bool XboxVideo::EndFrame() {
    if (!initialized_) {
        return false;
    }
    pb_draw_text_screen();
    if (!WaitForGpuIdle(kDrainTimeoutMs)) {
        Diagnostics::Log("present error: GPU still busy after %lu ms", kDrainTimeoutMs);
        return false;
    }
    const DWORD start = KeTickCount;
    while (pb_finished()) {
        if (KeTickCount - start > kSwapWaitTimeoutMs) {
            Diagnostics::Log("present error: swap table full for %lu ms", kSwapWaitTimeoutMs);
            return false;
        }
    }
    ++presentedFrames_;
    if (presentedFrames_ == 1) {
        Diagnostics::Log("first-present: frame queued for the next VBlank");
    }
    return true;
}

bool XboxVideo::Drain() {
    if (!initialized_) {
        return true;
    }
    if (!WaitForGpuIdle(kDrainTimeoutMs)) {
        Diagnostics::Log("drain error: GPU did not go idle within %lu ms", kDrainTimeoutMs);
        return false;
    }
    return true;
}

void XboxVideo::Shutdown() {
    if (!initialized_) {
        return;
    }
    pb_show_debug_screen();
    pb_kill();
    initialized_ = false;
    Diagnostics::Log("video-shutdown: pbkit stopped after %u presented frames", presentedFrames_);
}

} // namespace helengine::xbox
