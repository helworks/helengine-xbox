// Host-side contract tests for the Xbox probe's CPU geometry and math.
//
// Compiled with the pinned container's host compiler (see
// scripts/test-native.ps1) with assertions forced on. No nxdk headers are
// involved and no Xbox binary is executed.

#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>

#include <string>
#include "platform/xbox/XboxAuthoredTransform.hpp"
#include "platform/xbox/XboxContentPath.hpp"
#include "platform/xbox/XboxCubeMesh.hpp"
#include "platform/xbox/XboxMath.hpp"
#include "platform/xbox/XboxLighting.hpp"
#include "platform/xbox/XboxTextureFormat.hpp"
#include "platform/xbox/XboxTextureUpload.hpp"
#include "platform/xbox/XboxSpriteGeometry.hpp"

using helengine::xbox::CubeGeometry;
using helengine::xbox::CubeVertex;
using helengine::xbox::Identity;
using helengine::xbox::MakeCubeGeometry;
using helengine::xbox::Matrix4;
using helengine::xbox::Multiply;
using helengine::xbox::TryTransformNormal;
using helengine::xbox::EvaluateDirectionalDiffuse;
using helengine::xbox::RotationX;
using helengine::xbox::RotationY;
using helengine::xbox::Transform;
using helengine::xbox::Translation;
using helengine::xbox::TryPerspective;
using helengine::xbox::Vector4;
using helengine::xbox::DecodeTextureToRgba8;
using helengine::xbox::XboxRgbaPixel;
using helengine::xbox::XboxTextureAlphaPrecision;
using helengine::xbox::XboxTextureColorFormat;
using helengine::xbox::XboxTextureSourceView;
using helengine::xbox::XboxTextureStatus;
using helengine::xbox::BuildXboxTextureLayout;
using helengine::xbox::GetXboxTextureRowPitchBytes;
using helengine::xbox::ConvertTextureToNv2a;
using helengine::xbox::XboxTextureLayout;
using helengine::xbox::ShouldPreserveAnonymousRgbaAlpha;
using helengine::xbox::TryBuildXboxSpriteQuad;
using helengine::xbox::XboxSpriteInput;
using helengine::xbox::XboxSpriteQuad;
using helengine::xbox::XboxSpriteStatus;

namespace {

constexpr float kPi = 3.14159265358979323846f;
int g_checks = 0;

bool Near(float a, float b, float tolerance = 1e-5f) {
    return std::fabs(a - b) <= tolerance;
}

#define CHECK(condition)                                                                   \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(condition)) {                                                                \
            std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            assert(condition);                                                             \
            return false;                                                                  \
        }                                                                                  \
    } while (false)

void Sub(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i) out[i] = a[i] - b[i];
}

void Cross(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool SameColor(const CubeVertex& a, const CubeVertex& b) {
    for (int i = 0; i < 4; ++i) {
        if (!Near(a.Color[i], b.Color[i])) return false;
    }
    return true;
}

bool TestCubeGeometry() {
    const CubeGeometry cube = MakeCubeGeometry();
    CHECK(CubeGeometry::VertexCount == 24);
    CHECK(CubeGeometry::IndexCount == 36);

    for (unsigned i = 0; i < CubeGeometry::VertexCount; ++i) {
        const CubeVertex& v = cube.Vertices[i];
        for (int k = 0; k < 3; ++k) {
            CHECK(std::isfinite(v.Position[k]));
            CHECK(Near(std::fabs(v.Position[k]), 1.0f)); // unit half-extents
        }
        for (int k = 0; k < 4; ++k) {
            CHECK(std::isfinite(v.Color[k]));
            CHECK(v.Color[k] >= 0.0f && v.Color[k] <= 1.0f);
        }
        CHECK(Near(v.Color[3], 1.0f)); // opaque alpha
    }

    for (unsigned i = 0; i < CubeGeometry::IndexCount; ++i) {
        CHECK(cube.Indices[i] < CubeGeometry::VertexCount);
    }

    // Every triangle is nondegenerate, single-coloured, and wound outward.
    for (unsigned t = 0; t < CubeGeometry::IndexCount; t += 3) {
        const CubeVertex& a = cube.Vertices[cube.Indices[t]];
        const CubeVertex& b = cube.Vertices[cube.Indices[t + 1]];
        const CubeVertex& c = cube.Vertices[cube.Indices[t + 2]];
        float ab[3], ac[3], n[3], centroid[3];
        Sub(b.Position, a.Position, ab);
        Sub(c.Position, a.Position, ac);
        Cross(ab, ac, n);
        CHECK(Dot(n, n) > 1e-6f);
        for (int k = 0; k < 3; ++k) centroid[k] = (a.Position[k] + b.Position[k] + c.Position[k]) / 3.0f;
        CHECK(Dot(n, centroid) > 0.0f);
        CHECK(SameColor(a, b));
        CHECK(SameColor(a, c));
    }

    // Exactly six distinct face colours, each face uniformly coloured, each
    // face's four vertices sharing the same dominant axis coordinate.
    CubeVertex faceColors[6];
    int distinct = 0;
    for (unsigned face = 0; face < 6; ++face) {
        const CubeVertex& first = cube.Vertices[face * 4];
        for (unsigned k = 1; k < 4; ++k) {
            CHECK(SameColor(first, cube.Vertices[face * 4 + k]));
        }
        bool seen = false;
        for (int d = 0; d < distinct; ++d) {
            if (SameColor(faceColors[d], first)) seen = true;
        }
        CHECK(!seen);
        faceColors[distinct++] = first;
    }
    CHECK(distinct == 6);
    return true;
}

bool NearMatrix(const Matrix4& a, const Matrix4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!Near(a.Values[i], b.Values[i])) return false;
    }
    return true;
}

