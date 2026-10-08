#pragma once
#include "XboxMath.hpp"

namespace helengine::xbox {
constexpr unsigned XboxShaderConstantCount = 96;
constexpr unsigned XboxTextureExtentConstant = 93;
constexpr unsigned XboxViewportScaleConstant = 94;
constexpr unsigned XboxViewportOffsetConstant = 95;

// Converts the native column-vector clip matrix to the authored HLSL row-vector
// convention at the reflected matrix register. -1 means no matrix binding.
// All unspecified constants, including SPIRV-Cross's half-pixel helper, start at
// zero. Material constants must be uploaded afterwards using validated bindings.
bool BuildShaderDrawConstants(const Matrix4& clip, int matrixRegister,
                              unsigned width, unsigned height, unsigned textureWidth, unsigned textureHeight,
                              float (&output)[XboxShaderConstantCount][4]);
}
