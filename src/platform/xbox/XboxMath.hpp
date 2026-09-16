#pragma once

// Small native matrix helpers for the Xbox diagnostic probe.
//
// Conventions (local to this probe; shared engine math is untouched):
//   - Matrix4 stores 16 floats row-major: Values[row * 4 + column].
//   - Column-vector multiplication: clip = Projection * View * Model * position.
//   - Right-handed view space looking down negative Z.
//   - Normalized clip depth range [0, 1] (z/w = 0 at the near plane, 1 at far).
// No heap allocation; everything is fixed-size and deterministic. This header
// does not depend on nxdk so the host-side contract tests can compile it.

namespace helengine::xbox {

struct Matrix4 {
    float Values[16];
};

struct Vector4 {
    float X, Y, Z, W;
};

Matrix4 Identity();
Matrix4 Translation(float x, float y, float z);
Matrix4 RotationX(float radians);
Matrix4 RotationY(float radians);
Matrix4 Multiply(const Matrix4& left, const Matrix4& right);
Vector4 Transform(const Matrix4& matrix, const Vector4& vector);

// Perspective with f = 1/tan(fov/2):
//   m00 = f/aspect, m11 = f, m22 = far/(near-far), m23 = far*near/(near-far),
//   m32 = -1, all other entries zero.
// Returns false and leaves output untouched for nonfinite or nonpositive FOV,
// FOV >= pi, nonpositive aspect, nonpositive near, or far <= near.
bool TryPerspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane, Matrix4& output);

} // namespace helengine::xbox