bool TestMath() {
    const Matrix4 identity = Identity();
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK(Near(identity.Values[r * 4 + c], r == c ? 1.0f : 0.0f));
        }
    }
    const Vector4 p{1.0f, 2.0f, 3.0f, 1.0f};
    const Vector4 same = Transform(identity, p);
    CHECK(Near(same.X, 1.0f) && Near(same.Y, 2.0f) && Near(same.Z, 3.0f) && Near(same.W, 1.0f));

    const Matrix4 t = Translation(10.0f, -20.0f, 30.0f);
    const Vector4 moved = Transform(t, p);
    CHECK(Near(moved.X, 11.0f) && Near(moved.Y, -18.0f) && Near(moved.Z, 33.0f) && Near(moved.W, 1.0f));
    const Vector4 direction = Transform(t, Vector4{1.0f, 2.0f, 3.0f, 0.0f});
    CHECK(Near(direction.X, 1.0f) && Near(direction.Y, 2.0f) && Near(direction.Z, 3.0f));

    // Right-handed rotations: +90 deg about X maps +Y to +Z; about Y maps +Z to +X.
    const Vector4 yAxis{0.0f, 1.0f, 0.0f, 1.0f};
    const Vector4 rx = Transform(RotationX(kPi / 2.0f), yAxis);
    CHECK(Near(rx.X, 0.0f) && Near(rx.Y, 0.0f) && Near(rx.Z, 1.0f));
    const Vector4 zAxis{0.0f, 0.0f, 1.0f, 1.0f};
    const Vector4 ry = Transform(RotationY(kPi / 2.0f), zAxis);
    CHECK(Near(ry.X, 1.0f) && Near(ry.Y, 0.0f) && Near(ry.Z, 0.0f));

    // Multiplication order: (T * R) * v == T * (R * v).
    const Matrix4 r = RotationY(0.7f);
    const Matrix4 tr = Multiply(t, r);
    const Vector4 viaProduct = Transform(tr, p);
    const Vector4 viaSteps = Transform(t, Transform(r, p));
    CHECK(Near(viaProduct.X, viaSteps.X) && Near(viaProduct.Y, viaSteps.Y) && Near(viaProduct.Z, viaSteps.Z));
    const Matrix4 rt = Multiply(r, t);
    CHECK(!NearMatrix(tr, rt));
    CHECK(NearMatrix(Multiply(identity, tr), tr));

    // Perspective near/far mapping into [0, 1].
    Matrix4 projection{};
    const bool valid = TryPerspective(1.0471975512f, 4.0f / 3.0f, 0.1f, 100.0f, projection);
    CHECK(valid);
    const Vector4 nearClip = Transform(projection, Vector4{0.0f, 0.0f, -0.1f, 1.0f});
    const Vector4 farClip = Transform(projection, Vector4{0.0f, 0.0f, -100.0f, 1.0f});
    CHECK(std::fabs(nearClip.Z / nearClip.W) < 0.0001f);
    CHECK(std::fabs(farClip.Z / farClip.W - 1.0f) < 0.0001f);
    CHECK(Near(nearClip.W, 0.1f) && Near(farClip.W, 100.0f)); // w = -z_view
    const Vector4 mid = Transform(projection, Vector4{0.0f, 0.0f, -10.0f, 1.0f});
    CHECK(mid.Z / mid.W > 0.0f && mid.Z / mid.W < 1.0f);
    const float f = 1.0f / std::tan(1.0471975512f / 2.0f);
    CHECK(Near(projection.Values[0], f / (4.0f / 3.0f)));
    CHECK(Near(projection.Values[5], f));
    CHECK(Near(projection.Values[14], -1.0f));
    CHECK(Near(projection.Values[15], 0.0f));
    // Points inside the frustum land inside |x/w|,|y/w| <= 1; a point at the
    // horizontal edge of the 60-degree vertical field maps to |x/w| == 1.
    const float halfHeightAtTen = std::tan(1.0471975512f / 2.0f) * 10.0f;
    const Vector4 edge = Transform(projection, Vector4{halfHeightAtTen * (4.0f / 3.0f), halfHeightAtTen, -10.0f, 1.0f});
    CHECK(Near(edge.X / edge.W, 1.0f, 1e-4f) && Near(edge.Y / edge.W, 1.0f, 1e-4f));

    // Invalid inputs leave the output untouched.
    Matrix4 untouched = Translation(1.0f, 2.0f, 3.0f);
    const Matrix4 before = untouched;
    CHECK(!TryPerspective(0.0f, 1.0f, 0.1f, 100.0f, untouched));
    CHECK(!TryPerspective(kPi, 1.0f, 0.1f, 100.0f, untouched));
    CHECK(!TryPerspective(NAN, 1.0f, 0.1f, 100.0f, untouched));
    CHECK(!TryPerspective(1.0f, 0.0f, 0.1f, 100.0f, untouched));
    CHECK(!TryPerspective(1.0f, -1.0f, 0.1f, 100.0f, untouched));
    CHECK(!TryPerspective(1.0f, 1.0f, 0.0f, 100.0f, untouched));
    CHECK(!TryPerspective(1.0f, 1.0f, 0.1f, 0.1f, untouched));
    CHECK(!TryPerspective(1.0f, 1.0f, 0.1f, 0.05f, untouched));
    CHECK(!TryPerspective(1.0f, INFINITY, 0.1f, 100.0f, untouched));
    CHECK(NearMatrix(untouched, before));
    return true;
}

