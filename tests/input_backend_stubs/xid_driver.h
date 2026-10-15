#pragma once
#include <cstdint>
inline constexpr int XID_TYPE_GAMECONTROLLER = 1;
struct xid_dev_t {
    struct { int bType = XID_TYPE_GAMECONTROLLER; } xid_desc;
    void* user_data = nullptr;
};
struct xid_gamepad_in {
    std::uint8_t startByte = 0, bLength = 0;
    std::uint16_t dButtons = 0;
    std::uint8_t a = 0, b = 0, x = 0, y = 0, black = 0, white = 0, l = 0, r = 0;
    std::int16_t leftStickX = 0, leftStickY = 0, rightStickX = 0, rightStickY = 0;
};
using ConnectionCallback = void (*)(xid_dev_t*, int);
inline ConnectionCallback connected_callback = nullptr;
inline ConnectionCallback disconnected_callback = nullptr;
inline void* read_callback = nullptr;
inline void usbh_xid_init() {}
inline void usbh_install_xid_conn_callback(ConnectionCallback connected, ConnectionCallback disconnected) {
    connected_callback = connected;
    disconnected_callback = disconnected;
}
inline int usbh_xid_read(xid_dev_t*, int, void* callback) { read_callback = callback; return 0; }
