#include "XboxShaderMaterial.hpp"
#include <cmath>
#include <cstring>
#include <pbkit/nv_regs.h>
namespace helengine::xbox {
bool XboxShaderMaterial::Bind(const Matrix4& clip, unsigned width, unsigned height, float textureWidth, float textureHeight,
                              XboxShaderInstructionWriter vertexWriter, XboxShaderRegisterWriter pixelWriter,
                              XboxShaderConstantWriter constantWriter, void* context, const char*& reason) const {
    reason = "Material constants are incomplete or a GPU writer is missing";
    float constants[96][4];
    if (vertexWriter == nullptr || pixelWriter == nullptr || constantWriter == nullptr ||
        !BuildDrawConstants(clip, width, height, textureWidth, textureHeight, constants)) return false;
    XboxShaderArtifactView pixel{};
    if (!SharedProgram->ReadPixelArtifact(pixel, reason)) return false;
    uint32_t factors[2] = {};
    for (uint32_t i = 0; i < pixel.ConstantBindingCount; ++i) {
        const uint8_t* record = pixel.ConstantBindings + i * 72;
        const auto value = PixelConstants.find(reinterpret_cast<const char*>(record));
        const uint32_t row = ReadShaderWord(record + 64);
        if (value == PixelConstants.end() || value->second.size() < (size_t(row) + 1) * 4) {
            reason = "Pixel constants are incomplete";
            return false;
        }
        const float* rgba = value->second.data() + row * 4;
        // Match nxdk's unsigned combiner constant conversion: ARGB8, truncated.
        factors[i] = (uint32_t(rgba[3] * 255) << 24) | (uint32_t(rgba[0] * 255) << 16) |
                     (uint32_t(rgba[1] * 255) << 8) | uint32_t(rgba[2] * 255);
    }
    if (!SharedProgram->Bind(vertexWriter, pixelWriter, context, reason)) return false;
    for (uint32_t i = 0; i < pixel.ConstantBindingCount; ++i) {
        const uint32_t factor = ReadShaderWord(pixel.ConstantBindings + i * 72 + 68);
        pixelWriter(context, factor == 0 ? NV097_SET_COMBINER_FACTOR0 : NV097_SET_COMBINER_FACTOR1, factors[i]);
        pixelWriter(context, NV097_SET_SPECULAR_FOG_FACTOR + factor * 4, factors[i]);
    }
    constantWriter(context, constants);
    reason = nullptr;
    return true;
}
namespace {
bool RuntimeConstant(const std::string& name) {
    return name == "gl_HalfPixel" || name == "Xbox_TextureExtent" || name == "Xbox_ViewportScale" || name == "Xbox_ViewportOffset";
}
}
bool XboxShaderMaterial::Initialize(std::shared_ptr<const XboxShaderProgram> program, const std::string& matrixBuffer) {
    Ready = false;
    SharedProgram.reset();
    Constants.clear();
    PixelConstants.clear();
    XboxShaderArtifactView vertex{};
    const char* reason = nullptr;
    if (!program || !program->ReadVertexArtifact(vertex, reason) || vertex.ConstantBindingCount == 0 || RuntimeConstant(matrixBuffer)) return false;
    if (!matrixBuffer.empty()) {
        for (unsigned row = 0; row < 4; ++row) if (program->FindVertexConstant(matrixBuffer.c_str(), row) < 0) return false;
        for (unsigned row = 4; row < 96; ++row) if (program->FindVertexConstant(matrixBuffer.c_str(), row) >= 0) return false;
    }
    SharedProgram = std::move(program);
    MatrixBuffer = matrixBuffer;
    Ready = true;
    return true;
}
bool XboxShaderMaterial::SetPixelConstant(const std::string& name, const float* values, size_t floatCount) {
    const bool wasReady = Ready;
    Ready = false;
    if (!wasReady || values == nullptr || floatCount == 0 || floatCount > 96 * 4 || floatCount % 4 != 0) return false;
    XboxShaderArtifactView pixel{};
    const char* reason = nullptr;
    if (!SharedProgram->ReadPixelArtifact(pixel, reason)) return false;
    size_t required = 0;
    for (uint32_t i = 0; i < pixel.ConstantBindingCount; ++i) {
        const uint8_t* record = pixel.ConstantBindings + i * 72;
        if (name == reinterpret_cast<const char*>(record)) {
            const size_t end = (size_t(ReadShaderWord(record + 64)) + 1) * 4;
            if (end > required) required = end;
        }
    }
    if (required != floatCount) return false;
    for (size_t i = 0; i < floatCount; ++i)
        if (!std::isfinite(values[i]) || values[i] < 0 || values[i] > 1) return false;
    PixelConstants[name] = std::vector<float>(values, values + floatCount);
    Ready = true;
    return true;
}
bool XboxShaderMaterial::SetVertexConstant(const std::string& name, const float* values, size_t floatCount) {
    const bool wasReady = Ready;
    Ready = false;
    if (!wasReady || values == nullptr || floatCount == 0 || floatCount > 96 * 4 || floatCount % 4 != 0 ||
        name == MatrixBuffer || RuntimeConstant(name)) return false;
    XboxShaderArtifactView vertex{};
    const char* reason = nullptr;
    if (!SharedProgram->ReadVertexArtifact(vertex, reason)) return false;
    size_t required = 0;
    for (uint32_t i = 0; i < vertex.ConstantBindingCount; ++i) {
        const uint8_t* record = vertex.ConstantBindings + i * 72;
        if (name == reinterpret_cast<const char*>(record)) {
            const size_t end = (size_t(ReadShaderWord(record + 64)) + 1) * 4;
            if (end > required) required = end;
        }
    }
    if (required != floatCount) return false;
    for (size_t i = 0; i < floatCount; ++i) if (!std::isfinite(values[i])) return false;
    Constants[name] = std::vector<float>(values, values + floatCount);
    Ready = true;
    return true;
}
bool XboxShaderMaterial::BuildDrawConstants(const Matrix4& clip, unsigned width, unsigned height,
                                           float textureWidth, float textureHeight, float (&output)[96][4]) const {
    if (!Ready) return false;
    XboxShaderArtifactView vertex{};
    const char* reason = nullptr;
    if (!SharedProgram->ReadVertexArtifact(vertex, reason)) return false;
    float staged[96][4];
    if (!BuildShaderDrawConstants(clip, -1, width, height, textureWidth, textureHeight, staged)) return false;
    if (!SharedProgram->ApplyVertexLiterals(staged, reason)) return false;
    for (uint32_t i = 0; i < vertex.ConstantBindingCount; ++i) {
        const uint8_t* record = vertex.ConstantBindings + i * 72;
        const std::string name(reinterpret_cast<const char*>(record));
        const uint32_t row = ReadShaderWord(record + 64), reg = ReadShaderWord(record + 68);
        if (RuntimeConstant(name)) {
            if (row != 0) return false;
        } else if (name == MatrixBuffer) {
            if (row >= 4) return false;
            for (unsigned column = 0; column < 4; ++column) staged[reg][column] = clip.Values[column * 4 + row];
        } else {
            auto value = Constants.find(name);
            if (value == Constants.end() || value->second.size() < (size_t(row) + 1) * 4) return false;
            std::memcpy(staged[reg], value->second.data() + row * 4, 4 * sizeof(float));
        }
    }
    std::memcpy(output, staged, sizeof(staged));
    return true;
}
}
