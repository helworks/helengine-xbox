#pragma once

#include <cstdint>

namespace helengine::xbox {

struct XboxSpriteInput {
    float X;
    float Y;
    float Width;
    float Height;
    float RotationRadians;
    float SourceX;
    float SourceY;
    float SourceWidth;
    float SourceHeight;
    std::uint32_t RealTextureWidth;
    std::uint32_t RealTextureHeight;
    std::uint32_t StoredTextureWidth;
    std::uint32_t StoredTextureHeight;
    std::uint8_t Tint[4];
};

struct XboxSpriteVertex {
    float X;
    float Y;
    float U;
    float V;
    float R;
    float G;
    float B;
    float A;
};

struct XboxSpriteQuad {
    XboxSpriteVertex Vertices[4];
};

enum class XboxSpriteStatus : std::uint8_t {
    Ok,
    InvalidExtent,
    InvalidSize,
    InvalidSourceRect,
    NonFinite,
};

XboxSpriteStatus TryBuildXboxSpriteQuad(const XboxSpriteInput& input, XboxSpriteQuad& output) noexcept;

} // namespace helengine::xbox
