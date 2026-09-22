#pragma once

// GPU rendering for the native Xbox probe: persistent vertex storage, vertex
// program / register-combiner setup, unlit render state and indexed
// submission through pbkit. Owns no presentation state (see XboxVideo).

#include "XboxBootHost.hpp"
#include "XboxCubeMesh.hpp"
#include "XboxMath.hpp"
#include "XboxSpriteGeometry.hpp"

namespace helengine::xbox {

class XboxRuntimeTexture;

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

    // Pushes the shared unlit render state once per authored frame. The
    // authored path draws a variable number of meshes, so state is separated
    // from submission rather than pushed per draw as the probe does.
    bool BeginAuthoredFrame();

    // Submits one authored mesh with its own clip matrix and GPU-visible
    // vertex array. The array must be the same interleaved layout the probe
    // uses and must stay resident until the GPU has consumed the frame.
    // Performs no allocation. Must follow BeginAuthoredFrame.
    bool DrawAuthored(const Matrix4& clip,
                      const CubeVertex* gpuVertices,
                      unsigned vertexCount,
                      const unsigned short* indices,
                      unsigned indexCount);

    // Begins the ordered 2D overlay pass after Core's 3D draws.
    bool BeginSpritePass();
    bool DrawSprite(const XboxSpriteQuad& quad, const XboxRuntimeTexture& texture);

    // Releases GPU-visible storage. Call only after the GPU has been drained.
    void Shutdown();

    RendererCounters Counters() const { return counters_; }

private:
    bool UploadPrograms();
    bool UploadVertexProgram(const uint32_t* program, unsigned instructionCount);
    void PushUnlitCombiner();
    void PushSpriteCombiner();
    bool BuildTransforms(unsigned width, unsigned height);
    void PushRenderState();
    void PushConstants(const Matrix4& clip);
    void PushVertexAttributes(const CubeVertex* base);
    void PushSpriteVertexAttributes(const XboxSpriteVertex* base);
    void PushIndexedTriangles(const unsigned short* indices, unsigned indexCount);

    bool initialized_ = false;
    CubeVertex* gpuVertices_ = nullptr; // write-combined contiguous memory; never read back
    XboxSpriteVertex* gpuSpriteVertices_ = nullptr;
    unsigned spriteCount_ = 0;
    bool spritePassActive_ = false;
#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
    bool spriteCapacityReported_ = false;
#endif
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
