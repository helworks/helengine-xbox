#pragma once

#include <cstdint>
#include <pbkit/nv_regs.h>

namespace helengine::xbox::shaderprobe {
// Engine-owned prelude for extraction/binding of the initial fp20 subset.
// Compiler commands follow this prelude and may enable their required stages.
// Texture objects and render/material state deliberately remain untouched.
template <typename PushOne>
uint32_t* PushPixelShaderBaseline(uint32_t* p, PushOne pushOne) {
    pushOne(p, NV097_SET_SHADER_STAGE_PROGRAM, 0);
    p += 2;
    pushOne(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    p += 2;
    return p;
}
}
