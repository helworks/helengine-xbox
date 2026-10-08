#include "platform/xbox/XboxModelTextureCoordinates.hpp"
#include "ModelAsset.hpp"
#include "platform/xbox/XboxCubeMesh.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <cstdio>
using namespace helengine::xbox;
int main() {
    ModelAsset asset;
    const char* reason = nullptr;
    std::vector<float> coordinates{123};
    assert(!BuildModelTextureCoordinates(asset, 0, coordinates, reason));
    assert(!BuildModelTextureCoordinates(asset, 65536, coordinates, reason));
    assert(BuildModelTextureCoordinates(asset, 3, coordinates, reason));
    assert(coordinates.empty() && reason == nullptr);
    asset.TexCoords = new Array<float2>(3);
    (*asset.TexCoords)[0].X = .25f; (*asset.TexCoords)[0].Y = .75f;
    (*asset.TexCoords)[1].X = -1; (*asset.TexCoords)[1].Y = 2;
    (*asset.TexCoords)[2].X = 1; (*asset.TexCoords)[2].Y = 0;
    assert(BuildModelTextureCoordinates(asset, 3, coordinates, reason));
    assert((coordinates == std::vector<float>{.25f,.75f,-1,2,1,0})); // No V flip, texel scaling or clamping.
    CubeVertex vertices[3]{};
    vertices[1].Position[0] = 17;
    vertices[1].Color[3] = .5f;
    assert(CopyModelTextureCoordinates(coordinates, vertices, 3));
    assert(vertices[0].TexCoord[0] == .25f && vertices[0].TexCoord[1] == .75f);
    assert(vertices[1].TexCoord[0] == -1 && vertices[1].TexCoord[1] == 2);
    assert(vertices[2].TexCoord[0] == 1 && vertices[2].TexCoord[1] == 0);
    assert(vertices[1].Position[0] == 17 && vertices[1].Color[3] == .5f);
    assert(!CopyModelTextureCoordinates(coordinates, vertices, 2));
    assert(vertices[0].TexCoord[0] == .25f);
    assert(!CopyModelTextureCoordinates(coordinates, nullptr, 3));
    assert(CopyModelTextureCoordinates({}, vertices, 3));
    for (const auto& vertex : vertices) assert(vertex.TexCoord[0] == 0 && vertex.TexCoord[1] == 0);
    (*asset.TexCoords)[0].X = .5f;
    assert(coordinates[0] == .25f); // Copied ownership.
    assert(!BuildModelTextureCoordinates(asset, 4, coordinates, reason));
    assert(coordinates.empty() && reason != nullptr);
    for (float invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        (*asset.TexCoords)[1].Y = invalid;
        assert(!BuildModelTextureCoordinates(asset, 3, coordinates, reason));
        assert(coordinates.empty());
    }
    delete asset.TexCoords; asset.TexCoords = new Array<float2>(0);
    assert(BuildModelTextureCoordinates(asset, 3, coordinates, reason) && coordinates.empty());
    asset.Dispose();
    std::puts("Model UV copy preserves normalized authored values and rejects count/nonfinite errors.");
}
