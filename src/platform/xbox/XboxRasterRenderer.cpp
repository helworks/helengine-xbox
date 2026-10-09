#include "XboxRasterRenderer.hpp"

#include "XboxDiagnostics.hpp"
#include "XboxRuntimeTexture.hpp"
#include "XboxShaderProgram.hpp"
#include "XboxShaderConstants.hpp"
#include "XboxShaderMaterial.hpp"
#include "XboxShaderTextureBinding.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <pbkit/pbkit.h>
#include <xboxkrnl/xboxkrnl.h>

#ifndef HELENGINE_XBOX_POSE
#define HELENGINE_XBOX_POSE 0
#endif
#ifndef HELENGINE_XBOX_DISABLE_CULLING
#define HELENGINE_XBOX_DISABLE_CULLING 0
#endif
#ifndef HELENGINE_XBOX_REVERSE_TRIANGLES
#define HELENGINE_XBOX_REVERSE_TRIANGLES 0
#endif

namespace helengine::xbox {
namespace {

// Vertex layout shared by the cube and the triangle probe. Float colour avoids
// packed-colour byte-order ambiguity in this first probe.
static_assert(sizeof(CubeVertex) == 48, "CubeVertex must be 48 bytes (3 + 4 + 3 + 2 floats)");
static_assert(offsetof(CubeVertex, TexCoord) == 40, "Normalized UV must follow the normal");
static_assert(offsetof(CubeVertex, Position) == 0, "Position must be at offset 0");
static_assert(offsetof(CubeVertex, Color) == 12, "Color must be at offset 12");
static_assert(sizeof(float) == 4, "float must be 32-bit");

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegreesToRadians = kPi / 180.0f;

// Pinned nxdk samples use this highest acceptable physical address for
// GPU-visible allocations (MmAllocateContiguousMemoryEx upper bound).
constexpr ULONG_PTR kGpuHighestAddress = 0x03FFAFFF;
constexpr uint32_t kGpuAddressMask = 0x03FFFFFF;

// vp20compiler maps program c[N] to hardware constant N + 96 (see the mesh and
// triangle samples, which set NV097_SET_TRANSFORM_CONSTANT_LOAD to 96).
constexpr uint32_t kConstantBase = 96;
constexpr unsigned kConstantVectorCount = 6; // c[0..3] clip rows, c[4] scale, c[5] offset

// Attribute slots consumed by shaders/unlit.vsh.
constexpr unsigned kPositionAttribute = 0;
constexpr unsigned kColorAttribute = 3;
constexpr unsigned kAttributeCount = 16;
constexpr unsigned kPosition2DAttribute = 0;
constexpr unsigned kTintAttribute = 3;
constexpr unsigned kTextureCoordinateAttribute = 9;
#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
constexpr unsigned kMaxSpriteQuadsPerFrame = 512;
constexpr unsigned kSpriteVertexCount = kMaxSpriteQuadsPerFrame * 4;
constexpr std::uint32_t kNvTextureStage0LinearFormat = 0x00011222;
constexpr std::uint32_t kNvTextureAddressClamp = 0x00030303;
constexpr std::uint32_t kNvTextureFilterFromPinnedMeshSample = 0x04074000;
constexpr std::uint32_t kNvTextureControlEnabled = 0x4003ffc0;
#endif

// 24-bit depth range used by pbkit for Z24S8 (pb_ZScale = 0xFFFFFF).
constexpr float kDepthScale = 16777215.0f;
// pbkit's viewport offset bias (pb_set_viewport adds 0.53125 to x and y).
constexpr float kViewportBias = 0.53125f;

// NV2A "no auto-increment" method flag: every following dword targets the
// same register (used for index streams).
constexpr uint32_t kNonIncrementingMethod = 0x40000000;
// Keep each begin/end block well under pbkit's 128-dword assertion limit.
constexpr unsigned kMaxIndexDwordsPerBlock = 96;

constexpr uint32_t ShiftForMask(uint32_t mask) {
    uint32_t shift = 0;
    while (shift < 32 && ((mask >> shift) & 1u) == 0) {
        ++shift;
    }
    return shift;
}

constexpr uint32_t MaskValue(uint32_t mask, uint32_t value) {
    return (value << ShiftForMask(mask)) & mask;
}

const uint32_t kVertexProgram[] = {
#include "shaders/unlit_vsh.inl"
};
static_assert(sizeof(kVertexProgram) % 16 == 0, "vertex program must be whole 16-byte instructions");

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
const uint32_t kSpriteVertexProgram[] = {
#include "shaders/sprite_vsh.inl"
};
static_assert(sizeof(kSpriteVertexProgram) % 16 == 0, "sprite vertex program must be whole instructions");
static_assert(sizeof(XboxSpriteVertex) == 32, "sprite vertex must be eight floats");
static_assert(offsetof(XboxSpriteVertex, X) == 0, "sprite position offset");
static_assert(offsetof(XboxSpriteVertex, U) == 8, "sprite texture-coordinate offset");
static_assert(offsetof(XboxSpriteVertex, R) == 16, "sprite tint offset");

const unsigned short kSpriteIndices[6] = {0, 1, 2, 0, 2, 3};
#endif

// Single triangle for the Triangle probe: counter-clockwise from the camera,
// red/green/blue corners, drawn through the same indexed path as the cube.
const CubeVertex kTriangleVertices[3] = {
    {{-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
};

const char* PoseName(int pose) {
    switch (pose) {
    case 0: return "Baseline";
    case 1: return "Far";
    case 2: return "NearPlane";
    case 3: return "ScreenEdge";
    }
    return "Unknown";
}

// Called only after XboxShaderProgram validates both owned artifact buffers.
void PushCompiledInstruction(void*, unsigned index, const uint32_t* instruction) {
    uint32_t* p = pb_begin();
    if (index == 0) {
        p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);
        p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
            MaskValue(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM) |
            MaskValue(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));
        p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);
        p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    }
    pb_push(p++, NV097_SET_TRANSFORM_PROGRAM, 4);
    std::memcpy(p, instruction, 16);
    p += 4;
    pb_end(p);
}

void PushCompiledRegister(void*, uint32_t method, uint32_t value) {
    uint32_t* p = pb_begin();
    p = pb_push1(p, method, value);
    pb_end(p);
}

#include "XboxShaderConstantUpload.inl"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
void PushBatchConstants(void* context, const float (*constants)[4]) {
    const unsigned* origin = static_cast<const unsigned*>(context);
    float adjusted[96][4];
    std::memcpy(adjusted, constants, sizeof(adjusted));
    adjusted[XboxViewportOffsetConstant][0] += origin[0];
    adjusted[XboxViewportOffsetConstant][1] += origin[1];
    PushCompiledConstants(nullptr, adjusted);
}
#endif

} // namespace

