#include <cassert>
#include <cstdint>
#include <cstdio>
#include <pbkit/nv_regs.h>

static bool stageProgramWritten = false;
static uint32_t stageProgram = 0;

// Capture the actual register writes emitted by the compiled sprite shader.
static uint32_t* pb_push1(uint32_t* p, uint32_t method, uint32_t value) {
    if (method == NV097_SET_SHADER_STAGE_PROGRAM) {
        stageProgramWritten = true;
        stageProgram = value;
    }
    return p + 2;
}

static void PushSprite() {
    uint32_t commands[1024] = {};
    uint32_t* p = commands;
#include "sprite_psh.inl"
    (void)p;
}

static void PushUnlit() {
    uint32_t commands[1024] = {};
    uint32_t* p = commands;
#include "unlit_psh.inl"
    (void)p;
}

int main() {
    PushSprite();
    // Stage zero must sample 2D; stages one through three must be disabled.
    assert(stageProgramWritten && "sprite shader never enables texture sampling");
    assert(stageProgram == 1 && "expected stage 0 2D sampling only");
    // The next frame must not retain sampling after its texture is disabled.
    stageProgramWritten = false;
    PushUnlit();
    assert(stageProgramWritten && "unlit shader never resets texture sampling");
    assert(stageProgram == 0 && "unlit shader retains sprite sampling");
    PushSprite();
    assert(stageProgram == 1 && "sprite sampling not restored next frame");
    std::puts("Sprite and unlit shader transition contracts passed");
}
