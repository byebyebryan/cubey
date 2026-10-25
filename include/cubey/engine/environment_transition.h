#pragma once

#include <algorithm>
#include <cmath>
#include <cubey/render/atmosphere_environment.h>
#include <stdexcept>

namespace cubey {
struct EnvironmentTransition {
    double duration_seconds = 1;
    float sun_elevation_begin = 0, sun_elevation_end = 0, sun_azimuth = 0;
    double cloud_seconds_begin = 0, cloud_time_scale = 1;
    float exposure_bias = 0;
};
struct EnvironmentTransitionSample {
    double elapsed_seconds = 0, cloud_seconds = 0;
    float sun_elevation = 0, sun_azimuth = 0, exposure = 0;
};
inline EnvironmentTransitionSample sample_environment_transition(const EnvironmentTransition& c,
                                                                 double seconds) {
    if (!std::isfinite(seconds) || !std::isfinite(c.duration_seconds) || c.duration_seconds <= 0 ||
        !std::isfinite(c.sun_elevation_begin) || !std::isfinite(c.sun_elevation_end) ||
        !std::isfinite(c.sun_azimuth) || !std::isfinite(c.cloud_seconds_begin) ||
        !std::isfinite(c.cloud_time_scale) || !std::isfinite(c.exposure_bias) ||
        c.cloud_seconds_begin < 0 || c.cloud_time_scale < 0)
        throw std::invalid_argument("invalid environment transition");
    const double t = std::clamp(seconds, 0.0, c.duration_seconds);
    const float u = static_cast<float>(t / c.duration_seconds);
    const float elevation =
        std::lerp(c.sun_elevation_begin, c.sun_elevation_end, u * u * (3 - 2 * u));
    return {.elapsed_seconds = t,
            .cloud_seconds = c.cloud_seconds_begin + t * c.cloud_time_scale,
            .sun_elevation = elevation,
            .sun_azimuth = c.sun_azimuth,
            .exposure = render::atmosphere_environment_auto_exposure(elevation, c.exposure_bias)};
}
} // namespace cubey
