#pragma once

// Conversion between generated engine matrices and this platform's math.
//
// Deliberately free of generated-Core headers except for one guarded overload,
// so the conversion and validation rules compile into the host contract tests.
// Getting these wrong does not crash - it silently renders the scene from the
// wrong place - so they are worth testing off-console.
//
// Two conventions differ and both are handled here, once:
//
//   - The generated float4x4 is a row-vector matrix: a point is a row on the
//     left, translation lives in M41..M43, and hierarchy composition is
//     local * parent. XboxMath uses column-vector multiplication with
//     translation in the last column. The conversion is a transpose.
//
//   - The generated camera looks down -Z in a right-handed space, which is
//     already what TryPerspective in XboxMath assumes, so no axis reflection
//     is applied. Dreamcast needs one because its pipeline takes positive
//     depth forward; this platform does not, and adding one would put the
//     scene behind the camera.

#include "XboxMath.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
class float4x4;
#endif

namespace helengine::xbox {

// Plain mirror of the generated float4x4 fields so the host tests can build one
// without the generated tree.
struct GeneratedMatrixFields {
    float M11, M12, M13, M14;
    float M21, M22, M23, M24;
    float M31, M32, M33, M34;
    float M41, M42, M43, M44;
};

// Transposes row-vector fields into this platform's column-vector Matrix4.
Matrix4 MatrixFromGenerated(const GeneratedMatrixFields& generated);

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
Matrix4 MatrixFromGenerated(const ::float4x4& generated);
#endif

// True when every element is finite and the matrix can place geometry: the
// upper 3x3 must be invertible, and the last row must be affine. A singular
// model transform collapses the mesh to a plane or a point, which reads as a
// missing object rather than an error, so it is refused up front.
bool ValidateModelTransform(const Matrix4& worldFromModel);

// True when the transform is a rigid motion - orthonormal basis, unit scale,
// affine last row. A camera with scale or shear has no well-defined view
// matrix under the cheap inverse below, so this is checked before using it.
bool ValidateRigidCameraTransform(const Matrix4& worldFromCamera);

// Inverts a validated rigid camera transform into a view matrix, using the
// transpose of the rotation and the rotated negative translation rather than a
// general inverse. Returns false and leaves output untouched when the input is
// not rigid.
bool TryViewFromCameraWorld(const Matrix4& worldFromCamera, Matrix4& output);

} // namespace helengine::xbox
