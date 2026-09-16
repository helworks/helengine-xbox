#include "platform/xbox/XboxBootHost.hpp"

int main() {
    helengine::xbox::XboxBootHost host;
    helengine::xbox::BootConfig config;
    config.Mode = static_cast<helengine::xbox::ProbeMode>(HELENGINE_XBOX_PROBE);
    config.StopAfterFrames = HELENGINE_XBOX_STOP_AFTER_FRAMES;
    return host.Run(config);
}
