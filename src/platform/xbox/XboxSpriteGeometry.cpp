#include "XboxSpriteGeometry.hpp"

#include <cmath>

namespace helengine::xbox {

XboxSpriteStatus TryBuildXboxSpriteQuad(const XboxSpriteInput& input, XboxSpriteQuad& output) noexcept {
    if (!std::isfinite(input.X) || !std::isfinite(input.Y) || !std::isfinite(input.Width) ||
        !std::isfinite(input.Height) || !std::isfinite(input.RotationRadians) ||
        !std::isfinite(input.SourceX) || !std::isfinite(input.SourceY) ||
        !std::isfinite(input.SourceWidth) || !std::isfinite(input.SourceHeight)) {
        return XboxSpriteStatus::NonFinite;
    }
    if (input.RealTextureWidth == 0 || input.RealTextureHeight == 0 || input.StoredTextureWidth == 0 ||
        input.StoredTextureHeight == 0 || input.RealTextureWidth > input.StoredTextureWidth ||
        input.RealTextureHeight > input.StoredTextureHeight) {
        return XboxSpriteStatus::InvalidExtent;
    }
    if (input.Width <= 0.0f || input.Height <= 0.0f) return XboxSpriteStatus::InvalidSize;
    if (input.SourceX < 0.0f || input.SourceY < 0.0f || input.SourceWidth <= 0.0f ||
        input.SourceHeight <= 0.0f || input.SourceX > 1.0f || input.SourceY > 1.0f ||
        input.SourceWidth > 1.0f - input.SourceX || input.SourceHeight > 1.0f - input.SourceY) {
        return XboxSpriteStatus::InvalidSourceRect;
    }

    const float centerX = input.X + input.Width * 0.5f;
    const float centerY = input.Y + input.Height * 0.5f;
    const float cosine = std::cos(input.RotationRadians);
    const float sine = std::sin(input.RotationRadians);
    if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(cosine) ||
        !std::isfinite(sine)) {
        return XboxSpriteStatus::NonFinite;
    }

    const float halfWidth = input.Width * 0.5f;
    const float halfHeight = input.Height * 0.5f;
    const float localX[4] = {-halfWidth, halfWidth, halfWidth, -halfWidth};
    const float localY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
    const float sourceU[4] = {input.SourceX, input.SourceX + input.SourceWidth,
                              input.SourceX + input.SourceWidth, input.SourceX};
    const float sourceV[4] = {input.SourceY, input.SourceY,
                              input.SourceY + input.SourceHeight, input.SourceY + input.SourceHeight};
    // NV2A's unnormalized linear-texture coordinates are measured in texels
    // (the pinned nxdk mesh sample supplies values up to texture.width/height).
    // Engine source rectangles are normalized, so convert them to texel units.
    const float uScale = static_cast<float>(input.RealTextureWidth);
    const float vScale = static_cast<float>(input.RealTextureHeight);
    const float inverse255 = 1.0f / 255.0f;
    const float tint[4] = {input.Tint[0] * inverse255, input.Tint[1] * inverse255,
                           input.Tint[2] * inverse255, input.Tint[3] * inverse255};

    XboxSpriteQuad candidate{};
    for (unsigned i = 0; i < 4; ++i) {
        XboxSpriteVertex& vertex = candidate.Vertices[i];
        vertex.X = centerX + localX[i] * cosine - localY[i] * sine;
        vertex.Y = centerY + localX[i] * sine + localY[i] * cosine;
        vertex.U = sourceU[i] * uScale;
        vertex.V = sourceV[i] * vScale;
        vertex.R = tint[0];
        vertex.G = tint[1];
        vertex.B = tint[2];
        vertex.A = tint[3];
        if (!std::isfinite(vertex.X) || !std::isfinite(vertex.Y) ||
            !std::isfinite(vertex.U) || !std::isfinite(vertex.V)) {
            return XboxSpriteStatus::NonFinite;
        }
    }
    output = candidate;
    return XboxSpriteStatus::Ok;
}

} // namespace helengine::xbox
