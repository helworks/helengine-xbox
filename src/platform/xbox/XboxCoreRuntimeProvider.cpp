#include "XboxCoreRuntimeProvider.hpp"

#include "XboxDiagnostics.hpp"

#include <cstddef>

#include <pbkit/pbkit.h>
#include <windows.h>

namespace helengine::xbox {
namespace {

// Held in file-scope storage rather than on the failing thread's stack so an
// emulator memory probe or a debugger can still read the reason after the
// console has stopped. Both symbols keep external linkage for the same reason.
constexpr std::size_t kFailureMessageCapacity = 256;

char g_lastCoreFailureMessage[kFailureMessageCapacity] = "";
volatile unsigned g_coreFailureCount = 0;

// Copies without <cstring> so this stays usable from a runtime header context
// that has not pulled the C string functions in.
void CopyBounded(char* destination, const char* source, std::size_t capacity) {
    if (capacity == 0) {
        return;
    }
    std::size_t index = 0;
    if (source != nullptr) {
        while (index + 1 < capacity && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

} // namespace

const char* LastCoreFailureMessage() {
    return g_lastCoreFailureMessage;
}

unsigned CoreFailureCount() {
    return g_coreFailureCount;
}

} // namespace helengine::xbox

namespace he_cpp_custom {

void Fail(const char* message) {
    using helengine::xbox::Diagnostics;

    // Record before reporting: if anything below faults, the message is
    // already where a post-mortem will look for it.
    helengine::xbox::CopyBounded(helengine::xbox::g_lastCoreFailureMessage,
                                 message,
                                 helengine::xbox::kFailureMessageCapacity);
    helengine::xbox::g_coreFailureCount = helengine::xbox::g_coreFailureCount + 1u;

    // Leave the 3D pipeline and show the text console, so the message is
    // readable on a plain TV with no debugger attached. pbkit tolerates this
    // from an arbitrary point in a frame; the frame in flight is abandoned,
    // which is the intent, because Core has already given up on it.
    pb_show_debug_screen();
    Diagnostics::Log("core fatal: %s",
                     message == nullptr ? "(no message)" : message);
    Diagnostics::Log("core fatal: halted, failures=%u",
                     helengine::xbox::CoreFailureCount());

    // Halt rather than return to the dashboard. A Core failure during bring-up
    // is something to read off the screen, and nxdk's runtime would otherwise
    // reboot the console and take the message with it.
    for (;;) {
        Sleep(1000);
    }
}

} // namespace he_cpp_custom
