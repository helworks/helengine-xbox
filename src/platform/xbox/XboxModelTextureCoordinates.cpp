#include "XboxModelTextureCoordinates.hpp"
#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
#include "ModelAsset.hpp"
#include "XboxCubeMesh.hpp"
#include <cmath>
namespace helengine::xbox {
bool CopyModelTextureCoordinates(const std::vector<float>& coordinates,
                                 CubeVertex* vertices, unsigned vertexCount) {
    if (vertices == nullptr || vertexCount == 0 || vertexCount > 65535 ||
        (!coordinates.empty() && coordinates.size() != static_cast<size_t>(vertexCount) * 2)) return false;
    for (unsigned i = 0; i < vertexCount; ++i) {
        vertices[i].TexCoord[0] = coordinates.empty() ? 0.0f : coordinates[i * 2];
        vertices[i].TexCoord[1] = coordinates.empty() ? 0.0f : coordinates[i * 2 + 1];
    }
    return true;
}
bool BuildModelTextureCoordinates(const ::ModelAsset& asset, unsigned vertexCount,
                                 std::vector<float>& coordinates, const char*& reason) {
    coordinates.clear();
    reason = "Model UV vertex count is outside the supported range";
    if (vertexCount == 0 || vertexCount > 65535) return false;
    if (asset.TexCoords == nullptr || asset.TexCoords->Length == 0) { reason = nullptr; return true; }
    reason = "Model UV count does not match its position count";
    if (asset.TexCoords->Length != static_cast<int32_t>(vertexCount)) return false;
    reason = "Model UV is not finite";
    for (unsigned i = 0; i < vertexCount; ++i) {
        const auto& uv = (*asset.TexCoords)[i];
        if (!RealEvaluation::IsFinite(uv.X) || !RealEvaluation::IsFinite(uv.Y)) return false;
    }
    coordinates.resize(static_cast<size_t>(vertexCount) * 2);
    for (unsigned i = 0; i < vertexCount; ++i) {
        coordinates[i * 2] = static_cast<float>((*asset.TexCoords)[i].X);
        coordinates[i * 2 + 1] = static_cast<float>((*asset.TexCoords)[i].Y);
    }
    reason = nullptr;
    return true;
}
}
#endif
