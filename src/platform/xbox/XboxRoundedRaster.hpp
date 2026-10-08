#pragma once
#include "XboxTextureFormat.hpp"
#include <vector>
namespace helengine::xbox {
struct XboxRoundedStyle {
    unsigned Width = 0, Height = 0;
    float Radius = 0, Border = 0;
    unsigned Corners = 15;
    XboxRgbaPixel Fill{255,255,255,255}, Outline{0,0,0,255};
    bool operator==(const XboxRoundedStyle& other) const;
};
bool NormalizeRoundedStyle(XboxRoundedStyle& style);
unsigned RoundedRasterExtent(unsigned extent);
bool RasterizeRounded(const XboxRoundedStyle& style, std::vector<XboxRgbaPixel>& pixels);
}