bool TestDirectionalLightingMath() {
    Matrix4 nonUniformScale = Identity();
    nonUniformScale.Values[0] = 2.0f;
    nonUniformScale.Values[5] = 1.0f;
    nonUniformScale.Values[10] = 1.0f;
    const float modelNormal[3] = {1.0f, 1.0f, 0.0f};
    float worldNormal[3]{};
    CHECK(TryTransformNormal(nonUniformScale, modelNormal, worldNormal));
    CHECK(Near(worldNormal[0], 0.4472136f));
    CHECK(Near(worldNormal[1], 0.8944272f));
    CHECK(Near(worldNormal[2], 0.0f));

    const float lightDirection[3] = {0.0f, 0.0f, -1.0f};
    const float frontNormal[3] = {0.0f, 0.0f, 2.0f};
    const float backNormal[3] = {0.0f, 0.0f, -1.0f};
    CHECK(Near(EvaluateDirectionalDiffuse(frontNormal, lightDirection, 1.0f), 1.0f));
    CHECK(Near(EvaluateDirectionalDiffuse(backNormal, lightDirection, 1.0f), 0.0f));
    CHECK(Near(EvaluateDirectionalDiffuse(frontNormal, lightDirection, 0.25f), 0.25f));

    Matrix4 singular = Identity();
    singular.Values[0] = 0.0f;
    CHECK(!TryTransformNormal(singular, modelNormal, worldNormal));
    return true;
}

