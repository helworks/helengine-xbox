#include "XboxLighting.hpp"

#include <cmath>

namespace helengine::xbox {
namespace {

bool Normalize(float vector[3]) noexcept {
    const float lengthSquared = vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-20f) return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    vector[0] *= inverseLength;
    vector[1] *= inverseLength;
    vector[2] *= inverseLength;
    return std::isfinite(vector[0]) && std::isfinite(vector[1]) && std::isfinite(vector[2]);
}

} // namespace

bool TryTransformNormal(const Matrix4& worldFromModel, const float normal[3], float output[3]) noexcept {
    if (normal == nullptr || output == nullptr) return false;
    for (unsigned index = 0; index < 16; ++index) {
        if (!std::isfinite(worldFromModel.Values[index])) return false;
    }
    if (!std::isfinite(normal[0]) || !std::isfinite(normal[1]) || !std::isfinite(normal[2])) return false;

    const float a = worldFromModel.Values[0], b = worldFromModel.Values[1], c = worldFromModel.Values[2];
    const float d = worldFromModel.Values[4], e = worldFromModel.Values[5], f = worldFromModel.Values[6];
    const float g = worldFromModel.Values[8], h = worldFromModel.Values[9], i = worldFromModel.Values[10];
    const float c00 = e * i - f * h, c01 = f * g - d * i, c02 = d * h - e * g;
    const float c10 = c * h - b * i, c11 = a * i - c * g, c12 = b * g - a * h;
    const float c20 = b * f - c * e, c21 = c * d - a * f, c22 = a * e - b * d;
    const float determinant = a * c00 + b * c01 + c * c02;
    if (!std::isfinite(determinant) || std::fabs(determinant) <= 1.0e-12f) return false;

    const float inverseDeterminant = 1.0f / determinant;
    float transformed[3] = {
        (c00 * normal[0] + c01 * normal[1] + c02 * normal[2]) * inverseDeterminant,
        (c10 * normal[0] + c11 * normal[1] + c12 * normal[2]) * inverseDeterminant,
        (c20 * normal[0] + c21 * normal[1] + c22 * normal[2]) * inverseDeterminant,
    };
    if (!Normalize(transformed)) return false;
    output[0] = transformed[0];
    output[1] = transformed[1];
    output[2] = transformed[2];
    return true;
}

float EvaluateDirectionalDiffuse(const float worldNormal[3], const float lightDirection[3],
                                 float intensity) noexcept {
    if (worldNormal == nullptr || lightDirection == nullptr || !std::isfinite(intensity) || intensity <= 0.0f) {
        return 0.0f;
    }
    float normal[3] = {worldNormal[0], worldNormal[1], worldNormal[2]};
    float direction[3] = {lightDirection[0], lightDirection[1], lightDirection[2]};
    if (!Normalize(normal) || !Normalize(direction)) return 0.0f;
    const float lambert = -(normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2]);
    const float diffuse = lambert * intensity;
    if (!std::isfinite(diffuse) || diffuse <= 0.0f) return 0.0f;
    return diffuse >= 1.0f ? 1.0f : diffuse;
}

} // namespace helengine::xbox
