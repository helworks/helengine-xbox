#include "XboxAuthoredTransform.hpp"

#include <cmath>

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
#include "float4x4.hpp"
#endif

namespace helengine::xbox {
namespace {

// Tolerance for the camera orthonormality check. Authored camera transforms
// arrive through the engine's quaternion-to-matrix path, so their basis is
// orthonormal to within accumulated float error rather than exactly; this is
// wide enough for that and far too tight to admit an authored scale.
constexpr float kRigidTolerance = 1e-3f;

bool AllFinite(const Matrix4& matrix) {
    for (unsigned index = 0; index < 16; ++index) {
        if (!std::isfinite(matrix.Values[index])) {
            return false;
        }
    }
    return true;
}

// Last row must be (0, 0, 0, 1): anything else is a projective transform, which
// neither the model path nor the rigid inverse below is prepared for.
bool IsAffine(const Matrix4& matrix) {
    return matrix.Values[12] == 0.0f && matrix.Values[13] == 0.0f &&
           matrix.Values[14] == 0.0f && matrix.Values[15] == 1.0f;
}

float Determinant3x3(const Matrix4& m) {
    const float a = m.Values[0], b = m.Values[1], c = m.Values[2];
    const float d = m.Values[4], e = m.Values[5], f = m.Values[6];
    const float g = m.Values[8], h = m.Values[9], i = m.Values[10];
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

// Dot product of two columns of the upper 3x3.
float ColumnDot(const Matrix4& m, unsigned left, unsigned right) {
    return m.Values[0 + left] * m.Values[0 + right] +
           m.Values[4 + left] * m.Values[4 + right] +
           m.Values[8 + left] * m.Values[8 + right];
}

} // namespace

Matrix4 MatrixFromGenerated(const GeneratedMatrixFields& g) {
    // Transpose: generated row r, column c lands at column-vector [c][r].
    Matrix4 result{};
    result.Values[0] = g.M11;  result.Values[1] = g.M21;  result.Values[2] = g.M31;  result.Values[3] = g.M41;
    result.Values[4] = g.M12;  result.Values[5] = g.M22;  result.Values[6] = g.M32;  result.Values[7] = g.M42;
    result.Values[8] = g.M13;  result.Values[9] = g.M23;  result.Values[10] = g.M33; result.Values[11] = g.M43;
    result.Values[12] = g.M14; result.Values[13] = g.M24; result.Values[14] = g.M34; result.Values[15] = g.M44;
    return result;
}

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
Matrix4 MatrixFromGenerated(const ::float4x4& generated) {
    GeneratedMatrixFields fields{};
    fields.M11 = generated.M11; fields.M12 = generated.M12; fields.M13 = generated.M13; fields.M14 = generated.M14;
    fields.M21 = generated.M21; fields.M22 = generated.M22; fields.M23 = generated.M23; fields.M24 = generated.M24;
    fields.M31 = generated.M31; fields.M32 = generated.M32; fields.M33 = generated.M33; fields.M34 = generated.M34;
    fields.M41 = generated.M41; fields.M42 = generated.M42; fields.M43 = generated.M43; fields.M44 = generated.M44;
    return MatrixFromGenerated(fields);
}
#endif

bool ValidateModelTransform(const Matrix4& worldFromModel) {
    if (!AllFinite(worldFromModel) || !IsAffine(worldFromModel)) {
        return false;
    }
    // A singular basis flattens the mesh, which reads on screen as a missing
    // object rather than as a fault, so refuse it here where it can be named.
    const float determinant = Determinant3x3(worldFromModel);
    return std::fabs(determinant) > 1e-12f;
}

bool ValidateRigidCameraTransform(const Matrix4& worldFromCamera) {
    if (!AllFinite(worldFromCamera) || !IsAffine(worldFromCamera)) {
        return false;
    }
    // Orthonormal columns: unit length and mutually perpendicular. This rejects
    // any authored scale or shear, which the transpose-based inverse below
    // would otherwise silently mis-invert.
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (std::fabs(ColumnDot(worldFromCamera, axis, axis) - 1.0f) > kRigidTolerance) {
            return false;
        }
    }
    if (std::fabs(ColumnDot(worldFromCamera, 0, 1)) > kRigidTolerance ||
        std::fabs(ColumnDot(worldFromCamera, 0, 2)) > kRigidTolerance ||
        std::fabs(ColumnDot(worldFromCamera, 1, 2)) > kRigidTolerance) {
        return false;
    }
    // A mirrored basis would flip winding and silently invert back-face
    // culling, so require a right-handed one.
    return Determinant3x3(worldFromCamera) > 0.0f;
}

bool TryViewFromCameraWorld(const Matrix4& worldFromCamera, Matrix4& output) {
    if (!ValidateRigidCameraTransform(worldFromCamera)) {
        return false;
    }

    const float tx = worldFromCamera.Values[3];
    const float ty = worldFromCamera.Values[7];
    const float tz = worldFromCamera.Values[11];

    Matrix4 view{};
    // Rotation part is the transpose of the camera's basis.
    view.Values[0] = worldFromCamera.Values[0];
    view.Values[1] = worldFromCamera.Values[4];
    view.Values[2] = worldFromCamera.Values[8];
    view.Values[4] = worldFromCamera.Values[1];
    view.Values[5] = worldFromCamera.Values[5];
    view.Values[6] = worldFromCamera.Values[9];
    view.Values[8] = worldFromCamera.Values[2];
    view.Values[9] = worldFromCamera.Values[6];
    view.Values[10] = worldFromCamera.Values[10];
    // Translation becomes -R^T * t.
    view.Values[3] = -(view.Values[0] * tx + view.Values[1] * ty + view.Values[2] * tz);
    view.Values[7] = -(view.Values[4] * tx + view.Values[5] * ty + view.Values[6] * tz);
    view.Values[11] = -(view.Values[8] * tx + view.Values[9] * ty + view.Values[10] * tz);
    view.Values[15] = 1.0f;

    output = view;
    return true;
}

} // namespace helengine::xbox