bool XboxRasterRenderer::BindCompiledMaterial(const XboxShaderMaterial& material, const Matrix4& clip,
                                             unsigned textureWidth, unsigned textureHeight) {
    if (!initialized_) return false;
    const char* reason = nullptr;
    if (!material.Bind(clip, static_cast<unsigned>(viewportScale_[0] * 2), static_cast<unsigned>(-viewportScale_[1] * 2),
            textureWidth, textureHeight, PushCompiledInstruction, PushCompiledRegister, PushCompiledConstants, nullptr, reason)) {
        Diagnostics::Log("compiled material rejected: %s", reason);
        return false;
    }
    return true;
}

bool XboxRasterRenderer::BindCompiledShader(const XboxShaderProgram& program, const Matrix4& clip, int matrixRegister,
                                           unsigned textureWidth, unsigned textureHeight) {
    if (!initialized_) return false;
    float constants[XboxShaderConstantCount][4];
    if (!BuildShaderDrawConstants(clip, matrixRegister, static_cast<unsigned>(viewportScale_[0] * 2),
            static_cast<unsigned>(-viewportScale_[1] * 2), textureWidth, textureHeight, constants)) return false;
    const char* reason = nullptr;
    if (!program.ApplyVertexLiterals(constants, reason)) return false;
    if (!program.Bind(PushCompiledInstruction, PushCompiledRegister, nullptr, reason)) {
        Diagnostics::Log("compiled shader rejected: %s", reason);
        return false;
    }
    PushCompiledConstants(nullptr, constants);
    return true;
}

