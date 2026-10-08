#include "platform/xbox/XboxMaterialFormat.hpp"
#include <cassert>
#include <cstdio>

using namespace helengine::xbox;

int main() {
    // Serialized platform material and shader material HELE headers select distinct
    // serializers even when the same renderer is built with both paths enabled.
    uint8_t classic[] = {'H','E','L','E',1,25,1,0,1,0,9,0};
    uint8_t shader[] = {'H','E','L','E',1,3,2,0,1,0,1,0};
    assert(ClassifyMaterialHeader(classic, sizeof(classic)) == XboxMaterialFormat::Classic);
    assert(ClassifyMaterialHeader(shader, sizeof(shader)) == XboxMaterialFormat::Shader);
    for (size_t length = 0; length < 12; ++length)
        assert(ClassifyMaterialHeader(classic, length) == XboxMaterialFormat::Unsupported);
    classic[10] = 2; // A model is not a material, although it uses the same container.
    assert(ClassifyMaterialHeader(classic, sizeof(classic)) == XboxMaterialFormat::Unsupported);
    shader[0] = 0;
    assert(ClassifyMaterialHeader(shader, sizeof(shader)) == XboxMaterialFormat::Unsupported);
    std::puts("Classic/shader material routing and invalid package rejection passed.");
}
