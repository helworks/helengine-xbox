#include <cassert>
#include <cstdint>
#include <cstdio>
#include <pbkit/nv_regs.h>
#include "../tools/shader-probe/native/XboxPixelShaderBaseline.hpp"

static uint32_t stageProgram = 0xffffffff;
static uint32_t otherStageInput = 0xffffffff;
static uint32_t* pb_push1(uint32_t* p, uint32_t method, uint32_t value) {
    if (method == NV097_SET_SHADER_STAGE_PROGRAM) stageProgram = value;
    if (method == NV097_SET_SHADER_OTHER_STAGE_INPUT) otherStageInput = value;
    return p + 2;
}
static void PushColour() {
    uint32_t commands[1024] = {}; uint32_t* p = commands;
#ifndef XBOX_TEST_RAW_COMPILER_STATE
    p = helengine::xbox::shaderprobe::PushPixelShaderBaseline(p, pb_push1);
#endif
#include "colour-pixel.inl"
    (void)p;
}
static void PushTextured() {
    uint32_t commands[1024] = {}; uint32_t* p = commands;
#ifndef XBOX_TEST_RAW_COMPILER_STATE
    p = helengine::xbox::shaderprobe::PushPixelShaderBaseline(p, pb_push1);
#endif
#include "textured-pixel.inl"
    (void)p;
}
int main() {
    PushColour();
    const uint32_t firstColour = stageProgram;
    PushTextured();
    const uint32_t textured = stageProgram;
    PushColour();
    std::printf("Stage programs colour/textured/colour: 0x%x / 0x%x / 0x%x (required 0 / 1 / 0)\n",
                firstColour, textured, stageProgram);
    std::fflush(stdout);
    assert(firstColour == 0);
    assert(textured == 1);
    assert(stageProgram == 0);
    assert(otherStageInput == 0);
    for (unsigned frame = 0; frame < 120; ++frame) {
        stageProgram = 0xffffffff;
        otherStageInput = 0xffffffff;
        PushColour();
        assert(stageProgram == 0 && otherStageInput == 0);
        PushTextured();
        assert(stageProgram == 1 && otherStageInput == 0);
        PushColour();
        assert(stageProgram == 0 && otherStageInput == 0);
    }
    std::puts("Pixel baseline and 120 repeated colour/textured/colour transitions passed.");
}
