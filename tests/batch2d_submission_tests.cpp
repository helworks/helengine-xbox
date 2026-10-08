#include "platform/xbox/XboxBatch2DSubmission.hpp"
#include "platform/xbox/XboxBatch2DProjection.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace helengine::xbox;
int main() {
    // Canonical shader lowers to DP4(position, c[column]); material uploads
    // the transpose of its Matrix4. Test translated viewports, not identity.
    const float projection[16] = {2.f/640,0,0,0, 0,-2.f/480,0,0, 0,0,1,0, -1,1,0,1};
    auto material = BatchProjectionForMaterial(projection);
    const float position[4] = {160,120,0,1};
    float clip[4]{};
    for (unsigned component=0; component<4; ++component)
        for (unsigned input=0; input<4; ++input)
            clip[component] += position[input] * material.Values[input*4+component];
    assert(std::fabs(clip[0] + .5f) < .00001f);
    assert(std::fabs(clip[1] - .5f) < .00001f);
    assert(clip[2] == 0 && clip[3] == 1);
    float vertices[4 * 24]{};
    for (unsigned i = 0; i < 4; ++i) vertices[i * 24 + 3] = 1;
    unsigned short indices[] = {0, 1, 2, 0, 2, 3};
    XboxBatch2DSubmission batch{vertices, 4, indices, 6, 10, 20, 100, 80};
    assert(ValidateBatch2DSubmission(batch, 640, 480, 4));
    assert(!ValidateBatch2DSubmission(batch, 640, 480, 3));
    indices[5] = 4;
    assert(!ValidateBatch2DSubmission(batch, 640, 480, 4));
    indices[5] = 3;
    vertices[3] = std::numeric_limits<float>::quiet_NaN();
    assert(!ValidateBatch2DSubmission(batch, 640, 480, 4));
    vertices[3] = 1;
    batch.ScissorX = -1;
    assert(!ValidateBatch2DSubmission(batch, 640, 480, 4));
    batch.ScissorX = 600;
    assert(!ValidateBatch2DSubmission(batch, 640, 480, 4));
    batch.ScissorX = 10;
    batch.IndexCount = 5;
    assert(!ValidateBatch2DSubmission(batch, 640, 480, 4));
    batch.IndexCount = 6;
    batch.Vertices = nullptr;
    assert(!ValidateBatch2DSubmission(batch, 640, 480, 4));
    puts("Batch submission rejects invalid indices, nonfinite vertices, capacity overflow and out-of-bounds scissors.");
}