bool XboxRasterRenderer::Initialize(unsigned width, unsigned height) {
    if (initialized_) {
        Diagnostics::Log("renderer error: already initialized");
        return false;
    }
    if (width == 0 || height == 0) {
        Diagnostics::Log("renderer error: invalid extent %ux%u", width, height);
        return false;
    }
    if (!BuildTransforms(width, height)) {
        return false;
    }

    cube_ = MakeCubeGeometry();
    for (unsigned i = 0; i < CubeGeometry::IndexCount; ++i) {
#if HELENGINE_XBOX_REVERSE_TRIANGLES
        // Reverse the triangle submission order (not the winding) to show that
        // the depth test, not draw order, decides visibility.
        const unsigned triangle = (CubeGeometry::IndexCount / 3) - 1 - (i / 3);
        cubeDrawOrder_[i] = cube_.Indices[triangle * 3 + (i % 3)];
#else
        cubeDrawOrder_[i] = cube_.Indices[i];
#endif
    }
    triangleIndices_[0] = 0;
    triangleIndices_[1] = 1;
    triangleIndices_[2] = 2;

    // One GPU-visible allocation for the whole probe: the cube first, then the
    // triangle. Write-combined memory is written sequentially and never read.
    cubeFirstVertex_ = 0;
    triangleFirstVertex_ = CubeGeometry::VertexCount;
    gpuVertexCount_ = CubeGeometry::VertexCount + 3;
    const SIZE_T bytes = static_cast<SIZE_T>(gpuVertexCount_) * sizeof(CubeVertex);
    gpuVertices_ = static_cast<CubeVertex*>(
        MmAllocateContiguousMemoryEx(bytes, 0, kGpuHighestAddress, 0, PAGE_READWRITE | PAGE_WRITECOMBINE));
    if (gpuVertices_ == nullptr) {
        Diagnostics::Log("renderer error: MmAllocateContiguousMemoryEx(%lu) failed", static_cast<unsigned long>(bytes));
        return false;
    }
    ++counters_.Allocations;
    std::memcpy(gpuVertices_ + cubeFirstVertex_, cube_.Vertices, sizeof(cube_.Vertices));
    std::memcpy(gpuVertices_ + triangleFirstVertex_, kTriangleVertices, sizeof(kTriangleVertices));

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
    // The shader path copies the shared six-float4 vertex; legacy sprites use a prefix of this allocation.
    const SIZE_T spriteBytes = static_cast<SIZE_T>(kSpriteVertexCount) * 96;
    gpuSpriteVertices_ = static_cast<XboxSpriteVertex*>(MmAllocateContiguousMemoryEx(
        spriteBytes, 0, kGpuHighestAddress, 0, PAGE_READWRITE | PAGE_WRITECOMBINE));
    if (gpuSpriteVertices_ == nullptr) {
        Diagnostics::Log("renderer error: sprite vertex allocation (%lu bytes) failed",
                         static_cast<unsigned long>(spriteBytes));
        MmFreeContiguousMemory(gpuVertices_);
        gpuVertices_ = nullptr;
        ++counters_.Releases;
        return false;
    }
    ++counters_.Allocations;
#endif

    if (!UploadPrograms()) {
        if (gpuSpriteVertices_ != nullptr) {
            MmFreeContiguousMemory(gpuSpriteVertices_);
            gpuSpriteVertices_ = nullptr;
            ++counters_.Releases;
        }
        MmFreeContiguousMemory(gpuVertices_);
        ++counters_.Releases;
        gpuVertices_ = nullptr;
        return false;
    }

    initialized_ = true;
    Diagnostics::Log("renderer-ready: %u verts (%lu B) at 0x%08lx, pose %s, cull %s, order %s", gpuVertexCount_,
                     static_cast<unsigned long>(bytes),
                     static_cast<unsigned long>(reinterpret_cast<uintptr_t>(gpuVertices_) & kGpuAddressMask),
                     PoseName(HELENGINE_XBOX_POSE), HELENGINE_XBOX_DISABLE_CULLING ? "off" : "back",
                     HELENGINE_XBOX_REVERSE_TRIANGLES ? "reversed" : "forward");
    return true;
}

bool XboxRasterRenderer::BuildTransforms(unsigned width, unsigned height) {
    // Explicit native test camera: 60 degree vertical FOV, aspect from the
    // actual back buffer, near 0.1, far 100. Column-vector convention with
    // clip = Projection * View * Model * position (see XboxMath.hpp).
    Matrix4 projection{};
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (!TryPerspective(60.0f * kDegreesToRadians, aspect, 0.1f, 100.0f, projection)) {
        Diagnostics::Log("renderer error: invalid perspective configuration (aspect %u:%u)", width, height);
        return false;
    }

    const Matrix4 model = Multiply(RotationY(30.0f * kDegreesToRadians), RotationX(-20.0f * kDegreesToRadians));
    Matrix4 view;
    switch (HELENGINE_XBOX_POSE) {
    case 1: view = Translation(0.0f, 0.0f, -12.0f); break;      // farther: cube must look smaller
    case 2: view = Translation(0.0f, 0.0f, -1.3f); break;       // cube pierces the near plane (z = -0.1)
    case 3: view = Translation(2.2f, 1.2f, -4.0f); break;       // cube crosses the right/top screen edges
    case 0:
    default: view = Translation(0.0f, 0.0f, -5.0f); break;      // baseline
    }
    cubeClip_ = Multiply(projection, Multiply(view, model));
    triangleClip_ = Multiply(projection, Translation(0.0f, 0.0f, -4.0f));

    // NV2A boundary conversion (see shaders/unlit.vsh): the program divides
    // by w and maps NDC to pbkit's viewport: x' = x*(w/2) + (bias + w/2),
    // y' = y*(-h/2) + (bias + h/2), z' = z*zscale. These equal the values
    // pb_set_viewport() programmed at pb_init; the GPU does not apply them a
    // second time to vertex-program output.
    viewportScale_[0] = static_cast<float>(width) * 0.5f;
    viewportScale_[1] = -static_cast<float>(height) * 0.5f;
    viewportScale_[2] = kDepthScale;
    viewportScale_[3] = 0.0f;
    viewportOffset_[0] = kViewportBias + static_cast<float>(width) * 0.5f;
    viewportOffset_[1] = kViewportBias + static_cast<float>(height) * 0.5f;
    viewportOffset_[2] = 0.0f;
    viewportOffset_[3] = 0.0f;
    return true;
}

