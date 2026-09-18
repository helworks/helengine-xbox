#include "XboxContentPath.hpp"

#include <cstddef>

namespace helengine::xbox {

const char* ValidateRelativeContentPath(const std::string& path) {
    if (path.empty()) {
        return "asset path is empty";
    }
    if (path.find('\0') != std::string::npos) {
        return "asset path contains an embedded NUL";
    }
    if (path.find(':') != std::string::npos) {
        return "asset path names a drive";
    }
    if (path.front() == '/' || path.front() == '\\') {
        return "asset path is rooted";
    }
    if (path.back() == '/' || path.back() == '\\') {
        return "asset path ends with a separator";
    }

    std::size_t segmentStart = 0;
    for (;;) {
        const std::size_t separator = path.find_first_of("/\\", segmentStart);
        const std::size_t segmentEnd = separator == std::string::npos ? path.size() : separator;
        if (segmentEnd == segmentStart) {
            return "asset path contains an empty segment";
        }
        const std::string segment = path.substr(segmentStart, segmentEnd - segmentStart);
        if (segment == "." || segment == "..") {
            return "asset path contains a dot segment";
        }
        if (separator == std::string::npos) {
            return nullptr;
        }
        segmentStart = separator + 1;
    }
}

bool ResolveDiscContentPath(const char* contentRootPath,
                            const std::string& assetPath,
                            std::string& resolved,
                            const char*& reason) {
    if (contentRootPath == nullptr || *contentRootPath == '\0') {
        reason = "content root is empty";
        return false;
    }
    const char* rejection = ValidateRelativeContentPath(assetPath);
    if (rejection != nullptr) {
        reason = rejection;
        return false;
    }

    std::string result = contentRootPath;
    if (result.back() != '\\' && result.back() != '/') {
        result.push_back('\\');
    }
    result.append(assetPath);
    // The manifest is forward-slashed; the Xbox file system is not.
    for (char& character : result) {
        if (character == '/') {
            character = '\\';
        }
    }
    resolved = result;
    reason = nullptr;
    return true;
}

} // namespace helengine::xbox
