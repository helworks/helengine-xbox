#include "platform/xbox/XboxRoundedRaster.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace helengine::xbox;
int main() {
    XboxRoundedStyle style;
    style.Width = style.Height = 16;
    style.Radius = 8;
    style.Fill = {255,0,0,128};
    std::vector<XboxRgbaPixel> pixels;
    assert(RasterizeRounded(style, pixels));
    assert(pixels.size() == 256);
    assert(pixels[0].A == 0); // rounded corner must not become a square
    assert(pixels[8*16+8].R == 255 && pixels[8*16+8].A == 128);
    bool partial = false;
    for (auto p : pixels) partial |= p.A > 0 && p.A < 128;
    assert(partial); // preserve coverage AA, not a binary mask
    style.Corners = 14; // only top-left square
    assert(RasterizeRounded(style, pixels));
    assert(pixels[0].A == 128 && pixels[15].A == 0);
    style.Corners = 0;
    style.Border = 2;
    style.Outline = {0,0,255,255};
    assert(RasterizeRounded(style, pixels));
    assert(pixels[0].B == 255 && pixels[0].A == 255);
    assert(pixels[8*16+8].R == 255 && pixels[8*16+8].A == 128);
    style.Border = 1.5f;
    assert(RasterizeRounded(style, pixels));
    // At the inner boundary: half red (alpha128), half blue (alpha255).
    assert(pixels[17].R == 85 && pixels[17].B == 170 && pixels[17].A == 192);
    style.Border = 1000;
    assert(RasterizeRounded(style, pixels));
    assert(pixels[8*16+8].B == 255 && pixels[8*16+8].A == 255);
    auto same = style;
    assert(NormalizeRoundedStyle(same));
    style.Border = 8;
    assert(NormalizeRoundedStyle(style) && style == same);
    same.Fill.A = 0;
    assert(!(style == same));
    style.Width = 0;
    auto oldSize = pixels.size();
    assert(!RasterizeRounded(style, pixels) && pixels.size() == oldSize);
    style.Width = 16;
    style.Radius = std::numeric_limits<float>::quiet_NaN();
    assert(!RasterizeRounded(style, pixels));
    style.Radius = 0;
    style.Border = -3;
    style.Width = 10000;
    assert(RasterizeRounded(style, pixels));
    assert(pixels.size() == 512*16); // cap transient and cached texture size
    puts("Rounded raster: coverage, corner mask, border, straight alpha, normalization and bounds passed.");
}
