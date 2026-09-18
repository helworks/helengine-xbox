#include "platform/xbox/XboxBootHost.hpp"

#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
#include "platform/xbox/XboxDiagnostics.hpp"
#include "platform/xbox/XboxRuntimeHost.hpp"
#endif

int main() {
#if defined(HELENGINE_XBOX_HAS_GENERATED_CORE)
    // An authored image exists to run the authored scene. The diagnostic probe
    // is still compiled in and still reachable through a Diagnostic build, but
    // selecting it here would mean an image that carries a scene and ignores
    // it, which is what every authored build did before the runtime host
    // existed.
    helengine::xbox::RuntimeResult result;
    const int status = helengine::xbox::RunAuthoredScene(HELENGINE_XBOX_STOP_AFTER_FRAMES, result);
    if (status != 0) {
        helengine::xbox::Diagnostics::Log("authored run failed at %s: %s",
                                          helengine::xbox::RuntimeStageName(result.Stage),
                                          result.Reason);
    }
    return status;
#else
    helengine::xbox::XboxBootHost host;
    helengine::xbox::BootConfig config;
    config.Mode = static_cast<helengine::xbox::ProbeMode>(HELENGINE_XBOX_PROBE);
    config.StopAfterFrames = HELENGINE_XBOX_STOP_AFTER_FRAMES;
    return host.Run(config);
#endif
}
