#pragma once
#include "XboxShaderRuntimeConfig.hpp"
#if defined(HELENGINE_XBOX_HAS_SHADER_RUNTIME)
#include "ShaderRuntimeMaterial.hpp"
#include "XboxShaderMaterial.hpp"
#include <vector>
class ShaderMaterialAsset;
class ShaderAsset;
namespace helengine::xbox {
// Generated-material adapter: owns layout/default values and copied native program data.
class XboxShaderRuntimeMaterial final : public ::ShaderRuntimeMaterial {
public:
    static XboxShaderRuntimeMaterial* Create(const ::ShaderMaterialAsset& material, const ::ShaderAsset& shader, const char*& reason);
    ~XboxShaderRuntimeMaterial() override;
    void Dispose() override;
    // Refreshes inherited property values before every draw, preventing shared-model state leaks.
    bool Prepare(const char*& reason);
    const XboxShaderMaterial& NativeMaterial() const { return Native; }
    std::shared_ptr<XboxShaderProgram> SharedProgram() const { return Program; }
    bool RequiresTexture() const { return Textured; }
private:
    XboxShaderMaterial Native;
    std::shared_ptr<XboxShaderProgram> Program;
    std::string MatrixBuffer;
    std::vector<std::string> VertexBuffers;
    std::vector<std::string> PixelBuffers;
    bool Disposed = false;
    bool Textured = false;
};
}
#endif
