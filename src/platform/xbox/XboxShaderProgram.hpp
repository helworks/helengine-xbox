#pragma once
#include "XboxShaderArtifact.hpp"
#include <vector>

namespace helengine::xbox {
using XboxShaderInstructionWriter = void (*)(void* context, unsigned index, const uint32_t* instruction);

// Owns both shader stages. Failed replacement invalidates the program instead
// of silently falling back to the previous material's shader.
class XboxShaderProgram {
public:
    bool Load(const uint8_t* vertex, size_t vertexLength, const uint8_t* pixel, size_t pixelLength, const char*& reason);
    bool Bind(XboxShaderInstructionWriter vertexWriter, XboxShaderRegisterWriter pixelWriter,
              void* context, const char*& reason) const;
    bool IsReady() const { return Ready; }
    // Resolves compiler-assigned constants; absent rows and invalid programs fail closed.
    int FindVertexConstant(const char* name, uint32_t row) const;
    // Borrows validated immutable data for this call; never retain across Load or destruction.
    bool ReadVertexArtifact(XboxShaderArtifactView& output, const char*& reason) const;
    bool ReadPixelArtifact(XboxShaderArtifactView& output, const char*& reason) const;
    // Applies only validated compiler-owned rows, preserving all caller-owned constants.
    bool ApplyVertexLiterals(float (&constants)[96][4], const char*& reason) const;
private:
    std::vector<uint8_t> Vertex;
    std::vector<uint8_t> Pixel;
    bool Ready = false;
};
}
