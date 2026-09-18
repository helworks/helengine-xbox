#include "XboxRuntimeMaterial.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "MaterialBlendMode.hpp"
#include "MaterialCullMode.hpp"
#include "MaterialRenderState.hpp"

namespace helengine::xbox {

XboxRuntimeMaterial::XboxRuntimeMaterial() : ::RuntimeMaterial() {
    set_LightingModel(::RuntimeMaterialLightingModel::Unlit);
    set_SupportsNormalMapping(false);
    set_SupportsEmissive(false);
    set_CastsShadows(false);
    set_ReceivesShadows(false);
}

XboxRuntimeMaterial::~XboxRuntimeMaterial() {
    Dispose();
}

XboxRuntimeMaterial* XboxRuntimeMaterial::Create(const ::PlatformMaterialAsset& asset,
                                                 const char*& reason) {
    reason = nullptr;

    // Each of these would otherwise render as flat opaque white, which reads as
    // a working renderer rather than as an unimplemented feature, so the
    // unsupported cases are named individually.
    if (asset.RendererFamilyId != RendererFamilyId) {
        reason = "material was cooked for another renderer family";
        return nullptr;
    }
    if (!asset.TextureRelativePath.empty()) {
        reason = "textured materials are not supported yet";
        return nullptr;
    }
    if (asset.UseVertexColor) {
        reason = "vertex-coloured materials are not supported yet";
        return nullptr;
    }
    if (asset.DoubleSided) {
        reason = "double-sided materials are not supported yet";
        return nullptr;
    }
    if (asset.BaseColorA != 255) {
        reason = "translucent materials are not supported yet";
        return nullptr;
    }
    if (asset.Lit) {
        reason = "lit materials are not supported yet";
        return nullptr;
    }

    XboxRuntimeMaterial* material = new XboxRuntimeMaterial();
    material->set_Id(asset.Id);
    material->color_[0] = static_cast<float>(asset.BaseColorR) / 255.0f;
    material->color_[1] = static_cast<float>(asset.BaseColorG) / 255.0f;
    material->color_[2] = static_cast<float>(asset.BaseColorB) / 255.0f;
    material->color_[3] = 1.0f;

    ::MaterialRenderState* state = material->get_RenderState();
    state->set_BlendMode(::MaterialBlendMode::Opaque);
    state->set_CullMode(::MaterialCullMode::Back);
    state->set_DepthTestEnabled(true);
    state->set_DepthWriteEnabled(true);
    return material;
}

void XboxRuntimeMaterial::Dispose() {
    if (disposed_) {
        return;
    }
    disposed_ = true;
    ::RuntimeMaterial::Dispose();
}

} // namespace helengine::xbox

#endif
