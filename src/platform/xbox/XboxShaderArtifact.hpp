#pragma once
#include <cstddef>
#include <cstdint>

namespace helengine::xbox {
enum class XboxShaderStage : uint32_t { Vertex = 1, Pixel = 2 };

// Non-owning validated view. Its backing bytes must remain immutable and alive.
struct XboxShaderArtifactView {
    const uint8_t* Payload = nullptr;
    uint32_t WordCount = 0;
    XboxShaderStage Stage = XboxShaderStage::Vertex;
    const uint8_t* ConstantBindings = nullptr;
    uint32_t ConstantBindingCount = 0;
    // Format4 records: kind(1=image,2=sampler), name[64], physical stage, auxiliary, reserved.
    // Format5 VS records: kind3, float4 bits at4, zero padding20..67, register68, zero72..79.
    // Format6 also permits kind4/5 VS input/output and6/7 PS input/output:
    // canonical semantic[64] at4, NV2A route68, float component count72, zero76.
    const uint8_t* ResourceBindings = nullptr;
    uint32_t ResourceBindingCount = 0;
    bool HasPhysicalInterface = false;
};

constexpr size_t XboxShaderHeaderBytes = 128;
uint32_t ReadShaderWord(const uint8_t* bytes);
uint32_t ShaderPayloadChecksum(const uint8_t* bytes, size_t length);
bool ValidateShaderArtifact(const uint8_t* bytes, size_t length, XboxShaderStage expectedStage,
                            XboxShaderArtifactView& output, const char*& reason);
using XboxShaderRegisterWriter = void (*)(void* context, uint32_t method, uint32_t value);
// Validates the whole immutable buffer before the first callback.
bool ApplyPixelShaderArtifact(const uint8_t* bytes, size_t length, XboxShaderRegisterWriter writer,
                             void* context, const char*& reason);
}
