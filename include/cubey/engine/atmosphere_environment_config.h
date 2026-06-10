#pragma once

#include <cubey/core/run_config.h>
#include <cubey/render/atmosphere_environment.h>

namespace cubey {

inline constexpr float kAtmosphereEnvironmentDefaultTimeSpeedHoursPerSecond =
    render::kAtmosphereEnvironmentDefaultTimeSpeedHoursPerSecond;

struct AtmosphereEnvironmentRunDefaults {
    float sun_elevation_degrees = 60.0F;
    float sun_azimuth_degrees = 0.0F;
    render::AtmosphereEnvironmentGroundMode ground_mode =
        render::AtmosphereEnvironmentGroundMode::Ground;
    bool reference_geometry_enabled = true;
};

struct AtmosphereEnvironmentRunState {
    render::AtmosphereEnvironmentConfig environment{};
    bool solar_time_enabled = true;
    bool time_playing = true;
    float time_speed_hours_per_second = kAtmosphereEnvironmentDefaultTimeSpeedHoursPerSecond;
    bool auto_exposure_enabled = true;
    float exposure_bias = 0.0F;
    float resolved_exposure = 0.0F;
};

[[nodiscard]] bool
atmosphere_environment_run_config_uses_solar_time(const RunConfig::AtmosphereOptions& atmosphere);
[[nodiscard]] float
atmosphere_environment_run_config_time_speed(const RunConfig::AtmosphereOptions& atmosphere);
[[nodiscard]] bool
atmosphere_environment_run_config_time_playing(const RunConfig::AtmosphereOptions& atmosphere);
[[nodiscard]] AtmosphereEnvironmentRunState
atmosphere_environment_run_state_from_config(const RunConfig::AtmosphereOptions& atmosphere,
                                             const AtmosphereEnvironmentRunDefaults& defaults = {});
void apply_atmosphere_environment_look_options(
    render::AtmosphereEnvironmentConfig& environment,
    const RunConfig::AtmosphereOptions& atmosphere);
void atmosphere_environment_resolve_run_state(AtmosphereEnvironmentRunState& state);
[[nodiscard]] bool atmosphere_environment_advance_time(AtmosphereEnvironmentRunState& state,
                                                       double delta_seconds);

} // namespace cubey
