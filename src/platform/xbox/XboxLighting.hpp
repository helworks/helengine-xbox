#pragma once

#include "XboxAuthoredTransform.hpp"

namespace helengine::xbox {

// Applies the inverse-transpose of an affine model transform and normalizes
// the result. This is required for correct shading under nonuniform scale.
bool TryTransformNormal(const Matrix4& worldFromModel, const float normal[3], float output[3]) noexcept;

// Direction points in the direction light travels (the engine convention is
// the light entity's local -Z axis). Returns clamped Lambert diffuse [0, 1].
float EvaluateDirectionalDiffuse(const float worldNormal[3], const float lightDirection[3],
                                float intensity) noexcept;

} // namespace helengine::xbox
