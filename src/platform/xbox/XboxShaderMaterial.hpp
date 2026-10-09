#pragma once
#include "XboxShaderProgram.hpp"
#include "XboxShaderConstants.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace helengine::xbox {
using XboxShaderConstantWriter = void (*)(void* context, const float (*constants)[4]);
// Per-material CPU constants; programs are immutable, shared and independently owned.
// A rejected mutation invalidates draw preparation rather than reusing stale values.
class XboxShaderMaterial {
public:
    bool Initialize(std::shared_ptr<const XboxShaderProgram> program, const std::string& matrixBuffer);
    bool SetVertexConstant(const std::string& name, const float* values, size_t floatCount);
    bool SetPixelConstant(const std::string& name, const float* values, size_t floatCount);
    bool BuildDrawConstants(const Matrix4& clip, unsigned width, unsigned height,
                            float textureWidth, float textureHeight, float (&output)[96][4]) const;
    bool Bind(const Matrix4& clip, unsigned width, unsigned height, float textureWidth, float textureHeight,
              XboxShaderInstructionWriter vertexWriter, XboxShaderRegisterWriter pixelWriter,
              XboxShaderConstantWriter constantWriter, void* context, const char*& reason) const;
    const XboxShaderProgram* Program() const { return Ready ? SharedProgram.get() : nullptr; }
private:
    std::shared_ptr<const XboxShaderProgram> SharedProgram;
    std::string MatrixBuffer;
    std::map<std::string, std::vector<float>> Constants;
    std::map<std::string, std::vector<float>> PixelConstants;
    bool Ready = false;
};
}
