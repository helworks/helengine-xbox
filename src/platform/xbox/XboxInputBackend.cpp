#include "XboxInputBackend.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "InputFrameState.hpp"
#include "InputGamepadButton.hpp"
#include "XboxDiagnostics.hpp"
#include <usbh_lib.h>
#include <xid_driver.h>
#include <cstring>
#include <limits>

namespace helengine::xbox {

namespace {
// XID reports positive-up; the engine's stick contract uses negative-up.
// The asymmetric signed range requires saturation when negating INT16_MIN.
int16_t EngineStickY(int16_t nativeValue) {
    return nativeValue == std::numeric_limits<int16_t>::min()
        ? std::numeric_limits<int16_t>::max()
        : static_cast<int16_t>(-nativeValue);
}
struct ControllerReport { xid_dev_t* Device = nullptr; xid_gamepad_in Report{}; };
ControllerReport reports[4];
void ReadComplete(UTR_T* transfer) {
    auto* device = static_cast<xid_dev_t*>(transfer->context);
    auto* slot = device ? static_cast<ControllerReport*>(device->user_data) : nullptr;
    if (!slot || slot->Device != device || transfer->status < 0) return;
    if (transfer->xfer_len >= sizeof(xid_gamepad_in))
        std::memcpy(&slot->Report, transfer->buff, sizeof(xid_gamepad_in));
    transfer->xfer_len = 0;
    transfer->bIsTransferDone = 0;
    usbh_int_xfer(transfer);
}
void Connected(xid_dev_t* device, int) {
    if (!device || device->xid_desc.bType != XID_TYPE_GAMECONTROLLER) return;
    for (auto& slot : reports) if (!slot.Device) {
        slot = {};
        slot.Device = device;
        device->user_data = &slot;
        if (usbh_xid_read(device, 0, reinterpret_cast<void*>(ReadComplete)) < 0) {
            device->user_data = nullptr;
            slot = {};
        }
        return;
    }
}
void Disconnected(xid_dev_t* device, int) {
    for (auto& slot : reports) if (slot.Device == device) { device->user_data = nullptr; slot = {}; }
}
}
XboxInputBackend::XboxInputBackend() {
    usbh_core_init();
    usbh_xid_init();
    usbh_install_xid_conn_callback(Connected, Disconnected);
}
XboxInputBackend::~XboxInputBackend() {
    usbh_install_xid_conn_callback(nullptr, nullptr);
    usbh_core_deinit();
    for (auto& slot : reports) slot = {};
}

::InputFrameState XboxInputBackend::CaptureFrame() {
    ++capturedFrames_;
    usbh_pooling_hubs();
    ::InputFrameState state;
    auto& pads = (capturedFrames_ & 1) ? frameA_ : frameB_;
    for (int i = 0; i < 4; ++i) {
        auto& pad = pads.Data[i];
        pad = ::InputGamepadState();
        const auto& slot = reports[i];
        if (!slot.Device) continue;
        const auto& input = slot.Report;
        pad.Connected = true;
        pad.LeftStickX = input.leftStickX; pad.LeftStickY = EngineStickY(input.leftStickY);
        pad.RightStickX = input.rightStickX; pad.RightStickY = EngineStickY(input.rightStickY);
        pad.LeftTrigger = static_cast<int16_t>(input.l * 128);
        pad.RightTrigger = static_cast<int16_t>(input.r * 128);
        const auto button = [&](::InputGamepadButton value, bool down) { pad.SetButtonDown(value, down); };
        button(::InputGamepadButton::South, input.a > 32); button(::InputGamepadButton::East, input.b > 32);
        button(::InputGamepadButton::West, input.x > 32); button(::InputGamepadButton::North, input.y > 32);
        button(::InputGamepadButton::LeftShoulder, input.white > 32); button(::InputGamepadButton::RightShoulder, input.black > 32);
        button(::InputGamepadButton::LeftTrigger, input.l > 32); button(::InputGamepadButton::RightTrigger, input.r > 32);
        button(::InputGamepadButton::DPadUp, input.dButtons & 1); button(::InputGamepadButton::DPadDown, input.dButtons & 2);
        button(::InputGamepadButton::DPadLeft, input.dButtons & 4); button(::InputGamepadButton::DPadRight, input.dButtons & 8);
        button(::InputGamepadButton::Start, input.dButtons & 16); button(::InputGamepadButton::Select, input.dButtons & 32);
        button(::InputGamepadButton::LeftStick, input.dButtons & 64); button(::InputGamepadButton::RightStick, input.dButtons & 128);
    }
    state.set_Gamepads(&pads);
    state.set_GamepadCount(4);
    return state;
}

} // namespace helengine::xbox

#endif
