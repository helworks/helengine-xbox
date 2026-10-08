#include "platform/xbox/XboxShaderConstants.hpp"
#include <pbkit/nv_regs.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using namespace helengine::xbox;
constexpr uint32_t kConstantBase = 96;
static uint32_t packet[128];
static unsigned cursor = 0, delivered = 0;
static uint32_t* pb_begin() { std::memset(packet, 0, sizeof(packet)); return packet; }
static void pb_push(uint32_t* p, uint32_t method, unsigned count) { *p = method | (count << 18); }
static uint32_t* pb_push1(uint32_t* p, uint32_t method, uint32_t value) {
    pb_push(p, method, 1); p[1] = value; return p + 2;
}
static void pb_end(uint32_t* end) {
    assert(packet[0] == (NV097_SET_TRANSFORM_CONSTANT_LOAD | (1u << 18)));
    cursor = packet[1];
    const unsigned count = packet[2] >> 18;
    assert((packet[2] & 0x1fff) == NV097_SET_TRANSFORM_CONSTANT);
    // The incrementing method aperture ends at 0xbfc; 0xc00 is back-light state.
    assert(count > 0 && count <= 32 && count % 4 == 0);
    assert(end == packet + 3 + count);
    assert(cursor == 96 + delivered / 4);
    for (unsigned word = 0; word < count; ++word) {
        float value; std::memcpy(&value, packet + 3 + word, sizeof(value));
        assert(value == static_cast<float>(delivered + word));
    }
    delivered += count;
}
#include "platform/xbox/XboxShaderConstantUpload.inl"
int main() {
    float constants[96][4];
    for (unsigned row = 0; row < 96; ++row)
        for (unsigned column = 0; column < 4; ++column) constants[row][column] = static_cast<float>(row * 4 + column);
    PushCompiledConstants(nullptr, constants);
    assert(delivered == 384);
    std::puts("All 96 constant rows uploaded without crossing into back-light methods.");
}
