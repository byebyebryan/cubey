#pragma once
#include "raytracing_config.h"
#include <cubey/core/math.h>
#include <cubey/engine/environment_transition.h>
#include <glm/gtc/constants.hpp>

namespace cubey::projects::raytracing {
// Opt-in eight-second clear library presentation, matching forward/SDF authoring.
// Called once for an output state; secondary sample iterations never advance it.
inline Config sample_workshop_presentation(Config c, double seconds) {
    if (c.presentation != "library")
        return c;
    c.presentation_time =
        std::clamp(c.presentation_time >= 0 ? c.presentation_time : seconds, 0.0, 8.0);
    c.yaw += glm::radians(c.orbit_degrees) * static_cast<float>(c.presentation_time / 8);
    if (c.lighting_cycle != "frozen") {
        const auto sample = sample_environment_transition(
            {.duration_seconds = 8,
             .sun_elevation_begin = c.lighting_cycle == "dawn" ? -1.2F : 14.F,
             .sun_elevation_end = c.lighting_cycle == "dawn" ? 14.F : -1.2F,
             .sun_azimuth = -155,
             .cloud_seconds_begin = 300,
             .cloud_time_scale = 30},
            c.presentation_time);
        c.sun_elevation = glm::radians(sample.sun_elevation);
        c.sun_azimuth = glm::pi<float>() - glm::radians(sample.sun_azimuth);
        c.exposure = sample.exposure;
    }
    return c;
}
} // namespace cubey::projects::raytracing
