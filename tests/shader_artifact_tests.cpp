#include "platform/xbox/XboxShaderArtifact.hpp"
#include "platform/xbox/XboxShaderProgram.hpp"
#include <pbkit/nv_regs.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>

using namespace helengine::xbox;
static void Word(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
static void Checksum(std::vector<uint8_t>& bytes) {
    Word(bytes, 20, ShaderPayloadChecksum(bytes.data() + 128, bytes.size() - 128));
}
static void Interface(std::vector<uint8_t>& bytes, uint32_t kind, const char* semantic, uint32_t reg, uint32_t components) {
    const size_t offset = bytes.size();
    bytes.resize(offset + 80, 0);
    Word(bytes, offset, kind);
    for (unsigned i = 0; semantic[i] != 0; ++i) bytes[offset + 4 + i] = semantic[i];
    Word(bytes, offset + 68, reg); Word(bytes, offset + 72, components);
    Word(bytes, 4, 6); Word(bytes, 124, ReadShaderWord(bytes.data() + 124) + 1);
    Checksum(bytes);
}
static std::vector<uint8_t> Pixel() {
    std::vector<uint8_t> bytes(128 + 9 * 8, 0);
    Word(bytes, 0, 0x31535848); // HXS1
    Word(bytes, 4, 1);
    Word(bytes, 8, 2);
    Word(bytes, 12, 18);
    Word(bytes, 16, 2);
    Word(bytes, 128, NV097_SET_SHADER_STAGE_PROGRAM);
    Word(bytes, 136, NV097_SET_SHADER_OTHER_STAGE_INPUT);
    Word(bytes, 144, NV097_SET_COMBINER_CONTROL);
    Word(bytes, 148, 1);
    Word(bytes, 152, NV097_SET_COMBINER_COLOR_ICW);
    Word(bytes, 160, NV097_SET_COMBINER_SPECULAR_FOG_CW0);
    Word(bytes, 168, NV097_SET_COMBINER_SPECULAR_FOG_CW1);
    Word(bytes, 176, NV097_SET_COMBINER_COLOR_OCW);
    Word(bytes, 184, NV097_SET_COMBINER_ALPHA_ICW);
    Word(bytes, 192, NV097_SET_COMBINER_ALPHA_OCW);
    Checksum(bytes);
    return bytes;
}
static bool Valid(const std::vector<uint8_t>& bytes, XboxShaderStage stage = XboxShaderStage::Pixel) {
    XboxShaderArtifactView view{};
    const char* reason = nullptr;
    bool result = ValidateShaderArtifact(bytes.data(), bytes.size(), stage, view, reason);
    if (result) {
        assert(reason == nullptr && view.Payload == bytes.data() + 128);
        assert(view.WordCount * 4 + view.ConstantBindingCount * 72 + ReadShaderWord(bytes.data() + 124) * 80 == bytes.size() - 128 && view.Stage == stage);
        assert(view.ResourceBindingCount == ReadShaderWord(bytes.data() + 124));
        if (view.ResourceBindingCount != 0)
            assert(view.ResourceBindings == bytes.data() + 128 + view.WordCount * 4 + view.ConstantBindingCount * 72);
    } else {
        assert(reason != nullptr && view.Payload == nullptr && view.WordCount == 0);
    }
    return result;
}
static void CountWrites(void* context, uint32_t, uint32_t) {
    ++*static_cast<unsigned*>(context);
}
struct PixelState {
    uint32_t Program = 0xffffffff;
    uint32_t Selectors = 0xffffffff;
};
static void CaptureState(void* context, uint32_t method, uint32_t value) {
    auto& state = *static_cast<PixelState*>(context);
    if (method == NV097_SET_SHADER_STAGE_PROGRAM) state.Program = value;
    if (method == NV097_SET_SHADER_OTHER_STAGE_INPUT) state.Selectors = value;
}
static void CountInstruction(void* context, unsigned, const uint32_t*) {
    ++*static_cast<unsigned*>(context);
}
struct BoundPairState {
    unsigned Instructions = 0;
    PixelState Pixel;
};
static void CaptureInstruction(void* context, unsigned index, const uint32_t* instruction) {
    auto& state = *static_cast<BoundPairState*>(context);
    assert(instruction[0] == 0);
    if (index == 0) state.Instructions = 0;
    ++state.Instructions;
}
static void CapturePixel(void* context, uint32_t method, uint32_t value) {
    CaptureState(&static_cast<BoundPairState*>(context)->Pixel, method, value);
}
int main(int argc, char** argv) {
    // Compiler-owned RGBA factors may target either bounded eight-stage bank
    // or the two final-combiner factors, never adjacent or unaligned methods.
    for (uint32_t method : {uint32_t(NV097_SET_COMBINER_FACTOR0), uint32_t(NV097_SET_COMBINER_FACTOR0 + 28),
            uint32_t(NV097_SET_COMBINER_FACTOR1), uint32_t(NV097_SET_COMBINER_FACTOR1 + 28),
            uint32_t(NV097_SET_SPECULAR_FOG_FACTOR), uint32_t(NV097_SET_SPECULAR_FOG_FACTOR + 4),
            uint32_t(NV097_SET_COMBINER_FACTOR0 + 1), uint32_t(NV097_SET_COMBINER_FACTOR1 + 33),
            uint32_t(NV097_SET_SPECULAR_FOG_FACTOR + 8)}) {
        auto factor = Pixel();
        factor.resize(factor.size() + 8, 0);
        Word(factor, 12, 20); Word(factor, 200, method); Word(factor, 204, 0xcc336699);
        Checksum(factor);
        const bool expected = method != NV097_SET_COMBINER_FACTOR0 + 1 &&
            method != NV097_SET_COMBINER_FACTOR1 + 33 && method != NV097_SET_SPECULAR_FOG_FACTOR + 8;
        assert(Valid(factor) == expected);
    }
    // Format 2 carries physical constant rows after the instruction words.
    std::vector<uint8_t> mapped(128 + 16 + 3 * 72, 0);
    Word(mapped, 0, 0x31535848); Word(mapped, 4, 2); Word(mapped, 8, 1);
    Word(mapped, 12, 4); Word(mapped, 16, 2); Word(mapped, 140, 1); Word(mapped, 120, 3);
    const char* names[] = {"Transform", "Xbox_ViewportScale", "Xbox_ViewportOffset"};
    const uint32_t registers[] = {12, 94, 95};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned c = 0; names[i][c]; ++c) mapped[144 + i * 72 + c] = names[i][c];
        Word(mapped, 144 + i * 72 + 68, registers[i]);
    }
    auto literal = mapped;
    const size_t literalOffset = literal.size();
    literal.resize(literalOffset + 80, 0);
    Word(literal, 4, 5); Word(literal, 124, 1);
    Word(literal, literalOffset, 3); Word(literal, literalOffset + 68, 5);
    Word(literal, literalOffset + 4, 0x3e800000); // .25
    Word(literal, literalOffset + 8, 0x3f000000); // .5
    Word(literal, literalOffset + 12, 0x3f400000); // .75
    Word(literal, literalOffset + 16, 0x3f600000); // .875
    Checksum(literal);
    assert(Valid(literal, XboxShaderStage::Vertex));
    auto physicalVertex = literal;
    Interface(physicalVertex, 4, "POSITION", 0, 3);
    Interface(physicalVertex, 4, "COLOR0", 3, 4);
    Interface(physicalVertex, 5, "POSITION", 0, 4);
    Interface(physicalVertex, 5, "COLOR0", 3, 4);
    assert(Valid(physicalVertex, XboxShaderStage::Vertex));
    auto batchVertex = literal;
    Interface(batchVertex, 4, "POSITION", 0, 4);
    Interface(batchVertex, 4, "TEXCOORD0", 9, 4);
    Interface(batchVertex, 5, "POSITION", 0, 4);
    assert(Valid(batchVertex, XboxShaderStage::Vertex));
    for (unsigned width : {0u, 1u, 2u, 5u}) {
        auto malformed = batchVertex;
        Word(malformed, literal.size() + 72, width); Checksum(malformed);
        assert(!Valid(malformed, XboxShaderStage::Vertex));
    }
    for (unsigned width : {0u, 1u, 3u, 5u}) {
        auto malformed = batchVertex;
        Word(malformed, literal.size() + 80 + 72, width); Checksum(malformed);
        assert(!Valid(malformed, XboxShaderStage::Vertex));
    }
    auto physicalPixel = Pixel();
    Interface(physicalPixel, 6, "COLOR0", 3, 4);
    Interface(physicalPixel, 7, "COLOR0", 0, 4);
    assert(Valid(physicalPixel));
    XboxShaderProgram physicalProgram;
    const char* physicalReason = nullptr;
    assert(physicalProgram.Load(physicalVertex.data(), physicalVertex.size(), physicalPixel.data(), physicalPixel.size(), physicalReason));
    float physicalConstants[96][4]{};
    physicalConstants[0][0] = 123;
    assert(physicalProgram.ApplyVertexLiterals(physicalConstants, physicalReason));
    assert(physicalConstants[0][0] == 123 && physicalConstants[5][0] == .25f);
    auto missingVaryingPixel = physicalPixel;
    Interface(missingVaryingPixel, 6, "TEXCOORD0", 9, 2);
    assert(Valid(missingVaryingPixel));
    assert(!physicalProgram.Load(physicalVertex.data(), physicalVertex.size(), missingVaryingPixel.data(), missingVaryingPixel.size(), physicalReason));
    assert(!physicalProgram.IsReady());
    auto legacyPixel = Pixel();
    assert(!physicalProgram.Load(physicalVertex.data(), physicalVertex.size(), legacyPixel.data(), legacyPixel.size(), physicalReason));
    auto wrongRoute = physicalVertex;
    Word(wrongRoute, wrongRoute.size() - 12, 9); Checksum(wrongRoute);
    assert(!Valid(wrongRoute, XboxShaderStage::Vertex));
    auto wrongWidth = physicalPixel;
    Word(wrongWidth, wrongWidth.size() - 8, 3); Checksum(wrongWidth);
    assert(!Valid(wrongWidth));
    auto duplicateInterface = physicalVertex;
    Interface(duplicateInterface, 5, "COLOR0", 3, 4);
    assert(!Valid(duplicateInterface, XboxShaderStage::Vertex));
    auto missingPosition = literal;
    Interface(missingPosition, 5, "COLOR0", 3, 4);
    assert(!Valid(missingPosition, XboxShaderStage::Vertex));
    auto wrongStage = physicalPixel;
    Interface(wrongStage, 4, "POSITION", 0, 3);
    assert(!Valid(wrongStage));
    for (uint32_t reg : {12u, 93u, 94u, 95u, 96u}) {
        auto invalid = literal; Word(invalid, literalOffset + 68, reg); Checksum(invalid);
        assert(!Valid(invalid, XboxShaderStage::Vertex));
    }
    for (uint32_t bits : {0x7f800000u, 0xff800000u, 0x7fc00000u}) {
        auto invalid = literal; Word(invalid, literalOffset + 4, bits); Checksum(invalid);
        assert(!Valid(invalid, XboxShaderStage::Vertex));
    }
    for (size_t offset : {literalOffset, literalOffset + 20, literalOffset + 64, literalOffset + 72, literalOffset + 76}) {
        auto invalid = literal; Word(invalid, offset, 1); Checksum(invalid);
        assert(!Valid(invalid, XboxShaderStage::Vertex));
    }
    auto duplicateLiteral = literal;
    duplicateLiteral.insert(duplicateLiteral.end(), literal.begin() + literalOffset, literal.end());
    Word(duplicateLiteral, 124, 2); Checksum(duplicateLiteral);
    assert(!Valid(duplicateLiteral, XboxShaderStage::Vertex));
    for (size_t length = literalOffset; length < literal.size(); ++length) {
        auto truncated = literal; truncated.resize(length); Checksum(truncated);
        assert(!Valid(truncated, XboxShaderStage::Vertex));
    }
    Checksum(mapped);
    XboxShaderArtifactView mappedView{};
    const char* mappedReason = nullptr;
    assert(ValidateShaderArtifact(mapped.data(), mapped.size(), XboxShaderStage::Vertex, mappedView, mappedReason));
    assert(mappedView.ConstantBindingCount == 3 && mappedView.ConstantBindings == mapped.data() + 144);
    assert(ReadShaderWord(mappedView.ConstantBindings + 68) == 12);
    for (uint32_t invalidRegister : {94u, 96u}) {
        auto collision = mapped;
        Word(collision, 144 + 68, invalidRegister); Checksum(collision);
        assert(!ValidateShaderArtifact(collision.data(), collision.size(), XboxShaderStage::Vertex, mappedView, mappedReason));
    }
    auto invalidName = mapped; invalidName[144] = 0; Checksum(invalidName);
    assert(!ValidateShaderArtifact(invalidName.data(), invalidName.size(), XboxShaderStage::Vertex, mappedView, mappedReason));
    auto incomplete = mapped; incomplete.pop_back(); Checksum(incomplete);
    assert(!ValidateShaderArtifact(incomplete.data(), incomplete.size(), XboxShaderStage::Vertex, mappedView, mappedReason));
    auto corruptedBinding = mapped; corruptedBinding[144 + 68] ^= 1;
    assert(!ValidateShaderArtifact(corruptedBinding.data(), corruptedBinding.size(), XboxShaderStage::Vertex, mappedView, mappedReason));
    auto pixel = Pixel();
    auto textured = pixel;
    textured.resize(208 + 160, 0);
    Word(textured, 4, 4); Word(textured, 12, 20); Word(textured, 124, 2);
    Word(textured, 200, NV097_SET_SHADER_STAGE_PROGRAM); Word(textured, 204, 1);
    Word(textured, 208, 1); Word(textured, 288, 2);
    const char* textureNames[] = {"Albedo", "AlbedoSampler"};
    for (unsigned r = 0; r < 2; ++r)
        for (unsigned c = 0; textureNames[r][c]; ++c) textured[212 + r * 80 + c] = textureNames[r][c];
    Checksum(textured);
    assert(Valid(textured)); // Format4 retains both original resources at the compiler-assigned stage0.
    for (size_t length = 128; length < textured.size(); ++length) {
        auto truncated = textured; truncated.resize(length);
        assert(!Valid(truncated));
    }
    for (size_t offset : {size_t(204), size_t(208), size_t(288)}) {
        auto invalid = textured; Word(invalid, offset, 0); Checksum(invalid); assert(!Valid(invalid));
    }
    for (size_t offset : {size_t(276), size_t(280), size_t(284), size_t(356), size_t(360), size_t(364)}) {
        auto invalid = textured; Word(invalid, offset, 1); Checksum(invalid); assert(!Valid(invalid));
    }
    auto duplicateTexture = textured; Word(duplicateTexture, 288, 1); Checksum(duplicateTexture); assert(!Valid(duplicateTexture));
    auto invalidTextureName = textured; invalidTextureName[212] = '-'; Checksum(invalidTextureName); assert(!Valid(invalidTextureName));
    auto uniformPixel = pixel;
    uniformPixel.resize(200 + 72, 0);
    Word(uniformPixel, 4, 3); Word(uniformPixel, 120, 1);
    const char* tintName = "Tint";
    for (unsigned i = 0; tintName[i]; ++i) uniformPixel[200 + i] = tintName[i];
    Word(uniformPixel, 200 + 68, 1); Checksum(uniformPixel);
    assert(Valid(uniformPixel)); // Format 3 maps a PS row to global combiner factor 1.
    auto texturedUniform = textured;
    texturedUniform.insert(texturedUniform.begin() + 208, uniformPixel.begin() + 200, uniformPixel.end());
    Word(texturedUniform, 120, 1); Checksum(texturedUniform);
    assert(Valid(texturedUniform)); // Constants and resources occupy separate bounded tables.
    auto badFactor = uniformPixel; Word(badFactor, 268, 2); Checksum(badFactor);
    assert(!Valid(badFactor));
    badFactor = uniformPixel; Word(badFactor, 148, 0x1001); Checksum(badFactor);
    assert(!Valid(badFactor)); // Global uniform map must not be interpreted as per-stage constants.
    badFactor = uniformPixel; Word(badFactor, 4, 2); Checksum(badFactor);
    assert(!Valid(badFactor)); // Older readers must not mistake the new table for an old pixel artifact.
    badFactor = uniformPixel; badFactor.resize(200 + 144, 0); Word(badFactor, 120, 2);
    for (unsigned i = 0; tintName[i]; ++i) badFactor[272 + i] = tintName[i];
    Word(badFactor, 272 + 68, 0); Checksum(badFactor);
    assert(!Valid(badFactor)); // A uniform row cannot select two independent factors.
    XboxShaderProgram mappedProgram;
    assert(mappedProgram.Load(mapped.data(), mapped.size(), pixel.data(), pixel.size(), mappedReason));
    assert(mappedProgram.FindVertexConstant("Transform", 0) == 12);
    assert(mappedProgram.FindVertexConstant("Transform", 1) == -1);
    assert(mappedProgram.FindVertexConstant("Unknown", 0) == -1);
    assert(mappedProgram.FindVertexConstant(nullptr, 0) == -1);
    assert(!mappedProgram.Load(incomplete.data(), incomplete.size(), pixel.data(), pixel.size(), mappedReason));
    assert(mappedProgram.FindVertexConstant("Transform", 0) == -1);
    assert(Valid(pixel));
    auto legacyAbi = pixel;
    Word(legacyAbi, 16, 1);
    assert(!Valid(legacyAbi));
    for (size_t length = 0; length < pixel.size(); ++length) {
        auto truncated = pixel;
        truncated.resize(length);
        assert(!Valid(truncated));
    }
    assert(!Valid(pixel, XboxShaderStage::Vertex));
    for (size_t offset : {size_t(0), size_t(4), size_t(8), size_t(12), size_t(16), size_t(120), size_t(124)}) {
        auto bad = pixel;
        Word(bad, offset, 0xffffffff);
        assert(!Valid(bad));
    }
    auto bad = pixel;
    bad.back() ^= 1;
    assert(!Valid(bad));
    bad = pixel;
    Word(bad, 152, NV097_SET_BLEND_ENABLE);
    Checksum(bad);
    assert(!Valid(bad));
    bad = pixel;
    Word(bad, 128, NV097_SET_COMBINER_COLOR_ICW);
    Checksum(bad);
    assert(!Valid(bad));
    bad = pixel;
    Word(bad, 132, 1); // Baseline must be disabled, before any compiler overrides.
    Checksum(bad);
    assert(!Valid(bad));
    bad = pixel;
    bad.push_back(0);
    assert(!Valid(bad));
    bad = pixel;
    Word(bad, 144, NV097_SET_COMBINER_COLOR_ICW); // Missing iteration control.
    Checksum(bad);
    assert(!Valid(bad));
    bad = pixel;
    Word(bad, 192, NV097_SET_COMBINER_COLOR_ICW); // Missing active alpha output.
    Checksum(bad);
    assert(!Valid(bad));
    unsigned writes = 0;
    const char* reason = nullptr;
    assert(!ApplyPixelShaderArtifact(bad.data(), bad.size(), CountWrites, &writes, reason));
    assert(writes == 0);
    assert(ApplyPixelShaderArtifact(pixel.data(), pixel.size(), CountWrites, &writes, reason));
    assert(writes == 9);
    assert(!ApplyPixelShaderArtifact(pixel.data(), pixel.size(), nullptr, nullptr, reason));
    auto vertex = pixel;
    vertex.resize(144);
    Word(vertex, 8, 1);
    Word(vertex, 12, 4);
    Word(vertex, 128, 0);
    Word(vertex, 132, 0);
    Word(vertex, 136, 0);
    Word(vertex, 140, 1);
    Checksum(vertex);
    assert(Valid(vertex, XboxShaderStage::Vertex));
    XboxShaderProgram program;
    assert(program.Load(vertex.data(), vertex.size(), pixel.data(), pixel.size(), reason));
    unsigned callbacks = 0;
    assert(program.Bind(CountInstruction, CountWrites, &callbacks, reason));
    assert(callbacks == 10); // One instruction plus nine pixel writes.
    auto broken = pixel;
    broken.back() ^= 1;
    assert(!program.Load(vertex.data(), vertex.size(), broken.data(), broken.size(), reason));
    assert(!program.IsReady());
    callbacks = 0;
    assert(!program.Bind(CountInstruction, CountWrites, &callbacks, reason) && callbacks == 0);
    Word(vertex, 140, 0);
    Checksum(vertex);
    assert(!Valid(vertex, XboxShaderStage::Vertex));
    std::vector<std::vector<uint8_t>> pixelArtifacts;
    std::vector<std::vector<uint8_t>> vertexArtifacts;
    for (int i = 1; i < argc; ++i) {
        std::ifstream input(argv[i], std::ios::binary);
        assert(input.good());
        std::vector<uint8_t> actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        assert(actual.size() >= 128);
        const auto stage = static_cast<XboxShaderStage>(ReadShaderWord(actual.data() + 8));
        assert(Valid(actual, stage));
        if (stage == XboxShaderStage::Pixel) {
            writes = 0;
            assert(ApplyPixelShaderArtifact(actual.data(), actual.size(), CountWrites, &writes, reason));
            assert(writes == ReadShaderWord(actual.data() + 12) / 2); // Metadata records are not GPU commands.
            pixelArtifacts.push_back(actual);
        } else {
            vertexArtifacts.push_back(actual);
        }
    }
    if (argc > 1) {
        assert(pixelArtifacts.size() == 2); // Driver order: colour, textured.
        PixelState state;
        for (unsigned frame = 0; frame < 120; ++frame) {
            for (unsigned shader : {0u, 1u, 0u}) {
                const auto& bytes = pixelArtifacts[shader];
                assert(ApplyPixelShaderArtifact(bytes.data(), bytes.size(), CaptureState, &state, reason));
                assert(state.Program == shader && state.Selectors == 0);
            }
        }
        std::puts("Serialized pixel artifacts: 120 colour/textured/colour replay sequences passed.");
        assert(vertexArtifacts.size() == 2);
        XboxShaderProgram pairs[2];
        for (unsigned i = 0; i < 2; ++i) {
            assert(pairs[i].Load(vertexArtifacts[i].data(), vertexArtifacts[i].size(),
                                 pixelArtifacts[i].data(), pixelArtifacts[i].size(), reason));
            // Source buffers may be released after loading; programs own their bytes.
            vertexArtifacts[i].clear();
            pixelArtifacts[i].clear();
        }
        BoundPairState bound;
        for (unsigned frame = 0; frame < 120; ++frame) {
            for (unsigned shader : {0u, 1u, 0u}) {
                assert(pairs[shader].Bind(CaptureInstruction, CapturePixel, &bound, reason));
                assert(bound.Instructions > 0 && bound.Pixel.Program == shader && bound.Pixel.Selectors == 0);
            }
        }
        std::puts("Owned real shader pairs: vertex upload and pixel bind transitions passed after source release.");
    }
    std::puts("Shader artifact bounds, stage, baseline, checksum and method validation passed.");
}
