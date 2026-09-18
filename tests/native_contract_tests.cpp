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
#include "platform/xbox/XboxContentPath.hpp"
#include "platform/xbox/XboxCubeMesh.hpp"
#include "platform/xbox/XboxMath.hpp"

using helengine::xbox::CubeGeometry;
using helengine::xbox::CubeVertex;
using helengine::xbox::Identity;
using helengine::xbox::MakeCubeGeometry;
using helengine::xbox::Matrix4;
using helengine::xbox::Multiply;
using helengine::xbox::RotationX;
using helengine::xbox::RotationY;
using helengine::xbox::Transform;
using helengine::xbox::Translation;
using helengine::xbox::TryPerspective;
using helengine::xbox::Vector4;

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

} // namespace

int main() {
    const bool ok = TestCubeGeometry() && TestMath() && TestDefaultPoseIsInFrontOfCamera() &&
                    TestContentPathResolution();
    if (!ok) {
        std::fprintf(stderr, "native contract tests FAILED after %d checks\n", g_checks);
        return 1;
    }
    std::printf("native contract tests passed (%d checks)\n", g_checks);
    return 0;
}
