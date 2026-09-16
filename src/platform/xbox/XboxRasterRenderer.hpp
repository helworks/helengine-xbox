#pragma once

// GPU rendering for the native Xbox probe: persistent vertex storage, vertex
// program / register-combiner setup, unlit render state and indexed
// submission through pbkit. Owns no presentation state (see XboxVideo).

#include "XboxBootHost.hpp"
#include "XboxCubeMesh.hpp"
#include "XboxMath.hpp"

namespace helengine::xbox {

struct RendererCounters {
    unsigned Allocations = 0; // GPU-visible allocations performed
    unsigned Releases = 0;    // GPU-visible allocations released
    unsigned DrawCalls = 0;   // indexed submissions issued
    unsigned Triangles = 0;   // triangles submitted (cumulative)
};

class XboxRasterRenderer {
public:
    // Allocates GPU-visible vertex storage once, uploads the vertex program and
    // combiner setup, and computes the fixed camera/projection for the given
    // back-buffer extent. Returns false on any detected failure.
    bool Initialize(unsigned width, unsigned height);

    // Submits the probe geometry for one frame between XboxVideo::BeginFrame
    // and XboxVideo::EndFrame. Performs no allocation.
    bool Draw(ProbeMode mode);

    // Releases GPU-visible storage. Call only after the GPU has been drained.
    void Shutdown();

    RendererCounters Counters() const { return counters_; }

private:
    bool UploadPrograms();
    bool BuildTransforms(unsigned width, unsigned height);
    void PushRenderState();
    void PushConstants(const Matrix4& clip);
    void PushVertexAttributes(unsigned firstVertex);
    void PushIndexedTriangles(const unsigned short* indices, unsigned indexCount);

    bool initialized_ = false;
    CubeVertex* gpuVertices_ = nullptr; // write-combined contiguous memory; never read back
    unsigned gpuVertexCount_ = 0;
    unsigned cubeFirstVertex_ = 0;
    unsigned triangleFirstVertex_ = 0;
    CubeGeometry cube_{};               // ordinary CPU memory; indices are emitted into the push buffer
    unsigned short cubeDrawOrder_[CubeGeometry::IndexCount]{};
    unsigned short triangleIndices_[3]{};
    Matrix4 cubeClip_{};
    Matrix4 triangleClip_{};
    float viewportScale_[4]{};
    float viewportOffset_[4]{};
    RendererCounters counters_{};
};

} // namespace helengine::xbox
