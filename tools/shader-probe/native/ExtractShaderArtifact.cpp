#include "XboxPixelShaderBaseline.hpp"
#include "platform/xbox/XboxShaderArtifact.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>

using namespace helengine::xbox;
#if !defined(XBOX_EXTRACT_VERTEX)
static std::vector<uint32_t> PixelWords;
static uint32_t* pb_push1(uint32_t* p, uint32_t method, uint32_t value) {
    PixelWords.push_back(method);
    PixelWords.push_back(value);
    return p + 2;
}
static void ExtractPixel() {
    uint32_t buffer[1024] = {};
    uint32_t* p = shaderprobe::PushPixelShaderBaseline(buffer, pb_push1);
#include "shader.inl"
    (void)p;
}
#else
static const uint32_t VertexWords[] = {
#include "shader.inl"
};
#endif
static void Word(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
static int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool Hash(std::vector<uint8_t>& bytes, size_t offset, const char* text) {
    if (std::strlen(text) != 64) return false;
    for (unsigned i = 0; i < 32; ++i) {
        const int high = Hex(text[i * 2]), low = Hex(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        bytes[offset + i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}
static bool Build(const uint32_t* words, size_t count, XboxShaderStage stage,
                  const char* sourceHash, const char* toolsHash, const char* entryHash,
                  std::vector<uint8_t>& bytes, const char* bindingPath, const char* resourcePath) {
    std::vector<uint8_t> bindings;
    if (bindingPath != nullptr) {
        std::ifstream file(bindingPath, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 96 * 72) return false;
        file.seekg(0);
        bindings.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (file.bad() || bindings.size() % 72 != 0) return false;
    }
    std::vector<uint8_t> resources;
    if (resourcePath != nullptr) {
        std::ifstream file(resourcePath, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 102 * 80) return false;
        file.seekg(0);
        resources.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (file.bad() || resources.size() % 80 != 0) return false;
    }
    bytes.assign(128 + count * 4 + bindings.size() + resources.size(), 0);
    bool physicalInterface = false;
    for (size_t offset = 0; offset < resources.size(); offset += 80) {
        const uint32_t kind = ReadShaderWord(resources.data() + offset);
        if (kind >= 4 && kind <= 7) physicalInterface = true;
    }
    Word(bytes, 0, 0x31535848);
    Word(bytes, 4, !resources.empty() ? (stage == XboxShaderStage::Vertex ? 5 : 4) :
        (bindingPath == nullptr ? 1 : (stage == XboxShaderStage::Pixel && !bindings.empty() ? 3 : 2)));
    if (physicalInterface) Word(bytes, 4, 6);
    Word(bytes, 8, static_cast<uint32_t>(stage));
    Word(bytes, 12, static_cast<uint32_t>(count));
    Word(bytes, 16, 2);
    if (!Hash(bytes, 24, sourceHash) || !Hash(bytes, 56, toolsHash) || !Hash(bytes, 88, entryHash)) return false;
    for (size_t i = 0; i < count; ++i) Word(bytes, 128 + i * 4, words[i]);
    std::copy(bindings.begin(), bindings.end(), bytes.begin() + 128 + count * 4);
    Word(bytes, 120, static_cast<uint32_t>(bindings.size() / 72));
    std::copy(resources.begin(), resources.end(), bytes.begin() + 128 + count * 4 + bindings.size());
    Word(bytes, 124, static_cast<uint32_t>(resources.size() / 80));
    Word(bytes, 20, ShaderPayloadChecksum(bytes.data() + 128, bytes.size() - 128));
    XboxShaderArtifactView view{};
    const char* reason = nullptr;
    if (!ValidateShaderArtifact(bytes.data(), bytes.size(), stage, view, reason)) {
        std::fprintf(stderr, "Extraction rejected: %s\n", reason);
        return false;
    }
    return true;
}
static bool Write(const char* path, const std::vector<uint8_t>& bytes) {
    FILE* file = std::fopen(path, "wbx");
    if (file == nullptr) return false;
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    return written && closed;
}
int main(int argc, char** argv) {
    if (argc != 5 && argc != 6 && argc != 7) {
        std::fputs("Expected output-path source-sha256 tools-sha256 entry-sha256 [binding-table [resource-table]]\n", stderr);
        return 1;
    }
    std::vector<uint8_t> artifact;
#if defined(XBOX_EXTRACT_VERTEX)
    if (!Build(VertexWords, sizeof(VertexWords) / sizeof(uint32_t), XboxShaderStage::Vertex,
               argv[2], argv[3], argv[4], artifact, argc >= 6 ? argv[5] : nullptr, argc == 7 ? argv[6] : nullptr)) return 1;
#else
    ExtractPixel();
    if (!Build(PixelWords.data(), PixelWords.size(), XboxShaderStage::Pixel,
               argv[2], argv[3], argv[4], artifact, argc >= 6 ? argv[5] : nullptr, argc == 7 ? argv[6] : nullptr)) return 1;
#endif
    if (!Write(argv[1], artifact)) {
        std::fputs("Cannot publish shader artifacts (outputs must be new).\n", stderr);
        return 1;
    }
    std::printf("Validated shader artifact: %zu bytes.\n", artifact.size());
}