bool XboxRasterRenderer::UploadPrograms() {
    const unsigned instructionCount = static_cast<unsigned>(sizeof(kVertexProgram) / 16);
    if (!UploadVertexProgram(kVertexProgram, instructionCount)) return false;
    PushUnlitCombiner();
    Diagnostics::Log("renderer: uploaded %u vertex program instructions and unlit combiner", instructionCount);
    return true;
}

bool XboxRasterRenderer::UploadVertexProgram(const uint32_t* program, unsigned instructionCount) {
    if (program == nullptr || instructionCount == 0) return false;
    uint32_t* p = pb_begin();
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);
    p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
                 MaskValue(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM) |
                     MaskValue(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE,
                               NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    pb_end(p);

    // Instructions are 16 bytes; upload one per block exactly like the samples.
    for (unsigned i = 0; i < instructionCount; ++i) {
        p = pb_begin();
        pb_push(p++, NV097_SET_TRANSFORM_PROGRAM, 4);
        std::memcpy(p, &program[i * 4], 16);
        p += 4;
        pb_end(p);
    }

    return true;
}

void XboxRasterRenderer::PushUnlitCombiner() {
    uint32_t* p = pb_begin();
#include "shaders/unlit_psh.inl"
    pb_end(p);
}

void XboxRasterRenderer::PushSpriteCombiner() {
    uint32_t* p = pb_begin();
#include "shaders/sprite_psh.inl"
    pb_end(p);
}

void XboxRasterRenderer::PushRenderState() {
    uint32_t* p = pb_begin();
    p = pb_push1(p, NV097_SET_WINDOW_CLIP_TYPE, 0);
    for (unsigned clip = 0; clip < 8; ++clip) {
        p = pb_push1(p, NV097_SET_WINDOW_CLIP_HORIZONTAL + clip * 4, (Width() - 1) << 16);
        p = pb_push1(p, NV097_SET_WINDOW_CLIP_VERTICAL + clip * 4, (Height() - 1) << 16);
    }
    pb_end(p);
    p = pb_begin();
    p = pb_push1(p, NV097_SET_LIGHTING_ENABLE, 0);
    p = pb_push1(p, NV097_SET_SPECULAR_ENABLE, 0);
    p = pb_push1(p, NV097_SET_FOG_ENABLE, 0);
    p = pb_push1(p, NV097_SET_BLEND_ENABLE, 0);
    p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR, NV097_SET_BLEND_FUNC_SFACTOR_V_ONE);
    p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR, NV097_SET_BLEND_FUNC_DFACTOR_V_ZERO);
    p = pb_push1(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
    p = pb_push1(p, NV097_SET_ALPHA_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_COLOR_MASK, 0x01010101); // write A, R, G, B
    // pb_init leaves NV097_SET_CONTROL0 at STENCIL_WRITE | Z_PERSPECTIVE |
    // TEXTURE_PERSPECTIVE. Z_PERSPECTIVE makes the GPU derive depth from w
    // rather than from the screen-space z this vertex program computes; for
    // this scene w only spans roughly 3..7, so almost every comparison ties
    // and LEQUAL admits whichever fragment is drawn last. Clear that bit and
    // keep the other two, so the depth test uses our [0, 0xFFFFFF] z.
    p = pb_push1(p, NV097_SET_CONTROL0,
                 NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE | NV097_SET_CONTROL0_TEXTURE_PERSPECTIVE_ENABLE);
    p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, 1);
    p = pb_push1(p, NV097_SET_DEPTH_MASK, 1);
    // Depth cleared to the maximum (0xFFFFFF) each frame; nearer fragments win.
    p = pb_push1(p, NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LEQUAL);
    // Front faces are counter-clockwise as seen on screen (the cube is wound
    // counter-clockwise from outside); back faces are culled.
    p = pb_push1(p, NV097_SET_FRONT_FACE, NV097_SET_FRONT_FACE_V_CCW);
    p = pb_push1(p, NV097_SET_CULL_FACE, NV097_SET_CULL_FACE_V_BACK);
    p = pb_push1(p, NV097_SET_CULL_FACE_ENABLE, HELENGINE_XBOX_DISABLE_CULLING ? 0 : 1);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        const uint32_t base = stage * 64;
        p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0 + base, 0); // texture stage disabled
        p = pb_push1(p, NV097_SET_TEXTURE_OFFSET + base, 0);
        p = pb_push1(p, NV097_SET_TEXTURE_FORMAT + base, 0);
        p = pb_push1(p, NV097_SET_TEXTURE_CONTROL1 + base, 0);
        p = pb_push1(p, NV097_SET_TEXTURE_ADDRESS + base, 0);
        p = pb_push1(p, NV097_SET_TEXTURE_FILTER + base, 0);
        p = pb_push1(p, NV097_SET_TEXTURE_IMAGE_RECT + base, 0);
    }
    pb_end(p);
}

