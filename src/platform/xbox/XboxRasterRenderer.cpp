#include "XboxRasterRenderer.hpp"

#include "XboxDiagnostics.hpp"

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
static_assert(sizeof(CubeVertex) == 28, "CubeVertex must be 28 bytes (3 + 4 floats)");
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

// Single triangle for the Triangle probe: counter-clockwise from the camera,
// red/green/blue corners, drawn through the same indexed path as the cube.
const CubeVertex kTriangleVertices[3] = {
    {{-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
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

} // namespace

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

    if (!UploadPrograms()) {
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
    const unsigned instructionCount = static_cast<unsigned>(sizeof(kVertexProgram) / 16);
    for (unsigned i = 0; i < instructionCount; ++i) {
        p = pb_begin();
        pb_push(p++, NV097_SET_TRANSFORM_PROGRAM, 4);
        std::memcpy(p, &kVertexProgram[i * 4], 16);
        p += 4;
        pb_end(p);
    }

    // Register combiner: generated pbkit pushes from shaders/unlit.psh.
    p = pb_begin();
#include "shaders/unlit_psh.inl"
    pb_end(p);
    Diagnostics::Log("renderer: uploaded %u vertex program instructions and unlit combiner", instructionCount);
    return true;
}

void XboxRasterRenderer::PushRenderState() {
    uint32_t* p = pb_begin();
    p = pb_push1(p, NV097_SET_LIGHTING_ENABLE, 0);
    p = pb_push1(p, NV097_SET_SPECULAR_ENABLE, 0);
    p = pb_push1(p, NV097_SET_FOG_ENABLE, 0);
    p = pb_push1(p, NV097_SET_BLEND_ENABLE, 0);
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
        p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0 + stage * 64, 0); // texture stage disabled
    }
    pb_end(p);
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
    if (!initialized_) {
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
    PushRenderState();
    return true;
}

bool XboxRasterRenderer::DrawAuthored(const Matrix4& clip,
                                      const CubeVertex* gpuVertices,
                                      unsigned vertexCount,
                                      const unsigned short* indices,
                                      unsigned indexCount) {
    if (!initialized_) {
        Diagnostics::Log("renderer error: DrawAuthored before Initialize");
        return false;
    }
    if (gpuVertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount == 0 ||
        (indexCount % 3) != 0) {
        Diagnostics::Log("renderer error: authored mesh is malformed (%u verts, %u indices)",
                         vertexCount, indexCount);
        return false;
    }
    PushConstants(clip);
    PushVertexAttributes(gpuVertices);
    PushIndexedTriangles(indices, indexCount);
    return true;
}

void XboxRasterRenderer::Shutdown() {
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
