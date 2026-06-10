#pragma once

#include <cstdint>

namespace cubey {

struct AtmosphereEnvironmentRunState;
namespace render {
struct AtmosphereEnvironmentConfig;
}

namespace host {

struct AtmosphereEnvironmentUiConfig {
    const char* label = "Environment";
    bool default_open = false;
    std::uint32_t level = 0;
    const char* help =
        "Shared procedural atmosphere used by sky, lighting, reflection, and exposure.";
};

struct AtmosphereEnvironmentLookUiConfig {
    const char* label = "Sky Look";
    bool default_open = false;
    std::uint32_t level = 1;
    const char* help =
        "Atmosphere scattering and twilight color controls shared by environment consumers.";
};

[[nodiscard]] bool
draw_atmosphere_environment_look_controls(cubey::render::AtmosphereEnvironmentConfig& environment,
                                          AtmosphereEnvironmentLookUiConfig config = {});

[[nodiscard]] bool
draw_atmosphere_environment_controls(cubey::AtmosphereEnvironmentRunState& atmosphere,
                                     AtmosphereEnvironmentUiConfig config = {});

} // namespace host
} // namespace cubey
