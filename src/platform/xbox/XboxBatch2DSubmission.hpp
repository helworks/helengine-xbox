#pragma once
#include <cmath>
#include <cstdint>
namespace helengine::xbox {
// Synchronous view of the shared Batch2DVertex (six float4 values) and its indices.
struct XboxBatch2DSubmission {
    const float* Vertices;
    unsigned VertexCount;
    const unsigned short* Indices;
    unsigned IndexCount;
    int ScissorX, ScissorY, ScissorWidth, ScissorHeight;
};
inline bool ValidateBatch2DSubmission(const XboxBatch2DSubmission& batch, unsigned width,
                                    unsigned height, unsigned remainingVertices) {
    if (!batch.Vertices || !batch.Indices || batch.VertexCount == 0 || batch.VertexCount > 65536 ||
        batch.VertexCount > remainingVertices || batch.VertexCount % 4 != 0 ||
        batch.IndexCount != batch.VertexCount / 4 * 6 || width == 0 || height == 0 ||
        batch.ScissorX < 0 || batch.ScissorY < 0 || batch.ScissorWidth <= 0 || batch.ScissorHeight <= 0 ||
        uint64_t(batch.ScissorX) + unsigned(batch.ScissorWidth) > width ||
        uint64_t(batch.ScissorY) + unsigned(batch.ScissorHeight) > height) return false;
    for (unsigned index = 0; index < batch.IndexCount; ++index)
        if (batch.Indices[index] >= batch.VertexCount) return false;
    for (unsigned value = 0; value < batch.VertexCount * 24; ++value)
        if (!std::isfinite(batch.Vertices[value])) return false;
    return true;
}
}
