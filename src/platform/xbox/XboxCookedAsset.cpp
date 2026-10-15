#include "XboxCookedAsset.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "AssetSerializer.hpp"
#include "runtime/array.hpp"
#include "system/io/stream.hpp"

namespace helengine::xbox {
namespace {

void CloseStream(::Stream* stream) {
    if (stream == nullptr) {
        return;
    }
    stream->Close();
    stream->Dispose();
    delete stream;
}

} // namespace

::Asset* ReadCookedAsset(const HeCppString& path, ::IContentStreamSource* source,
                         const char*& reason) {
    reason = nullptr;
    if (source == nullptr) {
        reason = "content stream source is null";
        return nullptr;
    }
    ::Stream* stream = source->OpenRead(path);
    if (stream == nullptr) {
        // The source has already logged the concrete file and failure.
        reason = "content stream source returned no stream";
        return nullptr;
    }
    ::Asset* asset = ::AssetSerializer::Deserialize(stream);
    CloseStream(stream);
    if (asset == nullptr) {
        reason = "cooked asset failed to deserialize";
        return nullptr;
    }
    return asset;
}

void ReleaseAssetStorage(::Asset* asset) {
    if (asset == nullptr) {
        return;
    }
    // Asset keeps a shared empty array for the common case; deleting that
    // would take the shared instance out from under every other asset, so only
    // an owned array is released here.
    if (asset->FormerAuthoringAssetIds != nullptr &&
        asset->FormerAuthoringAssetIds != ::Array<HeCppString>::Empty()) {
        delete asset->FormerAuthoringAssetIds;
    }
    asset->FormerAuthoringAssetIds = nullptr;
    delete asset; // Asset has a virtual destructor
}

void DestroyAsset(::ModelAsset* asset) {
    if (asset == nullptr) {
        return;
    }
    asset->Dispose();
    ReleaseAssetStorage(asset);
}

void DestroyAsset(::TextureAsset* asset) {
    if (asset == nullptr) {
        return;
    }
    asset->Dispose();
    ReleaseAssetStorage(asset);
}

void DestroyAsset(::PlatformMaterialAsset* asset) {
    ReleaseAssetStorage(asset);
}

void DestroyUnknownAsset(::Asset* asset) {
    if (::ModelAsset* model = dynamic_cast<::ModelAsset*>(asset)) {
        DestroyAsset(model);
        return;
    }
    if (::TextureAsset* texture = dynamic_cast<::TextureAsset*>(asset)) {
        DestroyAsset(texture);
        return;
    }
    ReleaseAssetStorage(asset);
}

} // namespace helengine::xbox

#endif