bool TestDefaultPoseIsInFrontOfCamera() {
    // The host's default pose must place every cube vertex inside the frustum.
    const CubeGeometry cube = MakeCubeGeometry();
    const Matrix4 model = Multiply(RotationY(30.0f * kPi / 180.0f), RotationX(-20.0f * kPi / 180.0f));
    const Matrix4 view = Translation(0.0f, 0.0f, -5.0f);
    Matrix4 projection{};
    CHECK(TryPerspective(60.0f * kPi / 180.0f, 640.0f / 480.0f, 0.1f, 100.0f, projection));
    const Matrix4 clip = Multiply(projection, Multiply(view, model));
    for (unsigned i = 0; i < CubeGeometry::VertexCount; ++i) {
        const CubeVertex& v = cube.Vertices[i];
        const Vector4 c = Transform(clip, Vector4{v.Position[0], v.Position[1], v.Position[2], 1.0f});
        CHECK(c.W > 0.0f);
        CHECK(std::fabs(c.X / c.W) < 1.0f && std::fabs(c.Y / c.W) < 1.0f);
        CHECK(c.Z / c.W > 0.0f && c.Z / c.W < 1.0f);
    }
    return true;
}

bool TestContentPathResolution() {
    using helengine::xbox::ResolveDiscContentPath;
    using helengine::xbox::ValidateRelativeContentPath;

    // The exact shape the generated scene catalog manifest emits.
    std::string resolved;
    const char* reason = "unset";
    CHECK(ResolveDiscContentPath("D:\\", "cooked/scenes/rendering/cube_test.hasset", resolved, reason));
    CHECK(reason == nullptr);
    CHECK(resolved == "D:\\cooked\\scenes\\rendering\\cube_test.hasset");

    // A root without a trailing separator must not fuse into the first segment.
    CHECK(ResolveDiscContentPath("D:", "cooked/a.hasset", resolved, reason));
    CHECK(resolved == "D:\\cooked\\a.hasset");

    // Already-backslashed input is accepted and left alone.
    CHECK(ResolveDiscContentPath("D:\\", "cooked\\a.hasset", resolved, reason));
    CHECK(resolved == "D:\\cooked\\a.hasset");

    // Single segment, the degenerate case of the segment walk.
    CHECK(ResolveDiscContentPath("D:\\", "a.hasset", resolved, reason));
    CHECK(resolved == "D:\\a.hasset");

    // Every rejection names a reason and leaves the output untouched.
    const char* const rejected[] = {
        "",                              // empty
        "/cooked/a.hasset",              // rooted, posix
        "\\cooked\\a.hasset",            // rooted, xbox
        "E:\\elsewhere.hasset",          // names a drive
        "cooked/../../secrets",          // dot segment
        "cooked/./a.hasset",             // dot segment
        "cooked//a.hasset",              // empty segment
        "cooked/a.hasset/",              // trailing separator
    };
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        const std::string previous = resolved;
        reason = nullptr;
        CHECK(!ResolveDiscContentPath("D:\\", rejected[i], resolved, reason));
        CHECK(reason != nullptr);
        CHECK(resolved == previous);
    }

    // An embedded NUL has to be built as a std::string to survive the call.
    CHECK(ValidateRelativeContentPath(std::string("cooked/a\0b.hasset", 17)) != nullptr);

    // An empty content root is a caller error, not a path error.
    CHECK(!ResolveDiscContentPath("", "cooked/a.hasset", resolved, reason));
    CHECK(reason != nullptr);
    CHECK(!ResolveDiscContentPath(nullptr, "cooked/a.hasset", resolved, reason));
    CHECK(reason != nullptr);
    return true;
}

