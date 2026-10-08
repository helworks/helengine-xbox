#include "XboxCubeMesh.hpp"

namespace helengine::xbox {
namespace {

struct Face {
    float Normal[3];  // also the face centre for unit half-extents
    float U[3];       // cross(U, V) == Normal
    float V[3];
    float Color[4];
};

// Six faces; tangent frames chosen so cross(U, V) equals the outward normal,
// which makes the emitted corner order counter-clockwise from outside.
constexpr Face kFaces[6] = {
    {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},    // +Z red
    {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  // -Z green
    {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},   // +X blue
    {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},   // -X yellow
    {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},   // +Y cyan
    {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},   // -Y magenta
};

// Corner k of a face: centre + su*U + sv*V with (su, sv) in this order.
constexpr float kCornerSigns[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
constexpr unsigned short kFaceIndices[6] = {0, 1, 2, 0, 2, 3};

} // namespace

CubeGeometry MakeCubeGeometry() {
    CubeGeometry geometry{};
    for (unsigned face = 0; face < 6; ++face) {
        const Face& f = kFaces[face];
        for (unsigned corner = 0; corner < 4; ++corner) {
            CubeVertex& vertex = geometry.Vertices[face * 4 + corner];
            const float su = kCornerSigns[corner][0];
            const float sv = kCornerSigns[corner][1];
            for (unsigned axis = 0; axis < 3; ++axis) {
                vertex.Position[axis] = f.Normal[axis] + su * f.U[axis] + sv * f.V[axis];
            }
            for (unsigned channel = 0; channel < 4; ++channel) {
                vertex.Color[channel] = f.Color[channel];
            }
            for (unsigned axis = 0; axis < 3; ++axis) {
                vertex.Normal[axis] = f.Normal[axis];
            }
        }
        for (unsigned i = 0; i < 6; ++i) {
            geometry.Indices[face * 6 + i] = static_cast<unsigned short>(face * 4 + kFaceIndices[i]);
        }
    }
    return geometry;
}

} // namespace helengine::xbox
