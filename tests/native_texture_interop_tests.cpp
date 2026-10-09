#include "platform/xbox/XboxNativeTexturePayload.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace helengine::xbox;

// Read actual C# encoder output instead of rebuilding a native header in this test.
static std::vector<std::uint8_t> ReadFixture(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

static std::uint32_t ReadWord(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    assert(offset + 4 <= bytes.size());
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    constexpr std::uint32_t codes[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0b, 0x0c, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x16, 0x17, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
        0x1f, 0x20, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2c, 0x2e, 0x30, 0x31, 0x35,
        0x3a, 0x3b, 0x3c, 0x3f, 0x40, 0x41
    };
    static_assert(sizeof(codes) / sizeof(codes[0]) == XboxNativeFormatCount);
    for (const auto code : codes) {
        char name[24];
        std::snprintf(name, sizeof(name), "/format_%02X", code);
        const std::string stem = std::string(argv[1]) + name;
        const auto colors = ReadFixture(stem + ".colors");
        const auto palette = ReadFixture(stem + ".palette");
        const auto preview = ReadFixture(stem + ".rgba");
        const auto alpha = ReadFixture(stem + ".alpha");
        assert(alpha.size() == 1 && alpha[0] <= static_cast<std::uint8_t>(XboxTextureAlphaPrecision::A8));
        assert(preview.size() == 3 * 5 * 4);
        assert(ReadWord(colors, 0) == 0x31585458 && ReadWord(colors, 4) == 1);
        assert(ReadWord(colors, 8) == code && ReadWord(colors, 12) == 0);
        const XboxTextureSourceView source{
            3, 5, XboxTextureColorFormat::XboxNative, static_cast<XboxTextureAlphaPrecision>(alpha[0]),
            colors.data(), colors.size(), palette.empty() ? nullptr : palette.data(), palette.size()
        };
        XboxNativeTexturePayload payload{};
        assert(ParseXboxNativeTexturePayload(source, payload) == XboxTextureStatus::Ok);
        assert(payload.Layout.RealWidth == 3 && payload.Layout.RealHeight == 5);
        assert(payload.Layout.HardwareFormat == code);
        assert(payload.Texels == colors.data() + 48);
        assert(payload.Layout.ByteCount == colors.size() - 48);
        assert(payload.PaletteByteCount == palette.size());
        assert(payload.Layout.StorageWidth == ReadWord(colors, 24));
        assert(payload.Layout.StorageHeight == ReadWord(colors, 28));
        assert(GetXboxTextureFormatWord(payload.Layout) != 0);
        assert(ValidateXboxTextureLayout(payload.Layout));
        XboxNativeFormatInfo info{};
        assert(TryGetXboxNativeFormatInfo(code, info));
        const float expectedX = info.Linear ? 3.0f : 3.0f / payload.Layout.StorageWidth;
        const float expectedY = info.Linear ? 5.0f : 5.0f / payload.Layout.StorageHeight;
        assert(std::fabs(GetXboxTextureUvScaleX(payload.Layout) - expectedX) < 0.000001f);
        assert(std::fabs(GetXboxTextureUvScaleY(payload.Layout) - expectedY) < 0.000001f);
        if (info.Paletted) {
            assert(payload.Layout.PaletteEntries == 32 && palette.size() == 128);
            assert(palette[0] == 0 && palette[1] == 0 && palette[2] == 255 && palette[3] == 255);
        } else {
            assert(payload.Layout.PaletteEntries == 0 && palette.empty());
        }
        auto truncated = source;
        --truncated.ColorsLength;
        assert(ParseXboxNativeTexturePayload(truncated, payload) != XboxTextureStatus::Ok);
        assert(payload.Texels == nullptr && payload.Palette == nullptr);
    }
    std::puts("42 managed native payloads accepted by C++ with exact storage, palettes and UV scales; truncation rejected.");
}
