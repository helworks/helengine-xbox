#pragma once

// CPU-side immutable cube definition for the Xbox diagnostic probe.
//
// Six faces of four unique vertices each (24 vertices, 36 indices), unit
// half-extents, one opaque unlit colour per face. Winding is counter-clockwise
// when viewed from outside (dot(cross(b-a, c-a), centroid) > 0). This header
// does not depend on nxdk so the host-side contract tests can compile it.

namespace helengine::xbox {

struct CubeVertex {
    float Position[3];
    float Color[4];
};

struct CubeGeometry {
    static constexpr unsigned VertexCount = 24;
    static constexpr unsigned IndexCount = 36;
    CubeVertex Vertices[VertexCount];
    unsigned short Indices[IndexCount];
};

CubeGeometry MakeCubeGeometry();

} // namespace helengine::xbox
