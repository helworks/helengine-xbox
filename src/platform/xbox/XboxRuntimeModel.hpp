#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Native model built from one cooked ModelAsset.
//
// Owns a GPU-visible vertex array in the format the v0.1 vertex program already
// consumes (float3 position, float4 colour) plus a CPU-side 16-bit index array,
// because indices are copied into the push buffer rather than fetched by the
// GPU. Vertex colour is not authored per vertex here: this milestone is unlit
// and flat-shaded, so the bound material's colour is written across the array
// and refreshed only when it changes.
//
// Nothing here throws. Create returns null and sets reason, which the caller
// logs; the generated runtime's failure path is reached afterwards and carries
// no file context of its own.

#include "XboxCubeMesh.hpp"
#include "XboxMath.hpp"

#include "ModelAsset.hpp"
#include "RuntimeModel.hpp"

namespace helengine::xbox {

class XboxRuntimeModel final : public ::RuntimeModel {
public:
    // Indices reach the GPU through NV097_ARRAY_ELEMENT16, so the vertex count
    // cannot exceed what a 16-bit index can address.
    static constexpr unsigned MaxVertices = 65535;
    // Bounds the push-buffer work for one model. Far above anything this
    // milestone authors, and low enough that a corrupt count is caught.
    static constexpr unsigned MaxTriangles = 65535;

    // Copies and validates the borrowed asset. The caller retains the asset.
    static XboxRuntimeModel* Create(const ::ModelAsset& asset, const char*& reason);

    ~XboxRuntimeModel() override;

    void Dispose() override;

    // Writes lit material colour from source normals and the current world
    // transform. GPU-visible memory is write-combined and is never read back.
    bool ApplyDirectionalLighting(float r, float g, float b, float a, const Matrix4& worldFromModel,
                                 const float lightDirection[3], const float lightColor[3], float intensity);

    void EnsureColor(float r, float g, float b, float a);

    const CubeVertex* GpuVertices() const { return gpuVertices_; }
    unsigned VertexCount() const { return vertexCount_; }
    const unsigned short* Indices() const { return indices_; }
    unsigned IndexCount() const { return indexCount_; }
    bool HasTextureCoordinates() const { return hasTextureCoordinates_; }

private:
    XboxRuntimeModel();

    CubeVertex* gpuVertices_ = nullptr; // write-combined; never read back
    float* normals_ = nullptr; // ordinary memory; source normals used for per-frame lighting
    unsigned short* indices_ = nullptr; // ordinary memory
    unsigned vertexCount_ = 0;
    unsigned indexCount_ = 0;
    bool disposed_ = false;
    bool hasTextureCoordinates_ = false;
};

} // namespace helengine::xbox

#endif