void XboxRasterRenderer::PushSpriteVertexAttributes(const XboxSpriteVertex* vertices) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(vertices);
    const uint32_t stride = static_cast<uint32_t>(sizeof(XboxSpriteVertex));
    const uint32_t positionAddress = static_cast<uint32_t>(base + offsetof(XboxSpriteVertex, X)) & kGpuAddressMask;
    const uint32_t tintAddress = static_cast<uint32_t>(base + offsetof(XboxSpriteVertex, R)) & kGpuAddressMask;
    const uint32_t texcoordAddress = static_cast<uint32_t>(base + offsetof(XboxSpriteVertex, U)) & kGpuAddressMask;

    uint32_t* p = pb_begin();
    pb_push(p++, NV097_SET_VERTEX_DATA_ARRAY_FORMAT, kAttributeCount);
    for (unsigned i = 0; i < kAttributeCount; ++i) *(p++) = NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F;
    const uint32_t type = MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE,
                                    NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F);
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + kPosition2DAttribute * 4,
                 type | MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, 2) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + kPosition2DAttribute * 4, positionAddress);
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + kTintAttribute * 4,
                 type | MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, 4) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + kTintAttribute * 4, tintAddress);
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + kTextureCoordinateAttribute * 4,
                 type | MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, 2) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + kTextureCoordinateAttribute * 4, texcoordAddress);
    pb_end(p);
}

bool XboxRasterRenderer::BeginSpritePass() {
#if !defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
    return false;
#else
    if (!initialized_ || gpuSpriteVertices_ == nullptr) {
        Diagnostics::Log("renderer error: sprite pass unavailable");
        return false;
    }
    spriteCount_ = 0;
    spritePassActive_ = true;
    if (!UploadVertexProgram(kSpriteVertexProgram, static_cast<unsigned>(sizeof(kSpriteVertexProgram) / 16))) {
        spritePassActive_ = false;
        return false;
    }
    const float spriteConstants[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    uint32_t* p = pb_begin();
    p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT_LOAD, kConstantBase);
    pb_push(p++, NV097_SET_TRANSFORM_CONSTANT, 4);
    std::memcpy(p, spriteConstants, sizeof(spriteConstants));
    p += 4;
    p = pb_push1(p, NV097_SET_LIGHTING_ENABLE, 0);
    p = pb_push1(p, NV097_SET_SPECULAR_ENABLE, 0);
    p = pb_push1(p, NV097_SET_FOG_ENABLE, 0);
    p = pb_push1(p, NV097_SET_BLEND_ENABLE, 1);
    p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR, NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA);
    p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR, NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA);
    p = pb_push1(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
    p = pb_push1(p, NV097_SET_ALPHA_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_COLOR_MASK, 0x01010101);
    p = pb_push1(p, NV097_SET_CONTROL0,
                 NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE | NV097_SET_CONTROL0_TEXTURE_PERSPECTIVE_ENABLE);
    p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, 0);
    p = pb_push1(p, NV097_SET_DEPTH_MASK, 0);
    p = pb_push1(p, NV097_SET_CULL_FACE_ENABLE, 0);
    p = pb_push1(p, NV097_SET_TEXTURE_OFFSET, 0);
    p = pb_push1(p, NV097_SET_TEXTURE_FORMAT, kNvTextureStage0LinearFormat);
    p = pb_push1(p, NV097_SET_TEXTURE_CONTROL1, 0);
    p = pb_push1(p, NV097_SET_TEXTURE_ADDRESS, kNvTextureAddressClamp);
    p = pb_push1(p, NV097_SET_TEXTURE_FILTER, kNvTextureFilterFromPinnedMeshSample);
    p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0, kNvTextureControlEnabled);
    for (uint32_t stage = 1; stage < 4; ++stage)
        p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0 + stage * 64, 0);
    pb_end(p);
    PushSpriteCombiner();
    return true;
