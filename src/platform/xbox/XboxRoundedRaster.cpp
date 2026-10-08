#include "XboxRoundedRaster.hpp"
#include <algorithm>
#include <cmath>
namespace helengine::xbox {
namespace {
bool Equal(XboxRgbaPixel a, XboxRgbaPixel b) {
    return a.R == b.R && a.G == b.G && a.B == b.B && a.A == b.A;
}
double Coverage(double x, double y, double halfWidth, double halfHeight, double radius, unsigned corners) {
    const unsigned corner = y < 0 ? (x < 0 ? 1 : 2) : (x < 0 ? 4 : 8);
    const double r = (corners & corner) ? std::clamp(radius, 0., std::min(halfWidth, halfHeight)) : 0.;
    const double dx = std::abs(x) - halfWidth + r, dy = std::abs(y) - halfHeight + r;
    const double px = std::max(dx, 0.), py = std::max(dy, 0.);
    const double distance = std::sqrt(px*px + py*py) + std::min(std::max(dx, dy), 0.) - r;
    const double t = std::clamp(distance + .5, 0., 1.);
    return 1. - t*t*(3. - 2.*t);
}
// nxdk's lround is an assert-only stub. Channels are finite and nonnegative;
// clamp before the cast and round half up without calling the CRT stub.
uint8_t Byte(double value) { return static_cast<uint8_t>(std::clamp(value, 0., 255.) + .5); }
}
bool XboxRoundedStyle::operator==(const XboxRoundedStyle& other) const {
    return Width == other.Width && Height == other.Height && Radius == other.Radius && Border == other.Border &&
        Corners == other.Corners && Equal(Fill, other.Fill) && Equal(Outline, other.Outline);
}
bool NormalizeRoundedStyle(XboxRoundedStyle& style) {
    if (!style.Width || !style.Height || !std::isfinite(style.Radius) || !std::isfinite(style.Border)) return false;
    const float half = std::min(style.Width, style.Height) * .5f;
    style.Radius = std::clamp(style.Radius, 0.f, half);
    style.Border = std::clamp(style.Border, 0.f, half);
    style.Corners &= 15;
    return true;
}
unsigned RoundedRasterExtent(unsigned extent) { return std::min(extent, 512u); }
bool RasterizeRounded(const XboxRoundedStyle& input, std::vector<XboxRgbaPixel>& pixels) {
    auto style = input;
    if (!NormalizeRoundedStyle(style)) return false;
    const unsigned width = RoundedRasterExtent(style.Width), height = RoundedRasterExtent(style.Height);
    pixels.resize(size_t(width)*height);
    const double hw = style.Width * .5, hh = style.Height * .5;
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        const double localX = (x + .5) * double(style.Width) / width - hw;
        const double localY = (y + .5) * double(style.Height) / height - hh;
        const double outer = Coverage(localX, localY, hw, hh, style.Radius, style.Corners);
        double inner = outer;
        if (style.Border > 0) inner = style.Border >= std::min(hw, hh) ? 0. :
            std::min(outer, Coverage(localX, localY, hw - style.Border, hh - style.Border,
                                    std::max(style.Radius - style.Border, 0.f), style.Corners));
        const double fill = style.Fill.A * inner, border = style.Outline.A * std::max(outer - inner, 0.);
        const double alpha = fill + border;
        XboxRgbaPixel pixel{};
        if (alpha > .0255) {
            pixel.R = Byte((style.Fill.R * fill + style.Outline.R * border) / alpha);
            pixel.G = Byte((style.Fill.G * fill + style.Outline.G * border) / alpha);
            pixel.B = Byte((style.Fill.B * fill + style.Outline.B * border) / alpha);
            pixel.A = Byte(alpha);
        }
        pixels[size_t(y)*width + x] = pixel;
    }
    return true;
}
}
