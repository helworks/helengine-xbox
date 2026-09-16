#pragma once

// Bounded diagnostic output for the native Xbox probe.
//
// Every line goes to two sinks that the pinned nxdk provides:
//  - hal/debug.h debugPrint: the framebuffer text console (visible while the
//    debug screen is shown, i.e. before video-ready and after shutdown);
//  - xboxkrnl DbgPrint: the kernel debug channel (visible to emulator/debugger
//    hosts that capture it).
// The last line is also retained so the host can show it on the pbkit text
// overlay. Nothing here allocates.

#include <cstdarg>
#include <cstdio>

#include <hal/debug.h>
#include <xboxkrnl/xboxkrnl.h>

namespace helengine::xbox {

class Diagnostics {
public:
    static constexpr unsigned LineCapacity = 96;

    static void Log(const char* format, ...) __attribute__((format(printf, 1, 2))) {
        char line[LineCapacity];
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        debugPrint("%s\n", line);
        DbgPrint("helengine-xbox: %s\n", line);
        std::snprintf(LastLine(), LineCapacity, "%s", line);
    }

    static const char* Last() { return LastLine(); }

private:
    static char* LastLine() {
        static char storage[LineCapacity] = "";
        return storage;
    }
};

} // namespace helengine::xbox