#endif
}

bool XboxRasterRenderer::DrawCompiledBatch(const XboxBatch2DSubmission& batch, const XboxShaderMaterial& material,
    const Matrix4& projection, const XboxRuntimeTexture& texture,
    unsigned viewportX, unsigned viewportY, unsigned viewportWidth, unsigned viewportHeight) {
#if !defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
    (void)batch; (void)material; (void)projection; (void)texture;
    (void)viewportX; (void)viewportY; (void)viewportWidth; (void)viewportHeight;
    return false;
#else
    if (!initialized_ || !spritePassActive_ || !gpuSpriteVertices_ || texture.IsDisposedNative() ||
        spriteCount_ > kMaxSpriteQuadsPerFrame || viewportWidth == 0 || viewportHeight == 0 ||
        uint64_t(viewportX) + viewportWidth > Width() || uint64_t(viewportY) + viewportHeight > Height() ||
        !ValidateBatch2DSubmission(batch, Width(), Height(), (kMaxSpriteQuadsPerFrame - spriteCount_) * 4)) return false;
    unsigned origin[] = {viewportX, viewportY};
    const char* reason = nullptr;
    if (!BindShaderMaterialTexture(material, projection, viewportWidth, viewportHeight, &texture.Layout(), texture.GpuOffset(),
            PushCompiledInstruction, PushCompiledRegister, PushBatchConstants, origin, reason, texture.PaletteGpuOffset())) {
        Diagnostics::Log("batch shader rejected: %s", reason);
        return false;
    }
    auto* vertices = reinterpret_cast<uint8_t*>(gpuSpriteVertices_) + spriteCount_ * 4 * 96;
    std::memcpy(vertices, batch.Vertices, batch.VertexCount * 96);
    uint32_t* p = pb_begin();
    p = pb_push1(p, NV097_SET_WINDOW_CLIP_TYPE, 0);
    const uint32_t horizontal = uint32_t(batch.ScissorX) | (uint32_t(batch.ScissorX + batch.ScissorWidth - 1) << 16);
    const uint32_t vertical = uint32_t(batch.ScissorY) | (uint32_t(batch.ScissorY + batch.ScissorHeight - 1) << 16);
    for (unsigned clip = 0; clip < 8; ++clip) {
        p = pb_push1(p, NV097_SET_WINDOW_CLIP_HORIZONTAL + clip * 4, horizontal);
        p = pb_push1(p, NV097_SET_WINDOW_CLIP_VERTICAL + clip * 4, vertical);
    }
    pb_push(p++, NV097_SET_VERTEX_DATA_ARRAY_FORMAT, kAttributeCount);
    for (unsigned attribute = 0; attribute < kAttributeCount; ++attribute) *(p++) = NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F;
    const unsigned attributes[] = {0, 9, 3};
    const unsigned offsets[] = {0, 16, 32};
    for (unsigned attribute = 0; attribute < 3; ++attribute) {
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + attributes[attribute] * 4,
            MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F) |
            MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, 4) |
            MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, 96));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + attributes[attribute] * 4,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vertices + offsets[attribute])) & kGpuAddressMask);
    }
    pb_end(p);
    PushIndexedTriangles(batch.Indices, batch.IndexCount);
    spriteCount_ += batch.VertexCount / 4;
    return true;
#endif
}

