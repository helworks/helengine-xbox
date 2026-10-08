#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::xbox {

// The package declares its material representation independently of the selected
// renderer profile. Payload/version validation remains with the chosen serializer.
enum class XboxMaterialFormat { Unsupported, Classic, Shader };

inline XboxMaterialFormat ClassifyMaterialHeader(const uint8_t* header, size_t length) {
    if (header == nullptr || length < 12 || header[0] != 'H' || header[1] != 'E' ||
        header[2] != 'L' || header[3] != 'E' || header[4] != 1 ||
        header[8] != 1 || header[9] != 0 || header[7] != 0 || header[11] != 0)
        return XboxMaterialFormat::Unsupported;
    if (header[6] == 1 && header[10] == 9) return XboxMaterialFormat::Classic;
    if (header[6] == 2 && header[10] == 1) return XboxMaterialFormat::Shader;
    return XboxMaterialFormat::Unsupported;
}

} // namespace helengine::xbox
