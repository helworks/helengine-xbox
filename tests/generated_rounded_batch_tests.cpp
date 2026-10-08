#include "platform/xbox/XboxRoundedBatch.hpp"
#include "Batch2DSink.hpp"
#include "Batch2DBuffer.hpp"
#include "RuntimeTexture.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace he_cpp_custom { [[noreturn]] void Fail(const char* message) { std::fprintf(stderr,"%s\n",message); std::abort(); } }
using namespace helengine::xbox;
struct Sink : Batch2DSink {
    std::vector<float> Order;
    RuntimeTexture* Expected;
    void SetCamera(float4x4, float4) override {}
    void Submit(Batch2DBuffer* buffer, Batch2DRun run) override {
        assert(run.Variant == Batch2DVariant::Textured && run.Texture == Expected);
        assert(run.ScissorX == 5 && run.ScissorY == 6 && run.ScissorWidth == 100 && run.ScissorHeight == 90);
        assert(buffer->VertexCount == 4 && buffer->IndexCount == 6);
        auto& tl = (*buffer->Vertices)[0];
        auto& br = (*buffer->Vertices)[2];
        assert(tl.Position.W == 1 && tl.Position.Z == 0);
        assert(tl.Color.X == 1 && tl.Color.Y == 1 && tl.Color.Z == 1 && tl.Color.W == 1);
        assert(tl.TexLocal.X == 0 && tl.TexLocal.Y == 0 && br.TexLocal.X == 1 && br.TexLocal.Y == 1);
        if (Order.empty()) {
            assert(tl.Position.X == 10 && tl.Position.Y == 20);
            assert(br.Position.X == 30 && br.Position.Y == 30);
        } else { // +pi/2 follows Core's clockwise-in-local convention
            assert(std::abs(tl.Position.X - 15) < .001 && std::abs(tl.Position.Y - 35) < .001);
            assert(std::abs(br.Position.X - 25) < .001 && std::abs(br.Position.Y - 15) < .001);
        }
        Order.push_back(tl.Position.X);
    }
};
int main() {
    Sink sink;
    RuntimeTexture texture;
    sink.Expected = &texture;
    Batch2DWriter writer(&sink,1);
    writer.ConfigureCamera(float4x4(),float4(0,0,640,480));
    Batch2DRun run(Batch2DVariant::Textured,&texture,5,6,100,90);
    assert(EmitRoundedTexturedQuad(writer,run,10,20,20,10,0));
    assert(EmitRoundedTexturedQuad(writer,run,10,20,20,10,3.141592653589793/2));
    writer.Flush();
    assert(sink.Order.size() == 2);
    assert(!EmitRoundedTexturedQuad(writer,run,10,20,0,10,0));
    assert(!EmitRoundedTexturedQuad(writer,run,10,20,20,10,NAN));
    writer.Dispose();
    puts("Rounded textured batches preserve geometry, rotation, UV, white tint, scissors and order through actual shared writer.");
}
