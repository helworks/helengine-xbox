#include "XboxRuntimeModel.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxDiagnostics.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "float3.hpp"
#include "runtime/array.hpp"

#include <windows.h>

namespace helengine::xbox {
namespace {

// Same bound the v0.1 renderer uses: GPU-visible allocations must sit below
// the address the NV2A can reach.
constexpr ULONG_PTR kGpuHighestAddress = 0x03FFAFFF;

bool Finite3(const ::float3& value) {
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

// Exactly one of the two index buffers must be populated. The engine emits 16-
// or 32-bit indices depending on vertex count; both present, or neither, means
// the asset is not what the cooker claims.
bool TrySourceIndexCount(const ::ModelAsset& asset, unsigned& count, const char*& reason) {
    const bool has16 = asset.Indices16 != nullptr && asset.Indices16->get_Length() != 0;
    const bool has32 = asset.Indices32 != nullptr && asset.Indices32->get_Length() != 0;
    if (has16 == has32) {
        reason = "model requires exactly one nonempty index buffer";
        return false;
    }
    const int32_t length = has16 ? asset.Indices16->get_Length() : asset.Indices32->get_Length();
    if (length <= 0 || (length % 3) != 0) {
        reason = "model index count is not a positive multiple of three";
        return false;
    }
    if (static_cast<unsigned>(length) / 3u > XboxRuntimeModel::MaxTriangles) {
        reason = "model exceeds the supported triangle count";
        return false;
    }
    count = static_cast<unsigned>(length);
    return true;
}

uint32_t ReadIndex(const ::ModelAsset& asset, unsigned index) {
    if (asset.Indices16 != nullptr && asset.Indices16->get_Length() != 0) {
        return (*asset.Indices16)[static_cast<int32_t>(index)];
    }
    return static_cast<uint32_t>((*asset.Indices32)[static_cast<int32_t>(index)]);
}

} // namespace

XboxRuntimeModel::XboxRuntimeModel() : ::RuntimeModel() {
}

XboxRuntimeModel::~XboxRuntimeModel() {
    Dispose();
}

XboxRuntimeModel* XboxRuntimeModel::Create(const ::ModelAsset& asset, const char*& reason) {
    reason = nullptr;

    unsigned indexCount = 0;
    if (!TrySourceIndexCount(asset, indexCount, reason)) {
        return nullptr;
    }
    if (asset.Positions == nullptr || asset.Positions->get_Length() <= 0) {
        reason = "model has no positions";
        return nullptr;
    }
    const unsigned vertexCount = static_cast<unsigned>(asset.Positions->get_Length());
    if (vertexCount > MaxVertices) {
        reason = "model exceeds the 16-bit index vertex limit";
        return nullptr;
    }
    for (unsigned index = 0; index < vertexCount; ++index) {
        if (!Finite3((*asset.Positions)[static_cast<int32_t>(index)])) {
            reason = "model position is not finite";
            return nullptr;
        }
    }
    // Every index must address a real vertex. An out-of-range index would have
    // the GPU fetch outside the allocation, which does not fault - it draws
    // whatever happens to be resident.
    for (unsigned index = 0; index < indexCount; ++index) {
        if (ReadIndex(asset, index) >= vertexCount) {
            reason = "model index is out of range";
            return nullptr;
        }
    }

    XboxRuntimeModel* model = new XboxRuntimeModel();
    const SIZE_T vertexBytes = static_cast<SIZE_T>(vertexCount) * sizeof(CubeVertex);
    model->gpuVertices_ = static_cast<CubeVertex*>(
        MmAllocateContiguousMemoryEx(vertexBytes, 0, kGpuHighestAddress, 0,
                                     PAGE_READWRITE | PAGE_WRITECOMBINE));
    if (model->gpuVertices_ == nullptr) {
        reason = "model vertex allocation failed";
        delete model;
        return nullptr;
    }
    model->indices_ = new unsigned short[indexCount];
    model->vertexCount_ = vertexCount;
    model->indexCount_ = indexCount;

    for (unsigned index = 0; index < vertexCount; ++index) {
        const ::float3& position = (*asset.Positions)[static_cast<int32_t>(index)];
        CubeVertex& vertex = model->gpuVertices_[index];
        vertex.Position[0] = position.X;
        vertex.Position[1] = position.Y;
        vertex.Position[2] = position.Z;
        vertex.Color[0] = 1.0f;
        vertex.Color[1] = 1.0f;
        vertex.Color[2] = 1.0f;
        vertex.Color[3] = 1.0f;
    }
    for (unsigned index = 0; index < indexCount; ++index) {
        model->indices_[index] = static_cast<unsigned short>(ReadIndex(asset, index));
    }

    model->SetBounds(asset.BoundsMin, asset.BoundsMax);
    Diagnostics::Log("model: %u verts %u tris at 0x%08lx", vertexCount, indexCount / 3u,
                     static_cast<unsigned long>(reinterpret_cast<uintptr_t>(model->gpuVertices_)));
    return model;
}

void XboxRuntimeModel::EnsureColor(float r, float g, float b, float a) {
    if (colorWritten_ && color_[0] == r && color_[1] == g && color_[2] == b && color_[3] == a) {
        return;
    }
    color_[0] = r;
    color_[1] = g;
    color_[2] = b;
    color_[3] = a;
    colorWritten_ = true;
    for (unsigned index = 0; index < vertexCount_; ++index) {
        CubeVertex& vertex = gpuVertices_[index];
        vertex.Color[0] = r;
        vertex.Color[1] = g;
        vertex.Color[2] = b;
        vertex.Color[3] = a;
    }
}

void XboxRuntimeModel::Dispose() {
    if (disposed_) {
        return;
    }
    disposed_ = true;
    if (gpuVertices_ != nullptr) {
        MmFreeContiguousMemory(gpuVertices_);
        gpuVertices_ = nullptr;
    }
    delete[] indices_;
    indices_ = nullptr;
    vertexCount_ = 0;
    indexCount_ = 0;
    ::RuntimeModel::Dispose();
}

} // namespace helengine::xbox

#endif
