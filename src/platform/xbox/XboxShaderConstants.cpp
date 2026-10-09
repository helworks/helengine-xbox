#include "XboxShaderConstants.hpp"
#include <cmath>

namespace helengine::xbox {
bool BuildShaderDrawConstants(const Matrix4& clip, int matrixRegister, unsigned width, unsigned height,
                              float textureWidth, float textureHeight, float (&output)[XboxShaderConstantCount][4]) {
    if (width == 0 || height == 0 || !std::isfinite(textureWidth) || !std::isfinite(textureHeight) ||
        textureWidth <= 0 || textureHeight <= 0 ||
        matrixRegister < -1 || matrixRegister > 89) return false;
    for (float value : clip.Values) if (!std::isfinite(value)) return false;
    for (auto& vector : output) for (float& value : vector) value = 0;
    if (matrixRegister >= 0) {
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                output[matrixRegister + row][column] = clip.Values[column * 4 + row];
    }
    output[XboxTextureExtentConstant][0] = static_cast<float>(textureWidth);
    output[XboxTextureExtentConstant][1] = static_cast<float>(textureHeight);
    output[XboxViewportScaleConstant][0] = static_cast<float>(width) * 0.5f;
    output[XboxViewportScaleConstant][1] = -static_cast<float>(height) * 0.5f;
    output[XboxViewportScaleConstant][2] = 16777215.0f;
    output[XboxViewportOffsetConstant][0] = static_cast<float>(width) * 0.5f + 0.53125f;
    output[XboxViewportOffsetConstant][1] = static_cast<float>(height) * 0.5f + 0.53125f;
    return true;
}
}
