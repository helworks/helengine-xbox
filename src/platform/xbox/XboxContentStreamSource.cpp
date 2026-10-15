#include "XboxContentStreamSource.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)

#include "XboxContentPath.hpp"
#include "XboxDiagnostics.hpp"

#include <cstdint>
#include <vector>

#include "system/io/file-stream.hpp"

#include <windows.h>

namespace helengine::xbox {

XboxContentStreamSource::XboxContentStreamSource(const char* contentRootPath)
    : contentRootPath_(contentRootPath == nullptr ? "" : contentRootPath) {
}

::Stream* XboxContentStreamSource::fail(const char* reason, const char* detail) {
    lastFailure_ = reason;
    ++failureCount_;
    Diagnostics::Log("content: %s (%s)", reason, detail == nullptr ? "" : detail);
    return nullptr;
}

::Stream* XboxContentStreamSource::OpenRead(HeCppString assetPath) {
    std::string discPath;
    const char* reason = nullptr;
    if (!ResolveDiscContentPath(contentRootPath_.c_str(), assetPath.raw(), discPath, reason)) {
        return fail(reason, assetPath.c_str());
    }

    const HANDLE file = CreateFileA(discPath.c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return fail("asset not found on the disc", discPath.c_str());
    }

    DWORD highOrderSize = 0;
    const DWORD size = GetFileSize(file, &highOrderSize);
    if (size == INVALID_FILE_SIZE || highOrderSize != 0) {
        CloseHandle(file);
        return fail("asset length could not be read", discPath.c_str());
    }
    if (static_cast<std::size_t>(size) > MaxAssetBytes) {
        CloseHandle(file);
        return fail("asset exceeds the cooked payload limit", discPath.c_str());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    DWORD read = 0;
    // One call: cooked assets are small and the DVD is not a pipe, so a short
    // read here is a genuine fault rather than something to loop around.
    if (size != 0 && (!ReadFile(file, bytes.data(), size, &read, nullptr) || read != size)) {
        CloseHandle(file);
        return fail("asset read was short", discPath.c_str());
    }
    CloseHandle(file);

    ++servedCount_;
    // FileStream copies the buffer and owns the copy, so the vector can die here.
    return new ::FileStream(bytes.data(), bytes.size());
}

} // namespace helengine::xbox

#endif
