#include "platform/xbox/XboxInputBackend.hpp"
#include "InputFrameState.hpp"
#include "InputGamepadButton.hpp"
#include <usbh_lib.h>
#include <xid_driver.h>
#include <cstdio>
#include <cstdlib>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void Deliver(xid_dev_t& device, xid_gamepad_in& report) {
    UTR_T transfer{};
    transfer.context = &device;
    transfer.buff = reinterpret_cast<std::uint8_t*>(&report);
    transfer.xfer_len = sizeof(report);
    reinterpret_cast<void (*)(UTR_T*)>(read_callback)(&transfer);
}
}

int main() {
    helengine::xbox::XboxInputBackend backend;
    xid_dev_t device{};
    connected_callback(&device, 0);
    Require(read_callback != nullptr, "real backend installed XID read callback");
    struct Case { std::int16_t rawLeft, rawRight, expectedLeft, expectedRight; };
    const Case cases[] = {
        {0, 0, 0, 0}, {20000, -24000, -20000, 24000},
        {-20000, 24000, 20000, -24000}, {1, -1, -1, 1}, {-1, 1, 1, -1},
        {32767, -32768, -32767, 32767}, {-32768, 32767, 32767, -32767},
        {-32768, -32768, 32767, 32767}
    };
    for (const auto& test : cases) {
        xid_gamepad_in report{};
        report.leftStickX = -12345; report.rightStickX = 23456;
        report.leftStickY = test.rawLeft; report.rightStickY = test.rawRight;
        report.l = 255; report.r = 128;
        report.a = 255; report.b = 31; report.x = 33; report.y = 0;
        report.white = 255; report.black = 0;
        report.dButtons = 1 | 4 | 16 | 64;
        Deliver(device, report);
        auto frame = backend.CaptureFrame();
        Require(frame.get_GamepadCount() == 4, "four bounded gamepad slots retained");
        const auto& pad = frame.get_Gamepads()->Data[0];
        Require(pad.Connected, "connected report retained");
        Require(pad.LeftStickY == test.expectedLeft, "left Y must use negative-up and saturate INT16_MIN");
        Require(pad.RightStickY == test.expectedRight, "right Y must use negative-up and saturate INT16_MIN");
        Require(pad.LeftStickX == -12345 && pad.RightStickX == 23456, "X axes unchanged");
        Require(pad.LeftTrigger == 32640 && pad.RightTrigger == 16384, "trigger scaling unchanged");
        std::uint64_t expected = 0;
        for (auto button : {InputGamepadButton::South, InputGamepadButton::West,
             InputGamepadButton::LeftShoulder, InputGamepadButton::LeftTrigger,
             InputGamepadButton::RightTrigger, InputGamepadButton::DPadUp,
             InputGamepadButton::DPadLeft, InputGamepadButton::Start, InputGamepadButton::LeftStick})
            expected |= std::uint64_t{1} << static_cast<int>(button);
        Require(pad.Buttons == expected, "all button mappings unchanged");
        Require(!frame.get_Gamepads()->Data[1].Connected, "unused slots remain disconnected");
    }
    disconnected_callback(&device, 0);
    auto frame = backend.CaptureFrame();
    Require(!frame.get_Gamepads()->Data[0].Connected, "disconnect clears pad");
    std::puts("PASS: real Xbox CaptureFrame, eight Y cases, X/buttons/triggers and disconnect");
}
