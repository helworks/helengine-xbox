#pragma once

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

// Reading and releasing cooked assets.
//
// Shared by the 3D and 2D render managers, which both resolve cooked assets
// through IContentStreamSource during scene loading. Nothing here throws: a
// read that cannot be satisfied returns null and sets a static reason, which
// the caller logs before the generated runtime's own failure path is reached.
//
// The dynamic_cast in ReadCookedAssetAs is why this platform needs RTTI and
// its own __RTDynamicCast: AssetSerializer returns the base type and the
// concrete type is only recoverable at runtime.

#include <string>

#include "Asset.hpp"
#include "IContentStreamSource.hpp"
#include "ModelAsset.hpp"
#include "PlatformMaterialAsset.hpp"
#include "TextureAsset.hpp"

namespace helengine::xbox {

// Opens, deserializes and closes one cooked asset. Returns the base type.
::Asset* ReadCookedAsset(const std::string& path, ::IContentStreamSource* source,
                         const char*& reason);

// Releases the Asset base's own storage and deletes the object. Asset has a
// virtual destructor, so this is safe through the base pointer.
void ReleaseAssetStorage(::Asset* asset);

// ModelAsset and TextureAsset own arrays released through a non-virtual
// Dispose; PlatformMaterialAsset holds only value fields.
void DestroyAsset(::ModelAsset* asset);
void DestroyAsset(::TextureAsset* asset);
void DestroyAsset(::PlatformMaterialAsset* asset);

// For the type-mismatch path, where only the base type is known: the concrete
// type is recovered so an asset holding arrays still releases them.
void DestroyUnknownAsset(::Asset* asset);

// Reads one cooked asset and narrows it to the expected generated type.
template <typename TAsset>
TAsset* ReadCookedAssetAs(const std::string& path, ::IContentStreamSource* source,
                          const char*& reason) {
    ::Asset* asset = ReadCookedAsset(path, source, reason);
    if (asset == nullptr) {
        return nullptr;
    }
    TAsset* typed = dynamic_cast<TAsset*>(asset);
    if (typed == nullptr) {
        DestroyUnknownAsset(asset);
        reason = "cooked asset has an unexpected generated type";
        return nullptr;
    }
    return typed;
}

} // namespace helengine::xbox

#endif
