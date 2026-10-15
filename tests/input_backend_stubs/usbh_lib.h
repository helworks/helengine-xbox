#pragma once
#include <cstdint>
struct UTR_T {
    void* context = nullptr;
    int status = 0;
    std::uint32_t xfer_len = 0;
    std::uint8_t* buff = nullptr;
    int bIsTransferDone = 0;
};
inline void usbh_core_init() {}
inline void usbh_core_deinit() {}
inline void usbh_pooling_hubs() {}
inline int usbh_int_xfer(UTR_T*) { return 0; }
