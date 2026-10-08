#pragma once
#include "XboxMath.hpp"
namespace helengine::xbox {
inline Matrix4 BatchProjectionForMaterial(const float (&rows)[16]) {
    Matrix4 result{};
    // This is an upload payload, not a native column-vector transform.
    // The canonical HLSL cbuffer is column-major; BuildDrawConstants performs
    // the one transpose needed to publish its columns to Cg's DP4 registers.
    for (unsigned i = 0; i < 16; ++i) result.Values[i] = rows[i];
    return result;
}
}
