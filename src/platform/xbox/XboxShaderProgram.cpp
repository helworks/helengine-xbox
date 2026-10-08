#include "XboxShaderProgram.hpp"
#include <cstring>

namespace helengine::xbox {
bool XboxShaderProgram::ApplyVertexLiterals(float (&constants)[96][4], const char*& reason) const {
    XboxShaderArtifactView vertex{};
    if (!ReadVertexArtifact(vertex, reason)) return false;
    for (uint32_t i = 0; i < vertex.ResourceBindingCount; ++i) {
        const uint8_t* record = vertex.ResourceBindings + i * 80;
        if (ReadShaderWord(record) != 3) continue;
        const uint32_t reg = ReadShaderWord(record + 68);
        for (unsigned component = 0; component < 4; ++component) {
            const uint32_t bits = ReadShaderWord(record + 4 + component * 4);
            std::memcpy(&constants[reg][component], &bits, sizeof(bits));
        }
    }
    return true;
}
bool XboxShaderProgram::ReadPixelArtifact(XboxShaderArtifactView& output, const char*& reason) const {
    output = {};
    reason = "Shader program is not ready";
    return Ready && ValidateShaderArtifact(Pixel.data(), Pixel.size(), XboxShaderStage::Pixel, output, reason);
}
bool XboxShaderProgram::ReadVertexArtifact(XboxShaderArtifactView& output, const char*& reason) const {
    output = {};
    reason = "Shader program is not ready";
    return Ready && ValidateShaderArtifact(Vertex.data(), Vertex.size(), XboxShaderStage::Vertex, output, reason);
}
int XboxShaderProgram::FindVertexConstant(const char* name, uint32_t row) const {
    if (!Ready || name == nullptr) return -1;
    XboxShaderArtifactView vertex{};
    const char* reason = nullptr;
    if (!ValidateShaderArtifact(Vertex.data(), Vertex.size(), XboxShaderStage::Vertex, vertex, reason)) return -1;
    for (uint32_t i = 0; i < vertex.ConstantBindingCount; ++i) {
        const uint8_t* record = vertex.ConstantBindings + i * 72;
        if (row == ReadShaderWord(record + 64) && std::strcmp(name, reinterpret_cast<const char*>(record)) == 0)
            return static_cast<int>(ReadShaderWord(record + 68));
    }
    return -1;
}
bool XboxShaderProgram::Load(const uint8_t* vertex, size_t vertexLength, const uint8_t* pixel, size_t pixelLength, const char*& reason) {
    Ready = false;
    XboxShaderArtifactView vertexView{}, pixelView{};
    if (!ValidateShaderArtifact(vertex, vertexLength, XboxShaderStage::Vertex, vertexView, reason) ||
        !ValidateShaderArtifact(pixel, pixelLength, XboxShaderStage::Pixel, pixelView, reason)) return false;
    reason = "Shader pair physical interfaces disagree";
    if (vertexView.HasPhysicalInterface != pixelView.HasPhysicalInterface) return false;
    if (vertexView.HasPhysicalInterface) {
        for (uint32_t inputIndex = 0; inputIndex < pixelView.ResourceBindingCount; ++inputIndex) {
            const uint8_t* input = pixelView.ResourceBindings + inputIndex * 80;
            if (ReadShaderWord(input) != 6) continue;
            bool matched = false;
            for (uint32_t outputIndex = 0; outputIndex < vertexView.ResourceBindingCount; ++outputIndex) {
                const uint8_t* output = vertexView.ResourceBindings + outputIndex * 80;
                if (ReadShaderWord(output) == 5 && std::strcmp(reinterpret_cast<const char*>(input + 4),
                        reinterpret_cast<const char*>(output + 4)) == 0 &&
                    ReadShaderWord(input + 68) == ReadShaderWord(output + 68) &&
                    ReadShaderWord(input + 72) == ReadShaderWord(output + 72)) matched = true;
            }
            if (!matched) return false;
        }
    }
    std::vector<uint8_t> newVertex(vertex, vertex + vertexLength);
    std::vector<uint8_t> newPixel(pixel, pixel + pixelLength);
    Vertex.swap(newVertex);
    Pixel.swap(newPixel);
    Ready = true;
    reason = nullptr;
    return true;
}
bool XboxShaderProgram::Bind(XboxShaderInstructionWriter vertexWriter, XboxShaderRegisterWriter pixelWriter,
                             void* context, const char*& reason) const {
    reason = "Shader program is not ready or a GPU writer is missing";
    if (!Ready || vertexWriter == nullptr || pixelWriter == nullptr) return false;
    XboxShaderArtifactView vertex{}, pixel{};
    // Both stages must validate before either callback can touch GPU state.
    if (!ValidateShaderArtifact(Vertex.data(), Vertex.size(), XboxShaderStage::Vertex, vertex, reason) ||
        !ValidateShaderArtifact(Pixel.data(), Pixel.size(), XboxShaderStage::Pixel, pixel, reason)) return false;
    for (uint32_t i = 0; i < vertex.WordCount; i += 4) {
        uint32_t instruction[4];
        for (unsigned word = 0; word < 4; ++word) instruction[word] = ReadShaderWord(vertex.Payload + (i + word) * 4);
        vertexWriter(context, i / 4, instruction);
    }
    for (uint32_t i = 0; i < pixel.WordCount; i += 2)
        pixelWriter(context, ReadShaderWord(pixel.Payload + i * 4), ReadShaderWord(pixel.Payload + (i + 1) * 4));
    return true;
}
}