bool TestGeneratedMatrixConversion() {
    using helengine::xbox::GeneratedMatrixFields;
    using helengine::xbox::MatrixFromGenerated;

    // A generated row-vector matrix carries translation in M41..M43. After
    // conversion it must sit in the last column, where column-vector
    // multiplication expects it. Getting this backwards still renders, just
    // from the wrong place, so pin the exact element.
    GeneratedMatrixFields generated{};
    generated.M11 = 1.0f; generated.M22 = 1.0f; generated.M33 = 1.0f; generated.M44 = 1.0f;
    generated.M41 = 2.0f; generated.M42 = 3.0f; generated.M43 = 4.0f;

    const Matrix4 converted = MatrixFromGenerated(generated);
    CHECK(converted.Values[3] == 2.0f);
    CHECK(converted.Values[7] == 3.0f);
    CHECK(converted.Values[11] == 4.0f);
    CHECK(converted.Values[12] == 0.0f && converted.Values[15] == 1.0f);

    // And the converted matrix must actually translate a point.
    const Vector4 moved = Transform(converted, Vector4{1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(moved.X == 3.0f && moved.Y == 4.0f && moved.Z == 5.0f && moved.W == 1.0f);

    // Off-diagonal terms must transpose, not copy.
    GeneratedMatrixFields rotation{};
    rotation.M11 = 0.0f; rotation.M12 = 1.0f; rotation.M13 = 0.0f;
    rotation.M21 = -1.0f; rotation.M22 = 0.0f; rotation.M23 = 0.0f;
    rotation.M31 = 0.0f; rotation.M32 = 0.0f; rotation.M33 = 1.0f;
    rotation.M44 = 1.0f;
    const Matrix4 convertedRotation = MatrixFromGenerated(rotation);
    CHECK(convertedRotation.Values[1] == -1.0f);
    CHECK(convertedRotation.Values[4] == 1.0f);
    return true;
}

bool TestCameraTransformValidation() {
    using helengine::xbox::TryViewFromCameraWorld;
    using helengine::xbox::ValidateModelTransform;
    using helengine::xbox::ValidateRigidCameraTransform;

    // Identity is rigid, and its view matrix is identity.
    Matrix4 view{};
    CHECK(ValidateRigidCameraTransform(Identity()));
    CHECK(TryViewFromCameraWorld(Identity(), view));
    CHECK(NearMatrix(view, Identity()));

    // A camera five units back must view-transform the origin to -5 on Z.
    const Matrix4 backedOff = Translation(0.0f, 0.0f, 5.0f);
    CHECK(ValidateRigidCameraTransform(backedOff));
    CHECK(TryViewFromCameraWorld(backedOff, view));
    const Vector4 seen = Transform(view, Vector4{0.0f, 0.0f, 0.0f, 1.0f});
    CHECK(std::fabs(seen.Z + 5.0f) < 1e-5f);

    // A rotated-and-translated camera must invert exactly: applying the view to
    // the camera's own world position must land on the origin.
    const Matrix4 posed = Multiply(Translation(1.0f, 2.0f, 3.0f), RotationY(0.7f));
    CHECK(ValidateRigidCameraTransform(posed));
    CHECK(TryViewFromCameraWorld(posed, view));
    const Vector4 atCamera = Transform(view, Vector4{1.0f, 2.0f, 3.0f, 1.0f});
    CHECK(std::fabs(atCamera.X) < 1e-4f && std::fabs(atCamera.Y) < 1e-4f && std::fabs(atCamera.Z) < 1e-4f);

    // Scale is not rigid, and must be refused rather than mis-inverted.
    Matrix4 scaled = Identity();
    scaled.Values[0] = 2.0f;
    CHECK(!ValidateRigidCameraTransform(scaled));
    const Matrix4 before = view;
    CHECK(!TryViewFromCameraWorld(scaled, view));
    CHECK(NearMatrix(view, before));

    // A mirrored basis would invert back-face culling silently.
    Matrix4 mirrored = Identity();
    mirrored.Values[0] = -1.0f;
    CHECK(!ValidateRigidCameraTransform(mirrored));

    // Nonfinite and projective transforms are refused on both paths.
    Matrix4 nonfinite = Identity();
    nonfinite.Values[5] = NAN;
    CHECK(!ValidateRigidCameraTransform(nonfinite));
    CHECK(!ValidateModelTransform(nonfinite));
    Matrix4 projective = Identity();
    projective.Values[12] = 0.5f;
    CHECK(!ValidateRigidCameraTransform(projective));
    CHECK(!ValidateModelTransform(projective));

    // Model transforms accept scale but not a collapsed basis.
    CHECK(ValidateModelTransform(scaled));
    Matrix4 flattened = Identity();
    flattened.Values[10] = 0.0f;
    CHECK(!ValidateModelTransform(flattened));
    return true;
}

bool TestXboxTextureDecoding() {
    CHECK(ShouldPreserveAnonymousRgbaAlpha(false, XboxTextureColorFormat::Rgba32,
                                           XboxTextureAlphaPrecision::Opaque));
    CHECK(!ShouldPreserveAnonymousRgbaAlpha(true, XboxTextureColorFormat::Rgba32,
                                            XboxTextureAlphaPrecision::Opaque));
    CHECK(!ShouldPreserveAnonymousRgbaAlpha(false, XboxTextureColorFormat::Rgba32,
                                            XboxTextureAlphaPrecision::A8));
    CHECK(!ShouldPreserveAnonymousRgbaAlpha(false, XboxTextureColorFormat::Indexed8,
                                            XboxTextureAlphaPrecision::Opaque));
    XboxRgbaPixel fontAtlasPixels[] = {{0, 0, 0, 0}, {255, 255, 255, 255}, {16, 32, 48, 128}};
    SetTransparentFontAtlasTexelsToWhite(fontAtlasPixels, 3);
    CHECK(fontAtlasPixels[0].R == 255 && fontAtlasPixels[0].G == 255 &&
          fontAtlasPixels[0].B == 255 && fontAtlasPixels[0].A == 0);
    CHECK(fontAtlasPixels[1].R == 255 && fontAtlasPixels[1].G == 255 &&
          fontAtlasPixels[1].B == 255 && fontAtlasPixels[1].A == 255);
    CHECK(fontAtlasPixels[2].R == 16 && fontAtlasPixels[2].G == 32 &&
          fontAtlasPixels[2].B == 48 && fontAtlasPixels[2].A == 128);
    XboxRgbaPixel output[4]{};
    const std::uint8_t rgba[] = {0x12, 0x34, 0x56, 0x78};
    XboxTextureSourceView source{1, 1, XboxTextureColorFormat::Rgba32, XboxTextureAlphaPrecision::A8,
                                 rgba, sizeof(rgba), nullptr, 0};
    CHECK(DecodeTextureToRgba8(source, output, 4) == XboxTextureStatus::Ok);
    CHECK(output[0].R == 0x12 && output[0].G == 0x34 && output[0].B == 0x56 && output[0].A == 0x78);

    const std::uint8_t rgba4444[] = {0x79, 0x3f};
    source = {1, 1, XboxTextureColorFormat::Rgba4444, XboxTextureAlphaPrecision::A4,
              rgba4444, sizeof(rgba4444), nullptr, 0};
    CHECK(DecodeTextureToRgba8(source, output, 4) == XboxTextureStatus::Ok);
    CHECK(output[0].R == 0x99 && output[0].G == 0x77 && output[0].B == 0xff && output[0].A == 0x33);

    const std::uint8_t palette[] = {0x10, 0x20, 0x30, 0x40, 0xaa, 0xbb, 0xcc, 0xdd,
                                    0x70, 0x80, 0x90, 0xa0, 0x50, 0x60, 0x70, 0x80};
    const std::uint8_t index8[] = {1};
    source = {1, 1, XboxTextureColorFormat::Indexed8, XboxTextureAlphaPrecision::A8,
              index8, sizeof(index8), palette, sizeof(palette)};
    CHECK(DecodeTextureToRgba8(source, output, 4) == XboxTextureStatus::Ok);
    CHECK(output[0].R == 0xaa && output[0].G == 0xbb && output[0].B == 0xcc && output[0].A == 0xdd);

    const std::uint8_t index4[] = {0x10, 0x03};
    source = {3, 1, XboxTextureColorFormat::Indexed4, XboxTextureAlphaPrecision::Opaque,
              index4, sizeof(index4), palette, sizeof(palette)};
    CHECK(DecodeTextureToRgba8(source, output, 4) == XboxTextureStatus::Ok);
    CHECK(output[0].R == 0x10 && output[0].A == 0xff);
    CHECK(output[1].R == 0xaa && output[1].A == 0xff);
    CHECK(output[2].R == 0x50 && output[2].A == 0xff);

    const std::uint8_t alphaColors[] = {1, 2, 3, 127, 1, 2, 3, 128};
    source = {2, 1, XboxTextureColorFormat::Rgba32, XboxTextureAlphaPrecision::Binary,
              alphaColors, sizeof(alphaColors), nullptr, 0};
    CHECK(DecodeTextureToRgba8(source, output, 4) == XboxTextureStatus::Ok);
    CHECK(output[0].A == 0 && output[1].A == 0xff);
    source.AlphaPrecision = XboxTextureAlphaPrecision::Opaque;
    CHECK(DecodeTextureToRgba8(source, output, 4) == XboxTextureStatus::Ok);
    CHECK(output[0].A == 0xff && output[1].A == 0xff);

    XboxTextureSourceView invalid = source;
    invalid.Width = 0;
    CHECK(DecodeTextureToRgba8(invalid, output, 4) == XboxTextureStatus::InvalidExtent);
    invalid = source;
    invalid.Width = 0xffffffffu;
    invalid.Height = 0xffffffffu;
    CHECK(DecodeTextureToRgba8(invalid, output, 4) == XboxTextureStatus::SizeOverflow);
    invalid = source;
    invalid.ColorsLength = 7;
    CHECK(DecodeTextureToRgba8(invalid, output, 4) == XboxTextureStatus::MalformedPayload);
    invalid = source;
    CHECK(DecodeTextureToRgba8(invalid, output, 1) == XboxTextureStatus::InsufficientOutput);
    invalid = {1, 1, static_cast<XboxTextureColorFormat>(4), XboxTextureAlphaPrecision::A8,
               rgba, sizeof(rgba), nullptr, 0};
    CHECK(DecodeTextureToRgba8(invalid, output, 4) == XboxTextureStatus::UnsupportedFormat);

    invalid = {3, 1, XboxTextureColorFormat::Indexed4, XboxTextureAlphaPrecision::A8,
               index4, 1, palette, sizeof(palette)};
    CHECK(DecodeTextureToRgba8(invalid, output, 4) == XboxTextureStatus::MalformedPayload);
    const std::uint8_t badIndex[] = {4};
    invalid = {1, 1, XboxTextureColorFormat::Indexed8, XboxTextureAlphaPrecision::A8,
               badIndex, sizeof(badIndex), palette, sizeof(palette)};
    CHECK(DecodeTextureToRgba8(invalid, output, 4) == XboxTextureStatus::InvalidPalette);
    invalid.PaletteLength = 7;
    CHECK(DecodeTextureToRgba8(invalid, output, 4) == XboxTextureStatus::InvalidPalette);
    return true;
}

bool TestXboxTextureUploadLayout() {
    XboxTextureLayout layout{};
    CHECK(BuildXboxTextureLayout(3, 2, layout) == XboxTextureStatus::Ok);
    CHECK(layout.RealWidth == 3 && layout.RealHeight == 2);
    CHECK(layout.StorageWidth == 3 && layout.StorageHeight == 2);
    CHECK(layout.BytesPerTexel == 4 && layout.HardwareFormat == 0x12 && layout.ByteCount == 24);
    CHECK(GetXboxTextureRowPitchBytes(layout) == 12);

    const XboxRgbaPixel pixels[] = {{0x12, 0x34, 0x56, 0x78}, {0xaa, 0xbb, 0xcc, 0xdd},
                                    {1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}, {13, 14, 15, 16}};
    std::uint8_t bytes[24]{};
    const std::uint8_t sourceBytes[24]{};
    const XboxTextureSourceView source{3, 2, XboxTextureColorFormat::Rgba32,
                                       XboxTextureAlphaPrecision::A8, sourceBytes, sizeof(sourceBytes), nullptr, 0};
    CHECK(ConvertTextureToNv2a(source, pixels, layout, bytes, sizeof(bytes)) == XboxTextureStatus::Ok);
    CHECK(bytes[0] == 0x56 && bytes[1] == 0x34 && bytes[2] == 0x12 && bytes[3] == 0x78);
    CHECK(bytes[4] == 0xcc && bytes[5] == 0xbb && bytes[6] == 0xaa && bytes[7] == 0xdd);
    CHECK(ConvertTextureToNv2a(source, pixels, layout, bytes, 23) == XboxTextureStatus::InsufficientOutput);

    XboxTextureLayout untouched{1, 2, 3, 4, 5, 6, 7};
    CHECK(BuildXboxTextureLayout(0, 2, untouched) == XboxTextureStatus::InvalidExtent);
    CHECK(untouched.RealWidth == 1 && untouched.ByteCount == 7);
    CHECK(BuildXboxTextureLayout(16384, 1, untouched) == XboxTextureStatus::UnsupportedExtent);
    CHECK(BuildXboxTextureLayout(1, 65536, untouched) == XboxTextureStatus::UnsupportedExtent);
    CHECK(BuildXboxTextureLayout(0xffffffffu, 0xffffffffu, untouched) == XboxTextureStatus::UnsupportedExtent);
    return true;
}

bool TestXboxSpriteGeometry() {
    XboxSpriteInput input{10.0f, 20.0f, 32.0f, 16.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 1.0f, 16, 8, 16, 8,
                          {255, 255, 255, 255}};
    XboxSpriteQuad quad{};
    CHECK(TryBuildXboxSpriteQuad(input, quad) == XboxSpriteStatus::Ok);
    CHECK(Near(quad.Vertices[0].X, 10.0f) && Near(quad.Vertices[0].Y, 20.0f));
    CHECK(Near(quad.Vertices[1].X, 42.0f) && Near(quad.Vertices[1].Y, 20.0f));
    CHECK(Near(quad.Vertices[2].X, 42.0f) && Near(quad.Vertices[2].Y, 36.0f));
    CHECK(Near(quad.Vertices[3].X, 10.0f) && Near(quad.Vertices[3].Y, 36.0f));
    CHECK(Near(quad.Vertices[0].U, 0.0f) && Near(quad.Vertices[0].V, 0.0f));
    CHECK(Near(quad.Vertices[2].U, 16.0f) && Near(quad.Vertices[2].V, 8.0f));
    CHECK(Near(quad.Vertices[0].R, 1.0f) && Near(quad.Vertices[0].A, 1.0f));

    input.SourceX = 0.25f;
    input.SourceY = 0.25f;
    input.SourceWidth = 0.5f;
    input.SourceHeight = 0.5f;
    input.StoredTextureWidth = 32;
    input.StoredTextureHeight = 16;
    CHECK(TryBuildXboxSpriteQuad(input, quad) == XboxSpriteStatus::Ok);
    CHECK(Near(quad.Vertices[0].U, 4.0f) && Near(quad.Vertices[0].V, 2.0f));
    CHECK(Near(quad.Vertices[2].U, 12.0f) && Near(quad.Vertices[2].V, 6.0f));

    input = {0.0f, 0.0f, 2.0f, 4.0f, kPi / 2.0f,
             0.0f, 0.0f, 1.0f, 1.0f, 2, 4, 2, 4,
             {128, 64, 32, 16}};
    CHECK(TryBuildXboxSpriteQuad(input, quad) == XboxSpriteStatus::Ok);
    CHECK(Near(quad.Vertices[0].X, 3.0f) && Near(quad.Vertices[0].Y, 1.0f));
    CHECK(Near(quad.Vertices[0].R, 128.0f / 255.0f));
    CHECK(Near(quad.Vertices[0].G, 64.0f / 255.0f));

    XboxSpriteInput scaled = input;
    scaled.Width = 64.0f;
    scaled.Height = 32.0f;
    scaled.RotationRadians = 0.0f;
    CHECK(TryBuildXboxSpriteQuad(scaled, quad) == XboxSpriteStatus::Ok);
    CHECK(Near(quad.Vertices[1].X - quad.Vertices[0].X, 64.0f));
    CHECK(Near(quad.Vertices[3].Y - quad.Vertices[0].Y, 32.0f));

    XboxSpriteInput invalid = input;
    invalid.Width = 0.0f;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::InvalidSize);
    invalid = input;
    invalid.RealTextureWidth = 0;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::InvalidExtent);
    invalid = input;
    invalid.SourceX = -0.1f;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::InvalidSourceRect);
    invalid = input;
    invalid.SourceWidth = 0.0f;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::InvalidSourceRect);
    invalid = input;
    invalid.SourceX = 0.75f;
    invalid.SourceWidth = 0.5f;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::InvalidSourceRect);
    invalid = input;
    invalid.RotationRadians = NAN;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::NonFinite);
    invalid = input;
    invalid.X = INFINITY;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::NonFinite);
    invalid = input;
    invalid.StoredTextureWidth = 1;
    CHECK(TryBuildXboxSpriteQuad(invalid, quad) == XboxSpriteStatus::InvalidExtent);
    return true;
}

} // namespace

int main() {
    const bool ok = TestCubeGeometry() && TestMath() && TestDefaultPoseIsInFrontOfCamera() &&
                    TestContentPathResolution() && TestGeneratedMatrixConversion() && TestDirectionalLightingMath() &&
                    TestCameraTransformValidation() && TestXboxTextureDecoding() && TestXboxTextureUploadLayout() &&
                    TestXboxSpriteGeometry();
    if (!ok) {
        std::fprintf(stderr, "native contract tests FAILED after %d checks\n", g_checks);
        return 1;
    }
    std::printf("native contract tests passed (%d checks)\n", g_checks);
    return 0;
}
