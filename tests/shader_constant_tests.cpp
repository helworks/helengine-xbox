#include "platform/xbox/XboxShaderConstants.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace helengine::xbox;
int main() {
    Matrix4 nativeClip{{1,5,9,13, 2,6,10,14, 3,7,11,15, 4,8,12,16}};
    float constants[96][4] = {};
    assert(BuildShaderDrawConstants(nativeClip, 0, 640, 480, 256, 128, constants));
    const float position[] = {2,3,5,1};
    const float expected[] = {75,86,97,108};
    for (unsigned lane = 0; lane < 4; ++lane) {
        float result = 0;
        for (unsigned row = 0; row < 4; ++row) result += position[row] * constants[row][lane];
        assert(result == expected[lane]);
    }
    assert(constants[93][0] == 256 && constants[93][1] == 128);
    assert(constants[94][0] == 320 && constants[94][1] == -240 && constants[94][2] == 16777215);
    assert(constants[95][0] == 320.53125f && constants[95][1] == 240.53125f);
    assert(constants[4][0] == 0 && constants[4][1] == 0);
    assert(BuildShaderDrawConstants(nativeClip, 12, 320, 240, 32, 64, constants));
    assert(constants[0][0] == 0 && constants[12][0] == 1 && constants[15][3] == 16);
    assert(constants[93][0] == 32 && constants[93][1] == 64);
    assert(!BuildShaderDrawConstants(nativeClip, 90, 640, 480, 1, 1, constants));
    assert(!BuildShaderDrawConstants(nativeClip, 0, 0, 480, 1, 1, constants));
    assert(!BuildShaderDrawConstants(nativeClip, 0, 640, 480, 0, 1, constants));
    nativeClip.Values[0] = std::numeric_limits<float>::infinity();
    assert(!BuildShaderDrawConstants(nativeClip, 0, 640, 480, 1, 1, constants));
    std::puts("Shader draw constants: matrix packing, reserved slots, viewport, UV and invalid inputs passed.");
}
