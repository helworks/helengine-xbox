#include "XboxMath.hpp"

#include <cmath>

namespace helengine::xbox {

Matrix4 Identity() {
    Matrix4 m{};
    m.Values[0] = 1.0f;
    m.Values[5] = 1.0f;
    m.Values[10] = 1.0f;
    m.Values[15] = 1.0f;
    return m;
}

Matrix4 Translation(float x, float y, float z) {
    Matrix4 m = Identity();
    m.Values[3] = x;
    m.Values[7] = y;
    m.Values[11] = z;
    return m;
}

Matrix4 RotationX(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Matrix4 m = Identity();
    m.Values[5] = c;
    m.Values[6] = -s;
    m.Values[9] = s;
    m.Values[10] = c;
    return m;
}

Matrix4 RotationY(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Matrix4 m = Identity();
    m.Values[0] = c;
    m.Values[2] = s;
    m.Values[8] = -s;
    m.Values[10] = c;
    return m;
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) {
    Matrix4 m{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += left.Values[row * 4 + k] * right.Values[k * 4 + column];
            }
            m.Values[row * 4 + column] = sum;
        }
    }
    return m;
}

Vector4 Transform(const Matrix4& matrix, const Vector4& vector) {
    const float* v = matrix.Values;
    Vector4 out;
    out.X = v[0] * vector.X + v[1] * vector.Y + v[2] * vector.Z + v[3] * vector.W;
    out.Y = v[4] * vector.X + v[5] * vector.Y + v[6] * vector.Z + v[7] * vector.W;
    out.Z = v[8] * vector.X + v[9] * vector.Y + v[10] * vector.Z + v[11] * vector.W;
    out.W = v[12] * vector.X + v[13] * vector.Y + v[14] * vector.Z + v[15] * vector.W;
    return out;
}

bool TryPerspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane, Matrix4& output) {
    constexpr float kPi = 3.14159265358979323846f;
    if (!std::isfinite(verticalFovRadians) || !std::isfinite(aspect) || !std::isfinite(nearPlane) ||
        !std::isfinite(farPlane)) {
        return false;
    }
    if (verticalFovRadians <= 0.0f || verticalFovRadians >= kPi) {
        return false;
    }
    if (aspect <= 0.0f || nearPlane <= 0.0f || farPlane <= nearPlane) {
        return false;
    }
    const float f = 1.0f / std::tan(verticalFovRadians * 0.5f);
    if (!std::isfinite(f)) {
        return false;
    }
    Matrix4 m{};
    m.Values[0] = f / aspect;                                        // m00
    m.Values[5] = f;                                                 // m11
    m.Values[10] = farPlane / (nearPlane - farPlane);                // m22
    m.Values[11] = (farPlane * nearPlane) / (nearPlane - farPlane);  // m23
    m.Values[14] = -1.0f;                                            // m32
    output = m;
    return true;
}

} // namespace helengine::xbox
