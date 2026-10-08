#include "platform/xbox/XboxRoundedTextureCache.hpp"
#include <cassert>
#include <cstdio>
using namespace helengine::xbox;
struct Resource {
    int* Alive;
    explicit Resource(int& alive) : Alive(&alive) { ++*Alive; }
    ~Resource() { --*Alive; }
};
int main() {
    int alive = 0, created = 0;
    auto factory = [&](const XboxRoundedStyle&) { ++created; return std::make_unique<Resource>(alive); };
    XboxRoundedTextureCache<Resource> cache(8192, 2);
    XboxRoundedStyle a;
    a.Width = a.Height = 16;
    auto b = a; b.Fill.R = 0;
    auto c = a; c.Fill.G = 0;
    cache.BeginFrame();
    auto* first = cache.Get(a, factory);
    assert(first && alive == 1 && created == 1);
    assert(cache.Get(a, factory) == first && created == 1);
    assert(cache.Get(b, factory) && alive == 2 && cache.Bytes() == 8192);
    // Both textures can be queued or in flight: never evict either this frame.
    assert(!cache.Get(c, factory) && alive == 2 && created == 2);
    cache.BeginFrame(); // caller has drained previous-frame GPU work
    assert(cache.Get(a, factory) == first);
    assert(cache.Get(c, factory) && alive == 2 && created == 3);
    assert(cache.Get(a, factory) == first); // pinned A survived eviction of B
    cache.Clear();
    assert(alive == 0 && cache.Bytes() == 0);
    auto fail = [](const XboxRoundedStyle&) { return std::unique_ptr<Resource>(); };
    assert(!cache.Get(a, fail) && cache.Bytes() == 0);
    auto invalid = a; invalid.Width = 0;
    assert(!cache.Get(invalid, factory) && created == 3);
    auto large = a; large.Width = large.Height = 512;
    assert(!cache.Get(large, factory) && created == 3);
    {
        XboxRoundedTextureCache<Resource> owned;
        assert(owned.Get(a, factory));
    }
    assert(alive == 0);
    puts("Rounded cache: reuse, budget, pinned-frame lifetime, eviction, factory failure and disposal passed.");
}
