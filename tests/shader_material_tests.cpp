#include "platform/xbox/XboxShaderMaterial.hpp"
#include "platform/xbox/XboxShaderTextureBinding.hpp"
#include <pbkit/nv_regs.h>
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace helengine::xbox;
static void Word(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
static void Finish(std::vector<uint8_t>& data, unsigned stage, unsigned count, unsigned bindings) {
    Word(data, 0, 0x31535848); Word(data, 4, 2); Word(data, 8, stage);
    Word(data, 12, count); Word(data, 16, 2); Word(data, 120, bindings);
    Word(data, 20, ShaderPayloadChecksum(data.data() + 128, data.size() - 128));
}
static void Interface(std::vector<uint8_t>& bytes, uint32_t kind, const char* semantic, uint32_t reg, uint32_t components) {
    const size_t offset = bytes.size(); bytes.resize(offset + 80, 0);
    Word(bytes, offset, kind); std::memcpy(bytes.data() + offset + 4, semantic, std::strlen(semantic));
    Word(bytes, offset + 68, reg); Word(bytes, offset + 72, components);
    Word(bytes, 4, 6); Word(bytes, 124, ReadShaderWord(bytes.data() + 124) + 1);
    Word(bytes, 20, ShaderPayloadChecksum(bytes.data() + 128, bytes.size() - 128));
}
struct DrawState { unsigned Writes = 0; float Tint[4] = {}; float Extent[2] = {}; std::map<uint32_t, uint32_t> Registers; };
static void Vertex(void* context, unsigned, const uint32_t*) { ++static_cast<DrawState*>(context)->Writes; }
static void Pixel(void* context, uint32_t method, uint32_t value) {
    auto& state = *static_cast<DrawState*>(context);
    ++state.Writes;
    state.Registers[method] = value;
}
static void Constants(void* context, const float (*constants)[4]) {
    auto& state = *static_cast<DrawState*>(context);
    ++state.Writes;
    std::memcpy(state.Tint, constants[9], sizeof(state.Tint));
    std::memcpy(state.Extent, constants[93], sizeof(state.Extent));
}
int main(int argc, char** argv) {
    std::vector<uint8_t> vertex(144 + 8 * 72, 0), pixel(200, 0);
    Word(vertex, 140, 1);
    const char* names[] = {"TransformBuffer", "TransformBuffer", "TransformBuffer", "TransformBuffer", "Tint", "gl_HalfPixel", "Xbox_ViewportScale", "Xbox_ViewportOffset"};
    const unsigned registers[] = {12, 20, 7, 40, 9, 2, 94, 95};
    for (unsigned i = 0; i < 8; ++i) {
        std::memcpy(vertex.data() + 144 + i * 72, names[i], std::strlen(names[i]));
        Word(vertex, 144 + i * 72 + 64, i < 4 ? i : 0);
        Word(vertex, 144 + i * 72 + 68, registers[i]);
    }
    const unsigned methods[] = {NV097_SET_SHADER_STAGE_PROGRAM, NV097_SET_SHADER_OTHER_STAGE_INPUT,
        NV097_SET_COMBINER_CONTROL, NV097_SET_COMBINER_COLOR_ICW, NV097_SET_COMBINER_COLOR_OCW,
        NV097_SET_COMBINER_ALPHA_ICW, NV097_SET_COMBINER_ALPHA_OCW,
        NV097_SET_COMBINER_SPECULAR_FOG_CW0, NV097_SET_COMBINER_SPECULAR_FOG_CW1};
    for (unsigned i = 0; i < 9; ++i) Word(pixel, 128 + i * 8, methods[i]);
    Word(pixel, 148, 1); Finish(vertex, 1, 4, 8); Finish(pixel, 2, 18, 0);
    auto program = std::make_shared<XboxShaderProgram>();
    const char* reason = nullptr;
    assert(program->Load(vertex.data(), vertex.size(), pixel.data(), pixel.size(), reason));
    XboxShaderMaterial first, second;
    assert(first.Initialize(program, "TransformBuffer"));
    assert(second.Initialize(program, "TransformBuffer"));
    auto uniformPixel = pixel;
    uniformPixel.resize(200 + 72, 0);
    std::memcpy(uniformPixel.data() + 200, "PixelTint", 9);
    Word(uniformPixel, 268, 1);
    Finish(uniformPixel, 2, 18, 1); Word(uniformPixel, 4, 3);
    auto uniformProgram = std::make_shared<XboxShaderProgram>();
    assert(uniformProgram->Load(vertex.data(), vertex.size(), uniformPixel.data(), uniformPixel.size(), reason));
    auto twoFactorPixel = uniformPixel;
    twoFactorPixel.resize(200 + 2 * 72, 0);
    std::memcpy(twoFactorPixel.data() + 272, "OtherTint", 9);
    Finish(twoFactorPixel, 2, 18, 2); Word(twoFactorPixel, 4, 3);
    auto twoFactorProgram = std::make_shared<XboxShaderProgram>();
    assert(twoFactorProgram->Load(vertex.data(), vertex.size(), twoFactorPixel.data(), twoFactorPixel.size(), reason));
    XboxShaderMaterial uniformMaterial;
    assert(uniformMaterial.Initialize(uniformProgram, "TransformBuffer"));
    auto texturedPixel = pixel;
    texturedPixel.resize(368, 0);
    Word(texturedPixel, 200, NV097_SET_SHADER_STAGE_PROGRAM); Word(texturedPixel, 204, 1);
    Word(texturedPixel, 208, 1); std::memcpy(texturedPixel.data() + 212, "Albedo", 6);
    Word(texturedPixel, 288, 2); std::memcpy(texturedPixel.data() + 292, "Sampler", 7);
    Finish(texturedPixel, 2, 20, 0); Word(texturedPixel, 4, 4); Word(texturedPixel, 124, 2);
    auto texturedProgram = std::make_shared<XboxShaderProgram>();
    assert(texturedProgram->Load(vertex.data(), vertex.size(), texturedPixel.data(), texturedPixel.size(), reason));
    XboxShaderMaterial texturedMaterial;
    assert(texturedMaterial.Initialize(texturedProgram, "TransformBuffer"));
    auto literalVertex = vertex;
    const size_t literalOffset = literalVertex.size();
    literalVertex.resize(literalOffset + 80, 0);
    Word(literalVertex, literalOffset, 3); Word(literalVertex, literalOffset + 68, 5);
    Word(literalVertex, literalOffset + 4, 0x3e800000); Word(literalVertex, literalOffset + 8, 0x3f000000);
    Word(literalVertex, literalOffset + 12, 0x3f400000); Word(literalVertex, literalOffset + 16, 0x3f600000);
    Finish(literalVertex, 1, 4, 8); Word(literalVertex, 4, 5); Word(literalVertex, 124, 1);
    auto literalProgram = std::make_shared<XboxShaderProgram>();
    assert(literalProgram->Load(literalVertex.data(), literalVertex.size(), pixel.data(), pixel.size(), reason));
    XboxShaderMaterial literalMaterial;
    assert(literalMaterial.Initialize(literalProgram, "TransformBuffer"));
    auto interfaceVertex = literalVertex;
    Interface(interfaceVertex, 5, "POSITION", 0, 4);
    Interface(interfaceVertex, 5, "COLOR0", 3, 4);
    auto interfacePixel = pixel;
    Interface(interfacePixel, 6, "COLOR0", 3, 4);
    Interface(interfacePixel, 7, "COLOR0", 0, 4);
    auto interfaceProgram = std::make_shared<XboxShaderProgram>();
    assert(interfaceProgram->Load(interfaceVertex.data(), interfaceVertex.size(), interfacePixel.data(), interfacePixel.size(), reason));
    XboxShaderMaterial interfaceMaterial;
    assert(interfaceMaterial.Initialize(interfaceProgram, "TransformBuffer"));
    float directConstants[96][4]{};
    directConstants[12][0] = 123;
    assert(literalProgram->ApplyVertexLiterals(directConstants, reason));
    assert(directConstants[5][0] == .25f && directConstants[5][3] == .875f && directConstants[12][0] == 123);
    program.reset(); vertex.clear(); pixel.clear();
    Matrix4 clip{{1,5,9,13, 2,6,10,14, 3,7,11,15, 4,8,12,16}};
    if (argc == 4 && std::strcmp(argv[1], "--literal") == 0) {
        std::ifstream vsFile(argv[2], std::ios::binary), psFile(argv[3], std::ios::binary);
        assert(vsFile && psFile);
        std::vector<uint8_t> vs((std::istreambuf_iterator<char>(vsFile)), {});
        std::vector<uint8_t> ps((std::istreambuf_iterator<char>(psFile)), {});
        auto actualProgram = std::make_shared<XboxShaderProgram>();
        assert(actualProgram->Load(vs.data(), vs.size(), ps.data(), ps.size(), reason));
        XboxShaderMaterial actual;
        assert(actual.Initialize(actualProgram, "Transform"));
        vs.clear(); ps.clear(); actualProgram.reset();
        float actualConstants[96][4]{};
        assert(actual.BuildDrawConstants(clip, 640, 480, 1, 1, actualConstants));
        assert(actualConstants[5][0] == .25f && actualConstants[5][1] == .5f &&
            actualConstants[5][2] == .75f && actualConstants[5][3] == .875f);
        std::puts("Actual Cg literal colour survives artifact ownership and native constant preparation.");
    }
    if (argc == 4 && std::strcmp(argv[1], "--texture") == 0) {
        std::ifstream vsFile(argv[2], std::ios::binary), psFile(argv[3], std::ios::binary);
        assert(vsFile && psFile);
        std::vector<uint8_t> vs((std::istreambuf_iterator<char>(vsFile)), {});
        std::vector<uint8_t> ps((std::istreambuf_iterator<char>(psFile)), {});
        auto actualProgram = std::make_shared<XboxShaderProgram>();
        assert(actualProgram->Load(vs.data(), vs.size(), ps.data(), ps.size(), reason));
        XboxShaderMaterial actual;
        assert(actual.Initialize(actualProgram, "Transform"));
        vs.clear(); ps.clear(); actualProgram.reset();
        DrawState actualDraw;
        assert(!BindShaderMaterialTexture(actual, clip, 640, 480, nullptr, 0,
            Vertex, Pixel, Constants, &actualDraw, reason));
        assert(actualDraw.Writes == 0);
        for (unsigned width : {32u, 64u, 128u}) {
            XboxTextureLayout layout{width,32,width,32,4,0x12,width * 128u};
            assert(BindShaderMaterialTexture(actual, clip, 640, 480, &layout, width * 4096,
                Vertex, Pixel, Constants, &actualDraw, reason));
            assert(actualDraw.Extent[0] == width && actualDraw.Extent[1] == 32);
            assert(actualDraw.Registers.at(NV097_SET_TEXTURE_CONTROL1) == (width * 4) << 16);
            assert(actualDraw.Registers.at(NV097_SET_TEXTURE_IMAGE_RECT) == (width << 16 | 32));
            assert(actualDraw.Registers.at(NV097_SET_SHADER_STAGE_PROGRAM) == 1);
            assert(actualDraw.Registers.at(NV097_SET_TEXTURE_CONTROL0) == 0x4003ffc0);
        }
        std::puts("Real Cg textured material binds changed texture extents and owned shader binaries.");
    }
    float output[96][4] = {};
    output[0][0] = 123;
    assert(!first.BuildDrawConstants(clip, 640, 480, 256, 128, output));
    if (argc == 3) {
        std::ifstream vsFile(argv[1], std::ios::binary), psFile(argv[2], std::ios::binary);
        assert(vsFile && psFile);
        std::vector<uint8_t> vs((std::istreambuf_iterator<char>(vsFile)), {});
        std::vector<uint8_t> ps((std::istreambuf_iterator<char>(psFile)), {});
        auto actualProgram = std::make_shared<XboxShaderProgram>();
        assert(actualProgram->Load(vs.data(), vs.size(), ps.data(), ps.size(), reason));
        XboxShaderMaterial actual;
        assert(actual.Initialize(actualProgram, "TransformBuffer"));
        DrawState draw;
        assert(!actual.Bind(clip, 640, 480, 1, 1, Vertex, Pixel, Constants, &draw, reason));
        assert(draw.Writes == 0);
        const float cyan[] = {.2f,.8f,1,1};
        assert(actual.SetPixelConstant("BaseColorBuffer", cyan, 4));
        assert(actual.Bind(clip, 640, 480, 1, 1, Vertex, Pixel, Constants, &draw, reason));
        assert(draw.Registers.at(NV097_SET_COMBINER_FACTOR0) == 0xff33ccff);
        assert(draw.Registers.at(NV097_SET_SPECULAR_FOG_FACTOR) == 0xff33ccff);
        std::puts("Actual HLSL/Cg uniform-colour pair bound material ARGB factors correctly.");
    }
    assert(output[0][0] == 123); // Missing Tint must not publish partial constants.
    float red[] = {1,0,0,1}, blue[] = {0,0,1,.5f};
    assert(uniformMaterial.SetVertexConstant("Tint", red, 4));
    DrawState missingPixel;
    assert(!uniformMaterial.Bind(clip, 640, 480, 256, 128, Vertex, Pixel, Constants, &missingPixel, reason));
    assert(missingPixel.Writes == 0);
    float pixelColour[] = {.2f,.8f,1,.5f};
    assert(uniformMaterial.SetPixelConstant("PixelTint", pixelColour, 4));
    pixelColour[0] = 1; // The material owns a copy, not the caller's buffer.
    assert(uniformMaterial.Bind(clip, 640, 480, 256, 128, Vertex, Pixel, Constants, &missingPixel, reason));
    assert(missingPixel.Writes == 13);
    assert(missingPixel.Registers.at(NV097_SET_COMBINER_FACTOR1) == 0x7f33ccff);
    assert(missingPixel.Registers.at(NV097_SET_SPECULAR_FOG_FACTOR + 4) == 0x7f33ccff);
    XboxShaderMaterial twoFactors;
    assert(twoFactors.Initialize(twoFactorProgram, "TransformBuffer"));
    assert(twoFactors.SetVertexConstant("Tint", red, 4));
    assert(twoFactors.SetPixelConstant("PixelTint", red, 4));
    DrawState both;
    assert(!twoFactors.Bind(clip, 640, 480, 1, 1, Vertex, Pixel, Constants, &both, reason));
    assert(both.Writes == 0); // One missing factor rejects the entire draw, not just its own write.
    assert(twoFactors.SetPixelConstant("OtherTint", blue, 4));
    assert(twoFactors.Bind(clip, 640, 480, 1, 1, Vertex, Pixel, Constants, &both, reason));
    assert(both.Writes == 15);
    assert(both.Registers.at(NV097_SET_COMBINER_FACTOR1) == 0xffff0000);
    assert(both.Registers.at(NV097_SET_SPECULAR_FOG_FACTOR + 4) == 0xffff0000);
    assert(both.Registers.at(NV097_SET_COMBINER_FACTOR0) == 0x7f0000ff);
    assert(both.Registers.at(NV097_SET_SPECULAR_FOG_FACTOR) == 0x7f0000ff);
    XboxShaderMaterial otherPixel;
    assert(otherPixel.Initialize(uniformProgram, "TransformBuffer"));
    assert(otherPixel.SetVertexConstant("Tint", red, 4));
    assert(otherPixel.SetPixelConstant("PixelTint", red, 4));
    for (unsigned i = 0; i < 120; ++i) {
        DrawState current;
        assert(otherPixel.Bind(clip, 640, 480, 256, 128, Vertex, Pixel, Constants, &current, reason));
        assert(current.Registers.at(NV097_SET_COMBINER_FACTOR1) == 0xffff0000);
        assert(uniformMaterial.Bind(clip, 640, 480, 256, 128, Vertex, Pixel, Constants, &current, reason));
        assert(current.Registers.at(NV097_SET_COMBINER_FACTOR1) == 0x7f33ccff);
    }
    for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        assert(otherPixel.Initialize(uniformProgram, "TransformBuffer"));
        pixelColour[0] = invalid;
        assert(!otherPixel.SetPixelConstant("PixelTint", pixelColour, 4));
        DrawState rejected;
        assert(!otherPixel.Bind(clip, 640, 480, 256, 128, Vertex, Pixel, Constants, &rejected, reason));
        assert(rejected.Writes == 0);
    }
    assert(first.SetVertexConstant("Tint", red, 4));
    assert(second.SetVertexConstant("Tint", blue, 4));
    assert(interfaceMaterial.SetVertexConstant("Tint", red, 4));
    DrawState interfaceDraw;
    assert(BindShaderMaterialTexture(interfaceMaterial, clip, 640, 480, nullptr, 0,
        Vertex, Pixel, Constants, &interfaceDraw, reason));
    assert(literalMaterial.SetVertexConstant("Tint", red, 4));
    literalVertex.clear(); literalProgram.reset();
    assert(literalMaterial.BuildDrawConstants(clip, 640, 480, 1, 1, output));
    assert(output[5][0] == .25f && output[5][1] == .5f && output[5][2] == .75f && output[5][3] == .875f);
    XboxTextureLayout textureLayout{64,32,64,32,4,0x12,8192};
    DrawState textureDraw;
    assert(!BindShaderMaterialTexture(texturedMaterial, clip, 640, 480, &textureLayout, 0x1000,
        Vertex, Pixel, Constants, &textureDraw, reason)); // Missing Tint rejects before GPU writes.
    assert(textureDraw.Writes == 0);
    assert(texturedMaterial.SetVertexConstant("Tint", red, 4));
    assert(!BindShaderMaterialTexture(texturedMaterial, clip, 640, 480, nullptr, 0,
        Vertex, Pixel, Constants, &textureDraw, reason));
    assert(reason != nullptr && std::strstr(reason, "Texture") != nullptr);
    assert(!BindShaderMaterialTexture(first, clip, 640, 480, &textureLayout, 0x1000,
        Vertex, Pixel, Constants, &textureDraw, reason));
    assert(!BindShaderMaterialTexture(texturedMaterial, clip, 640, 480, &textureLayout, 0,
        Vertex, Pixel, Constants, &textureDraw, reason));
    assert(textureDraw.Writes == 0);
    for (unsigned i = 0; i < 120; ++i) {
        assert(BindShaderMaterialTexture(texturedMaterial, clip, 640, 480, &textureLayout, 0x1000 + i * 4096,
            Vertex, Pixel, Constants, &textureDraw, reason));
        assert(textureDraw.Extent[0] == 64 && textureDraw.Extent[1] == 32);
        assert(textureDraw.Registers.at(NV097_SET_TEXTURE_OFFSET) == 0x1000 + i * 4096);
        assert(textureDraw.Registers.at(NV097_SET_SHADER_STAGE_PROGRAM) == 1);
        assert(textureDraw.Registers.at(NV097_SET_TEXTURE_CONTROL0) == 0x4003ffc0);
        assert(BindShaderMaterialTexture(first, clip, 640, 480, nullptr, 0,
            Vertex, Pixel, Constants, &textureDraw, reason));
        assert(textureDraw.Extent[0] == 1 && textureDraw.Extent[1] == 1);
        assert(textureDraw.Registers.at(NV097_SET_SHADER_STAGE_PROGRAM) == 0);
        assert(textureDraw.Registers.at(NV097_SET_TEXTURE_CONTROL0) == 0);
    }
    red[0] = 0; // Material copied the source; caller is free to mutate it.
    DrawState state;
    assert(first.Bind(clip, 640, 480, 256, 128, Vertex, Pixel, Constants, &state, reason));
    assert(state.Writes == 11 && state.Tint[0] == 1);
    state.Writes = 0;
    assert(!first.Bind(clip, 640, 480, 256, 128, Vertex, Pixel, nullptr, &state, reason));
    assert(state.Writes == 0);
    for (unsigned i = 0; i < 120; ++i) {
        assert(first.BuildDrawConstants(clip, 640, 480, 256, 128, output));
        assert(output[9][0] == 1 && output[9][2] == 0 && output[9][3] == 1);
        assert(output[12][0] == 1 && output[20][0] == 5 && output[7][0] == 9 && output[40][3] == 16);
        assert(output[2][0] == 0 && output[93][0] == 256 && output[94][0] == 320);
        assert(second.BuildDrawConstants(clip, 320, 240, 32, 64, output));
        assert(output[9][0] == 0 && output[9][2] == 1 && output[9][3] == .5f);
        assert(output[93][0] == 32 && output[93][1] == 64 && output[94][0] == 160);
    }
    blue[0] = std::numeric_limits<float>::infinity();
    assert(!second.SetVertexConstant("Tint", blue, 4));
    assert(!second.BuildDrawConstants(clip, 320, 240, 32, 64, output));
    assert(!second.Bind(clip, 320, 240, 32, 64, Vertex, Pixel, Constants, &state, reason));
    assert(state.Writes == 0);
    assert(first.BuildDrawConstants(clip, 640, 480, 256, 128, output));
    assert(!first.SetVertexConstant("Xbox_ViewportScale", red, 4));
    assert(!first.BuildDrawConstants(clip, 640, 480, 256, 128, output));
    // A renderer invalidation must reach every material borrowing the same program.
    assert(otherPixel.Initialize(uniformProgram, "TransformBuffer"));
    assert(otherPixel.SetVertexConstant("Tint", red, 4));
    assert(otherPixel.SetPixelConstant("PixelTint", red, 4));
    DrawState beforeInvalidation;
    assert(otherPixel.Bind(clip, 640, 480, 1, 1, Vertex, Pixel, Constants, &beforeInvalidation, reason));
    assert(!uniformProgram->Load(nullptr, 0, nullptr, 0, reason));
    DrawState invalidated;
    assert(!uniformMaterial.Bind(clip, 640, 480, 1, 1, Vertex, Pixel, Constants, &invalidated, reason));
    assert(!otherPixel.Bind(clip, 640, 480, 1, 1, Vertex, Pixel, Constants, &invalidated, reason));
    assert(invalidated.Writes == 0);
    assert(texturedMaterial.Bind(clip, 640, 480, 64, 32, Vertex, Pixel, Constants, &invalidated, reason));
    assert(invalidated.Writes > 0); // Unrelated shader ownership is unaffected.
    std::puts("Shared shader materials: physical row mapping, private constants, lifetime, invalidation and fail-closed draws passed.");
}
