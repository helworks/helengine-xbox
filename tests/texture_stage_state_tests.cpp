#include "platform/xbox/XboxTextureStageState.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <pbkit/nv_regs.h>
using namespace helengine::xbox;
struct Writes { unsigned Count = 0; std::map<unsigned, unsigned> Values; };
static void Capture(void* context, uint32_t method, uint32_t value) {
    auto& writes = *static_cast<Writes*>(context);
    ++writes.Count; writes.Values[method] = value;
}
int main() {
    XboxTextureStageState state{};
    XboxTextureLayout layout{64, 32, 64, 32, 4, 0x12, 8192};
    assert(BuildTextureStageState(&layout, 0x1000, state));
    assert(state.Width == 64 && state.Height == 32);
    assert(state.Offset == 0x1000 && state.Pitch == 256);
    assert(state.ImageRect == 0x00400020 && state.Enabled);
    // Invalid preparation clears old state, so a caller cannot replay a previous texture.
    for (unsigned offset : {0u, 0x04000000u, 0x03ffaff0u}) {
        assert(!BuildTextureStageState(&layout, offset, state));
        assert(!state.Enabled && state.Offset == 0);
    }
    auto bad = layout; bad.StorageWidth = 128;
    assert(!BuildTextureStageState(&bad, 0x1000, state));
    bad = layout; bad.ByteCount = 8191;
    assert(!BuildTextureStageState(&bad, 0x1000, state));
    bad = layout; bad.HardwareFormat = 0;
    assert(!BuildTextureStageState(&bad, 0x1000, state));
    bad = layout; bad.RealHeight = 0;
    assert(!BuildTextureStageState(&bad, 0x1000, state));
    bad = layout; bad.RealWidth = bad.StorageWidth = 16384;
    assert(!BuildTextureStageState(&bad, 0x1000, state));
    for (int i = 0; i < 120; ++i) {
        assert(BuildTextureStageState(&layout, 0x2000, state) && state.Enabled);
        assert(BuildTextureStageState(nullptr, 0, state));
        assert(!state.Enabled && state.Offset == 0 && state.Pitch == 0 && state.ImageRect == 0);
        assert(state.Width == 1 && state.Height == 1);
    }
    assert(!BuildTextureStageState(nullptr, 0x1000, state));
    Writes writes;
    assert(!BindTextureStage(&layout, 0, Capture, &writes));
    assert(writes.Count == 0);
    assert(!BindTextureStage(&layout, 0x1000, nullptr, &writes));
    assert(writes.Count == 0);
    assert(BindTextureStage(&layout, 0x1000, Capture, &writes));
    assert(writes.Count == 15);
    assert(writes.Values[NV097_SET_TEXTURE_OFFSET] == 0x1000);
    assert(writes.Values[NV097_SET_TEXTURE_CONTROL1] == 0x01000000);
    assert(writes.Values[NV097_SET_TEXTURE_IMAGE_RECT] == 0x00400020);
    assert(writes.Values[NV097_SET_TEXTURE_FORMAT] == 0x00011222);
    assert(writes.Values[NV097_SET_TEXTURE_ADDRESS] == 0x00030303);
    const auto filter = writes.Values[NV097_SET_TEXTURE_FILTER];
    // Bilinear minification and magnification with no convolution kernel or
    // LOD bias: tiny coverage glyphs must not receive the mesh sample blur.
    assert((filter & NV097_SET_TEXTURE_FILTER_MIN) == (2u << 16));
    assert((filter & NV097_SET_TEXTURE_FILTER_MAG) == (2u << 24));
    assert((filter & 0x0000ffffu) == 0);
    assert(writes.Values[NV097_SET_TEXTURE_CONTROL0] == 0x4003ffc0);
    for (unsigned stage = 1; stage < 4; ++stage)
        assert(writes.Values[NV097_SET_TEXTURE_CONTROL0 + stage * 64] == 0);
    assert(BindTextureStage(nullptr, 0, Capture, &writes));
    assert(writes.Count == 23);
    for (unsigned stage = 0; stage < 4; ++stage)
        assert(writes.Values[NV097_SET_TEXTURE_CONTROL0 + stage * 64] == 0);
    std::puts("Texture preparation validates extents, pitch, allocation bounds and clears stale state.");
}
