#pragma once
#include <vector>
class ModelAsset;
namespace helengine::xbox {
struct CubeVertex;
// Copies authored normalized coordinates. Empty means the untextured model has no UV channel.
// Failure clears the output; callers must reject it before allocating or submitting GPU data.
bool BuildModelTextureCoordinates(const ::ModelAsset& asset, unsigned vertexCount,
                                 std::vector<float>& coordinates, const char*& reason);
// Copies a validated channel into the GPU vertex layout; absent UVs become zero.
// A count mismatch leaves the destination untouched.
bool CopyModelTextureCoordinates(const std::vector<float>& coordinates,
                                 CubeVertex* vertices, unsigned vertexCount);
}