bool XboxRasterRenderer::DrawSprite(const XboxSpriteQuad& quad, const XboxRuntimeTexture& texture) {
#if !defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
    (void)quad;
    (void)texture;
    return false;
#else
    if (!initialized_ || !spritePassActive_ || gpuSpriteVertices_ == nullptr) {
        Diagnostics::Log("renderer error: DrawSprite outside sprite pass");
        return false;
    }
    const XboxTextureLayout& layout = texture.Layout();
    XboxTextureStageState stage;
    if (texture.IsDisposedNative() || !BuildTextureStageState(&layout, texture.GpuOffset(), stage, texture.PaletteGpuOffset())) {
        return false;
    }
    if (spriteCount_ >= kMaxSpriteQuadsPerFrame) {
        if (!spriteCapacityReported_) {
            spriteCapacityReported_ = true;
            Diagnostics::Log("renderer warning: sprite frame capacity (%u) reached", kMaxSpriteQuadsPerFrame);
        }
        return false;
    }
    XboxSpriteVertex* vertices = gpuSpriteVertices_ + spriteCount_ * 4;
    std::memcpy(vertices, quad.Vertices, sizeof(quad.Vertices));
    // Sprite geometry supplies real texel coordinates. Native POT layouts sample normalized storage coordinates.
    const float uMultiplier = stage.UvScaleX / static_cast<float>(layout.RealWidth);
    const float vMultiplier = stage.UvScaleY / static_cast<float>(layout.RealHeight);
    for (unsigned vertex=0;vertex<4;++vertex) {
        vertices[vertex].U *= uMultiplier;
        vertices[vertex].V *= vMultiplier;
    }
    if (!BindTextureStage(&layout,texture.GpuOffset(),PushCompiledRegister,nullptr,texture.PaletteGpuOffset())) return false;
    PushSpriteVertexAttributes(vertices);
    PushIndexedTriangles(kSpriteIndices, 6);
    ++spriteCount_;
    return true;
#endif
}

void XboxRasterRenderer::PushConstants(const Matrix4& clip) {
    float constants[kConstantVectorCount * 4];
    std::memcpy(&constants[0], clip.Values, sizeof(clip.Values)); // c[0..3] = rows
    std::memcpy(&constants[16], viewportScale_, sizeof(viewportScale_));
    std::memcpy(&constants[20], viewportOffset_, sizeof(viewportOffset_));

    uint32_t* p = pb_begin();
    p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT_LOAD, kConstantBase);
    pb_push(p++, NV097_SET_TRANSFORM_CONSTANT, kConstantVectorCount * 4);
    std::memcpy(p, constants, sizeof(constants));
    p += kConstantVectorCount * 4;
    pb_end(p);
}

void XboxRasterRenderer::PushVertexAttributes(const CubeVertex* vertices) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(vertices);
    const uint32_t positionAddress = static_cast<uint32_t>(base + offsetof(CubeVertex, Position)) & kGpuAddressMask;
    const uint32_t colorAddress = static_cast<uint32_t>(base + offsetof(CubeVertex, Color)) & kGpuAddressMask;
    const uint32_t textureAddress = static_cast<uint32_t>(base + offsetof(CubeVertex, TexCoord)) & kGpuAddressMask;
    const uint32_t stride = static_cast<uint32_t>(sizeof(CubeVertex));

    uint32_t* p = pb_begin();
    // Disable every attribute first (float type, size 0, stride 0).
    pb_push(p++, NV097_SET_VERTEX_DATA_ARRAY_FORMAT, kAttributeCount);
    for (unsigned i = 0; i < kAttributeCount; ++i) {
        *(p++) = NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F;
    }
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + kPositionAttribute * 4,
                 MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, 3) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + kPositionAttribute * 4, positionAddress);
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + kColorAttribute * 4,
                 MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, 4) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + kColorAttribute * 4, colorAddress);
    // Pinned vp20compiler maps NV_vertex_program v[8] TEX0 to NV2A attribute 9.
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + kTextureCoordinateAttribute * 4,
                 MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, 2) |
                     MaskValue(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + kTextureCoordinateAttribute * 4, textureAddress);
    pb_end(p);
}

void XboxRasterRenderer::PushIndexedTriangles(const unsigned short* indices, unsigned indexCount) {
    // Indices live in ordinary CPU memory and are copied into the push buffer
    // two per dword through NV097_ARRAY_ELEMENT16; an odd trailing index goes
    // through NV097_ARRAY_ELEMENT32 (pinned mesh sample: draw_indices).
    unsigned emitted = 0;
    while (emitted < indexCount) {
        const unsigned remaining = indexCount - emitted;
        const unsigned pairs = (remaining / 2) < kMaxIndexDwordsPerBlock ? (remaining / 2) : kMaxIndexDwordsPerBlock;
        const bool tail = (pairs * 2 == remaining - 1);
        uint32_t* p = pb_begin();
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
        if (pairs > 0) {
            pb_push(p++, kNonIncrementingMethod | NV097_ARRAY_ELEMENT16, pairs);
            for (unsigned i = 0; i < pairs; ++i) {
                const uint32_t low = indices[emitted + i * 2];
                const uint32_t high = indices[emitted + i * 2 + 1];
                *(p++) = low | (high << 16);
            }
        }
        if (tail) {
            p = pb_push1(p, NV097_ARRAY_ELEMENT32, indices[emitted + pairs * 2]);
        }
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
        pb_end(p);
        emitted += pairs * 2 + (tail ? 1 : 0);
    }
    ++counters_.DrawCalls;
    counters_.Triangles += indexCount / 3;
}

