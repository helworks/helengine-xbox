#include "XboxShaderArtifact.hpp"
#include <pbkit/nv_regs.h>
#include <cstring>

namespace helengine::xbox {
uint32_t ReadShaderWord(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
uint32_t ShaderPayloadChecksum(const uint8_t* bytes, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
bool ApplyPixelShaderArtifact(const uint8_t* bytes, size_t length, XboxShaderRegisterWriter writer,
                             void* context, const char*& reason) {
    if (writer == nullptr) {
        reason = "Missing pixel register writer";
        return false;
    }
    XboxShaderArtifactView view{};
    if (!ValidateShaderArtifact(bytes, length, XboxShaderStage::Pixel, view, reason)) return false;
    for (uint32_t i = 0; i < view.WordCount; i += 2)
        writer(context, ReadShaderWord(view.Payload + i * 4), ReadShaderWord(view.Payload + (i + 1) * 4));
    return true;
}
bool ValidateShaderArtifact(const uint8_t* bytes, size_t length, XboxShaderStage expectedStage,
                            XboxShaderArtifactView& output, const char*& reason) {
    output = {};
    reason = "Invalid shader header";
    if (bytes == nullptr || length < XboxShaderHeaderBytes) return false;
    const uint32_t version = ReadShaderWord(bytes + 4);
    const uint32_t bindingCount = ReadShaderWord(bytes + 120);
    const uint32_t resourceCount = ReadShaderWord(bytes + 124);
    if (ReadShaderWord(bytes) != 0x31535848 || (version < 1 || version > 6) ||
        ReadShaderWord(bytes + 16) != 2 || (version == 1 && bindingCount != 0) || bindingCount > 96 ||
        (version < 4 && resourceCount != 0) || (version == 4 && resourceCount != 2) ||
        (version == 5 && (resourceCount == 0 || resourceCount > 93)) ||
        (version == 6 && (resourceCount == 0 || resourceCount > 102))) return false;
    const uint32_t stage = ReadShaderWord(bytes + 8);
    if ((stage != 1 && stage != 2) || stage != static_cast<uint32_t>(expectedStage)) return false;
    if (version == 3 && (stage != 2 || bindingCount == 0 || bindingCount > 2)) return false;
    if (version == 4 && (stage != 2 || bindingCount > 2)) return false;
    if (version == 5 && stage != 1) return false;
    if (version == 6 && stage == 2 && bindingCount > 2) return false;
    const uint32_t count = ReadShaderWord(bytes + 12);
    reason = "Invalid shader payload length";
    if (count == 0 || count > 544 || length != XboxShaderHeaderBytes + size_t(count) * 4 + size_t(bindingCount) * 72 + size_t(resourceCount) * 80) return false;
    const uint8_t* payload = bytes + XboxShaderHeaderBytes;
    reason = "Shader payload checksum mismatch";
    if (ShaderPayloadChecksum(payload, length - XboxShaderHeaderBytes) != ReadShaderWord(bytes + 20)) return false;
    const uint8_t* bindings = payload + size_t(count) * 4;
    reason = "Invalid shader constant bindings";
    if (expectedStage == XboxShaderStage::Pixel && bindingCount != 0 && version != 3 && version != 4 && version != 6) return false;
    bool occupied[96] = {};
    bool viewportScale = false, viewportOffset = false;
    for (uint32_t i = 0; i < bindingCount; ++i) {
        const uint8_t* record = bindings + i * 72;
        size_t nameLength = 0;
        while (nameLength < 64 && record[nameLength] != 0) {
            const uint8_t c = record[nameLength];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
                  (nameLength != 0 && c >= '0' && c <= '9'))) return false;
            ++nameLength;
        }
        if (nameLength == 0 || nameLength == 64) return false;
        for (size_t c = nameLength; c < 64; ++c) if (record[c] != 0) return false;
        const char* name = reinterpret_cast<const char*>(record);
        const uint32_t row = ReadShaderWord(record + 64), reg = ReadShaderWord(record + 68);
        if (row >= 96 || reg >= 96 || occupied[reg]) return false;
        if (expectedStage == XboxShaderStage::Pixel && reg > 1) return false;
        occupied[reg] = true;
        int reserved = -1;
        if (std::strcmp(name, "Xbox_TextureExtent") == 0) reserved = 93;
        if (std::strcmp(name, "Xbox_ViewportScale") == 0) { reserved = 94; viewportScale = true; }
        if (std::strcmp(name, "Xbox_ViewportOffset") == 0) { reserved = 95; viewportOffset = true; }
        if ((reserved >= 0 && (reg != static_cast<uint32_t>(reserved) || row != 0)) || (reserved < 0 && reg >= 93)) return false;
        for (uint32_t prior = 0; prior < i; ++prior) {
            const uint8_t* other = bindings + prior * 72;
            if (std::strcmp(name, reinterpret_cast<const char*>(other)) == 0 && row == ReadShaderWord(other + 64)) return false;
        }
    }
    if ((version == 2 || version >= 5) && expectedStage == XboxShaderStage::Vertex && (!viewportScale || !viewportOffset)) return false;
    const uint8_t* resources = bindings + size_t(bindingCount) * 72;
    bool texture = false, sampler = false;
    uint32_t interfaces = 0;
    bool requiredOutput = false;
    reason = "Invalid shader resource bindings";
    for (uint32_t i = 0; i < resourceCount; ++i) {
        const uint8_t* record = resources + i * 80;
        const uint32_t kind = ReadShaderWord(record);
        if (version == 5 || (version == 6 && kind == 3)) {
            const uint32_t reg = ReadShaderWord(record + 68);
            if (stage != 1 || kind != 3 || reg >= 93 || occupied[reg]) return false;
            occupied[reg] = true;
            for (unsigned component = 0; component < 4; ++component)
                if ((ReadShaderWord(record + 4 + component * 4) & 0x7f800000u) == 0x7f800000u) return false;
            for (unsigned byte = 20; byte < 68; ++byte) if (record[byte] != 0) return false;
            if (ReadShaderWord(record + 72) != 0 || ReadShaderWord(record + 76) != 0) return false;
            continue;
        }
        const bool interfaceRecord = version == 6 && kind >= 4 && kind <= 7;
        if (!interfaceRecord) {
            if (stage != 2) return false;
            if (kind == 1 && !texture) texture = true;
            else if (kind == 2 && !sampler) sampler = true;
            else return false;
            if (ReadShaderWord(record + 68) != 0 || ReadShaderWord(record + 72) != 0) return false;
        }
        if (ReadShaderWord(record + 76) != 0) return false;
        size_t nameLength = 0;
        while (nameLength < 64 && record[4 + nameLength] != 0) {
            const uint8_t c = record[4 + nameLength];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
                  (nameLength != 0 && c >= '0' && c <= '9'))) return false;
            ++nameLength;
        }
        if (nameLength == 0 || nameLength == 64) return false;
        for (size_t c = nameLength; c < 64; ++c) if (record[4 + c] != 0) return false;
        if (interfaceRecord) {
            if ((stage == 1 && kind != 4 && kind != 5) || (stage == 2 && kind != 6 && kind != 7)) return false;
            const char* semantic = reinterpret_cast<const char*>(record + 4);
            unsigned index, reg, components;
            if (std::strcmp(semantic, "POSITION") == 0 && stage == 1) {
                index = 0; reg = 0; components = kind == 4 ? 3 : 4;
                if (kind == 5) requiredOutput = true;
            } else if (std::strcmp(semantic, "COLOR0") == 0) {
                index = 1; reg = kind == 7 ? 0 : 3; components = 4;
                if (kind == 7) requiredOutput = true;
            } else if (std::strcmp(semantic, "TEXCOORD0") == 0 && kind != 7) {
                index = 2; reg = 9; components = 2;
            } else return false;
            if (kind == 4 && (index == 0 || index == 2) && ReadShaderWord(record + 72) == 4)
                components = 4;
            const uint32_t mask = 1u << ((kind - 4) * 3 + index);
            if ((interfaces & mask) != 0 || ReadShaderWord(record + 68) != reg || ReadShaderWord(record + 72) != components) return false;
            interfaces |= mask;
        }
    }
    if (version == 4 && (!texture || !sampler)) return false;
    if (version == 6 && (!requiredOutput || texture != sampler)) return false;
    if (expectedStage == XboxShaderStage::Vertex) {
        reason = "Invalid vertex instruction framing";
        if (count % 4 != 0) return false;
        for (uint32_t i = 0; i < count; i += 4) {
            if (ReadShaderWord(payload + i * 4) != 0) return false;
            const bool end = (ReadShaderWord(payload + (i + 3) * 4) & 1) != 0;
            if (end != (i + 4 == count)) return false;
        }
    } else {
        reason = "Invalid pixel baseline";
        if (count < 4 || count > 512 || count % 2 != 0 ||
            ReadShaderWord(payload) != NV097_SET_SHADER_STAGE_PROGRAM || ReadShaderWord(payload + 4) != 0 ||
            ReadShaderWord(payload + 8) != NV097_SET_SHADER_OTHER_STAGE_INPUT || ReadShaderWord(payload + 12) != 0) return false;
        uint32_t iterations = 0;
        uint32_t written[4] = {};
        bool finalColor = false, finalAlpha = false;
        uint32_t textureProgram = 0;
        for (uint32_t i = 0; i < count; i += 2) {
            const uint32_t method = ReadShaderWord(payload + i * 4);
            const uint32_t value = ReadShaderWord(payload + (i + 1) * 4);
            bool allowed = false;
            const uint32_t bases[] = {NV097_SET_COMBINER_COLOR_ICW, NV097_SET_COMBINER_COLOR_OCW,
                                     NV097_SET_COMBINER_ALPHA_ICW, NV097_SET_COMBINER_ALPHA_OCW};
            for (unsigned bank = 0; bank < 4; ++bank) {
                if (method >= bases[bank] && method < bases[bank] + 32 && (method - bases[bank]) % 4 == 0) {
                    written[bank] |= 1u << ((method - bases[bank]) / 4);
                    allowed = true;
                }
            }
            if (method == NV097_SET_SHADER_STAGE_PROGRAM) { allowed = value == 0 || value == 1; textureProgram = value; }
            if (method == NV097_SET_SHADER_OTHER_STAGE_INPUT) allowed = value == 0;
            if (method == NV097_SET_COMBINER_CONTROL) {
                iterations = value & NV097_SET_COMBINER_CONTROL_ITERATION_COUNT;
                allowed = iterations >= 1 && iterations <= 8;
                if (bindingCount != 0 && (value & (NV097_SET_COMBINER_CONTROL_FACTOR0 | NV097_SET_COMBINER_CONTROL_FACTOR1)) != 0)
                    allowed = false;
            }
            if (method == NV097_SET_COMBINER_SPECULAR_FOG_CW0) allowed = finalColor = true;
            if (method == NV097_SET_COMBINER_SPECULAR_FOG_CW1) allowed = finalAlpha = true;
            // fp20compiler emits packed ARGB literals into the two eight-stage
            // factor banks and/or the two final-combiner factors.
            const uint32_t factorBases[] = {NV097_SET_COMBINER_FACTOR0, NV097_SET_COMBINER_FACTOR1};
            for (uint32_t base : factorBases)
                if (method >= base && method < base + 32 && (method - base) % 4 == 0) allowed = true;
            if (method == NV097_SET_SPECULAR_FOG_FACTOR || method == NV097_SET_SPECULAR_FOG_FACTOR + 4) allowed = true;
            reason = "Forbidden pixel shader method or value";
            if (!allowed) return false;
        }
        reason = "Incomplete pixel combiner state";
        if (iterations == 0 || !finalColor || !finalAlpha) return false;
        const uint32_t required = (1u << iterations) - 1;
        for (unsigned bank = 0; bank < 4; ++bank) if ((written[bank] & required) != required) return false;
        reason = "Resource map disagrees with pixel texture program";
        if ((texture && textureProgram != 1) || (version == 6 && !texture && textureProgram != 0)) return false;
    }
    output = {payload, count, expectedStage, bindingCount == 0 ? nullptr : bindings, bindingCount,
              resourceCount == 0 ? nullptr : resources, resourceCount, version == 6};
    reason = nullptr;
    return true;
}
}
