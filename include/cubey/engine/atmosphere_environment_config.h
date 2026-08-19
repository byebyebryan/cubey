#pragma once

#include <cubey/render/atmosphere_environment.h>

#include <optional>
#include <string>

namespace cubey {

// Typed startup inputs consumed by the shared atmosphere environment runtime.
// Project facades own their public schema and may expose only a subset.
struct AtmosphereEnvironmentOptions {
    std::optional<std::string> time_of_day_mode{};
    std::optional<std::string> night_sky_mode{};
    std::optional<std::string> ground_mode{};
    std::optional<float> sun_elevation_degrees{};
    std::optional<float> sun_azimuth_degrees{};
    std::optional<float> camera_altitude_km{};
    std::optional<float> camera_yaw_offset_degrees{};
    std::optional<float> camera_pitch_offset_degrees{};
    std::optional<float> rayleigh_scale{};
    std::optional<float> mie_scale{};
    std::optional<float> ozone_scale{};
    std::optional<float> time_hours{};
    std::optional<float> day_of_year{};
    std::optional<float> latitude_degrees{};
    std::optional<float> sun_azimuth_offset_degrees{};
    std::optional<float> time_speed_hours_per_second{};
    std::optional<float> exposure_bias{};
    std::optional<float> twilight_strength{};
    std::optional<float> twilight_horizon_warmth{};
    std::optional<float> star_intensity{};
    std::optional<float> star_density{};
    std::optional<float> milky_way_intensity{};
    std::optional<float> milky_way_contrast{};
    std::optional<float> light_pollution{};
    std::optional<float> moon_intensity{};
    std::optional<float> moonlight_intensity{};
    std::optional<float> moon_phase_offset_days{};
    std::optional<float> moon_size_scale{};
    std::optional<bool> time_paused{};
    std::optional<bool> auto_exposure{};
    std::optional<bool> moon{};
};

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
atmosphere_environment_options_use_solar_time(const AtmosphereEnvironmentOptions& atmosphere);
void validate_atmosphere_environment_options(const AtmosphereEnvironmentOptions& atmosphere);
[[nodiscard]] float
atmosphere_environment_options_time_speed(const AtmosphereEnvironmentOptions& atmosphere);
[[nodiscard]] bool
atmosphere_environment_options_time_playing(const AtmosphereEnvironmentOptions& atmosphere);
[[nodiscard]] AtmosphereEnvironmentRunState
atmosphere_environment_run_state_from_config(const AtmosphereEnvironmentOptions& atmosphere,
                                             const AtmosphereEnvironmentRunDefaults& defaults = {});
void apply_atmosphere_environment_look_options(
    render::AtmosphereEnvironmentConfig& environment,
    const AtmosphereEnvironmentOptions& atmosphere);
void atmosphere_environment_resolve_run_state(AtmosphereEnvironmentRunState& state);
[[nodiscard]] bool atmosphere_environment_advance_time(AtmosphereEnvironmentRunState& state,
                                                       double delta_seconds);

} // namespace cubey