bool XboxRasterRenderer::Draw(ProbeMode mode) {
    if (!initialized_ || spritePassActive_) {
        Diagnostics::Log("renderer error: Draw before Initialize");
        return false;
    }
    PushRenderState();
    switch (mode) {
    case ProbeMode::Triangle:
        PushConstants(triangleClip_);
        PushVertexAttributes(gpuVertices_ + triangleFirstVertex_);
        PushIndexedTriangles(triangleIndices_, 3);
        return true;
    case ProbeMode::Cube:
        PushConstants(cubeClip_);
        PushVertexAttributes(gpuVertices_ + cubeFirstVertex_);
        PushIndexedTriangles(cubeDrawOrder_, CubeGeometry::IndexCount);
        return true;
    case ProbeMode::Clear:
    case ProbeMode::AbiChecks:
        return true; // nothing to draw; video owns the clear and the overlay carries the results
    }
    Diagnostics::Log("renderer error: unsupported probe mode %d", static_cast<int>(mode));
    return false;
}

bool XboxRasterRenderer::BeginAuthoredFrame() {
    if (!initialized_) {
        Diagnostics::Log("renderer error: BeginAuthoredFrame before Initialize");
        return false;
    }
    // The probe pushes state and geometry together because it draws exactly
    // one mesh. An authored frame draws a variable number, and the state is
    // identical for all of them, so it is pushed once here instead.
    spritePassActive_ = false;
    spriteCount_ = 0;
    if (!UploadVertexProgram(kVertexProgram, static_cast<unsigned>(sizeof(kVertexProgram) / 16))) return false;
    PushUnlitCombiner();
    PushRenderState();
    return true;
}

bool XboxRasterRenderer::DrawAuthored(const Matrix4& clip,
                                      const CubeVertex* gpuVertices,
                                      unsigned vertexCount,
                                      const unsigned short* indices,
                                      unsigned indexCount,
                                      const XboxShaderMaterial* material,
                                      const XboxRuntimeTexture* texture) {
    if (!initialized_ || spritePassActive_) {
        Diagnostics::Log("renderer error: DrawAuthored before Initialize");
        return false;
    }
    if (gpuVertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount == 0 ||
        (indexCount % 3) != 0) {
        Diagnostics::Log("renderer error: authored mesh is malformed (%u verts, %u indices)",
                         vertexCount, indexCount);
        return false;
    }
    if (material != nullptr) {
#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
        if (texture != nullptr && texture->IsDisposedNative()) return false;
        const char* reason = nullptr;
        if (!BindShaderMaterialTexture(*material, clip, static_cast<unsigned>(viewportScale_[0] * 2),
                static_cast<unsigned>(-viewportScale_[1] * 2), texture == nullptr ? nullptr : &texture->Layout(),
                texture == nullptr ? 0 : texture->GpuOffset(), PushCompiledInstruction, PushCompiledRegister,
                PushCompiledConstants, nullptr, reason, texture == nullptr ? 0 : texture->PaletteGpuOffset())) {
            Diagnostics::Log("compiled textured material rejected: %s", reason == nullptr ? "invalid binding" : reason);
            return false;
        }
#else
        if (texture != nullptr) return false;
        if (!BindCompiledMaterial(*material, clip, 1, 1)) return false;
#endif
    } else {
        if (texture != nullptr) return false;
#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
        if (!BindTextureStage(nullptr, 0, PushCompiledRegister, nullptr)) return false;
#endif
        if (!UploadVertexProgram(kVertexProgram, static_cast<unsigned>(sizeof(kVertexProgram) / 16))) return false;
        PushUnlitCombiner();
        PushConstants(clip);
    }
    PushVertexAttributes(gpuVertices);
    PushIndexedTriangles(indices, indexCount);
    return true;
}

void XboxRasterRenderer::Shutdown() {
    spritePassActive_ = false;
    if (gpuSpriteVertices_ != nullptr) {
        MmFreeContiguousMemory(gpuSpriteVertices_);
        gpuSpriteVertices_ = nullptr;
        ++counters_.Releases;
    }
    if (gpuVertices_ != nullptr) {
        MmFreeContiguousMemory(gpuVertices_);
        gpuVertices_ = nullptr;
        ++counters_.Releases;
    }
    if (initialized_) {
        Diagnostics::Log("renderer-shutdown: alloc %u free %u draws %u tris %u", counters_.Allocations,
                         counters_.Releases, counters_.DrawCalls, counters_.Triangles);
    }
    initialized_ = false;
}

} // namespace helengine::xbox
