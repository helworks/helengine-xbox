#include "Batch2DWriter.hpp"
#include "Batch2DSink.hpp"
#include "Batch2DBuffer.hpp"
#include "Batch2DVertex.hpp"
#include "RuntimeTexture.hpp"
#include "platform/xbox/XboxBatch2DSubmission.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace he_cpp_custom { [[noreturn]] void Fail(const char* message) { std::fprintf(stderr, "%s\n", message); std::abort(); } }
struct Sink final : Batch2DSink {
    std::vector<float> Markers;
    unsigned Cameras = 0;
    void SetCamera(float4x4, float4) override { ++Cameras; }
    void Submit(Batch2DBuffer* buffer, Batch2DRun run) override {
        assert(buffer && buffer->VertexCount == 4 && buffer->IndexCount == 6);
        const unsigned short expected[] = {0, 1, 2, 0, 2, 3};
        for (unsigned i = 0; i < 6; ++i) assert((*buffer->Indices)[i] == expected[i]);
        helengine::xbox::XboxBatch2DSubmission view{reinterpret_cast<const float*>(buffer->Vertices->Data),
            unsigned(buffer->VertexCount), buffer->Indices->Data, unsigned(buffer->IndexCount),
            run.ScissorX, run.ScissorY, run.ScissorWidth, run.ScissorHeight};
        assert(helengine::xbox::ValidateBatch2DSubmission(view, 640, 480, 4));
        Markers.push_back((*buffer->Vertices)[0].Position.X);
    }
};
int main() {
    static_assert(sizeof(Batch2DVertex) == 96);
    Sink sink;
    RuntimeTexture texture;
    Batch2DWriter writer(&sink, 1);
    const float4 viewport(0, 0, 640, 480);
    writer.ConfigureCamera(float4x4(), viewport);
    Batch2DRun run(Batch2DVariant::Textured, &texture, 0, 0, 640, 480);
    Batch2DVertex vertex;
    vertex.Position = float4(10, 20, 0, 1);
    writer.AppendQuad(run, vertex, vertex, vertex, vertex);
    vertex.Position.X = 30;
    writer.AppendQuad(run, vertex, vertex, vertex, vertex);
    assert(sink.Markers.size() == 1 && sink.Markers[0] == 10);
    writer.ConfigureCamera(float4x4(), viewport);
    assert(sink.Markers.size() == 2 && sink.Markers[1] == 30 && sink.Cameras == 2);
    writer.Flush();
    assert(sink.Markers.size() == 2);
    writer.Dispose();
    texture.Dispose();
    puts("Actual Xbox-generated shared writer preserves copied vertices, capacity flushes, camera ordering and submission ABI.");
}
