#pragma once

#include <cstdint>

namespace cubey {

struct AtmosphereEnvironmentRunState;

namespace host {

struct AtmosphereEnvironmentUiConfig {
    const char* label = "Environment";
    bool default_open = false;
    std::uint32_t level = 0;
    const char* help =
        "Shared procedural atmosphere used by sky, lighting, reflection, and exposure.";
};

[[nodiscard]] bool
draw_atmosphere_environment_controls(cubey::AtmosphereEnvironmentRunState& atmosphere,
                                     AtmosphereEnvironmentUiConfig config = {});

} // namespace host
} // namespace cubey
