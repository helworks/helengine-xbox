#include <cassert>
#include <cstdint>
#include <cstdio>
#include <pbkit/nv_regs.h>

static uint32_t registers[0x2000 / 4] = {};
static uint32_t* pb_push1(uint32_t* p, uint32_t method, uint32_t value) {
    assert(method < sizeof(registers) && method % 4 == 0);
    registers[method / 4] = value;
    return p;
}
static uint32_t Field(uint32_t method, uint32_t mask) {
    return (registers[method / 4] & mask) >> (__builtin_ffs(mask) - 1);
}
static void PushColour() {
    uint32_t commands[1024] = {}; uint32_t* p = commands;
#include "colour-pixel.inl"
    (void)p;
}
static void PushTextured() {
    uint32_t commands[1024] = {}; uint32_t* p = commands;
#include "textured-pixel.inl"
    (void)p;
}
int main() {
    PushColour();
    // NV2A source 4 is primary colour; 8 is texture stage zero.
    assert(Field(NV097_SET_COMBINER_SPECULAR_FOG_CW0, NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_SOURCE) == 4);
    assert(Field(NV097_SET_COMBINER_SPECULAR_FOG_CW1, NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_SOURCE) == 4);
    assert(Field(NV097_SET_COMBINER_SPECULAR_FOG_CW1, NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_ALPHA) == 1);
    PushTextured();
    assert(registers[NV097_SET_SHADER_STAGE_PROGRAM / 4] == 1);
    assert(Field(NV097_SET_COMBINER_COLOR_ICW, NV097_SET_COMBINER_COLOR_ICW_A_SOURCE) == 8);
    assert(Field(NV097_SET_COMBINER_COLOR_ICW, NV097_SET_COMBINER_COLOR_ICW_B_SOURCE) == 4);
    assert(Field(NV097_SET_COMBINER_ALPHA_ICW, NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE) == 8);
    assert(Field(NV097_SET_COMBINER_ALPHA_ICW, NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE) == 4);
    assert(Field(NV097_SET_COMBINER_ALPHA_ICW, NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA) == 1);
    assert(Field(NV097_SET_COMBINER_ALPHA_ICW, NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA) == 1);
    std::puts("Compiled COLOR0 RGB/alpha and texture stage 0 interface passed.");
}
