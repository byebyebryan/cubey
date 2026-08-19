#pragma once

#include <cubey/core/config_schema.h>
#include <cubey/engine/atmosphere_environment_config.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cubey {

// Some environment consumers intentionally expose only the clock and manual
// sun controls.  Keeping that scope at the atmosphere boundary prevents a
// project facade from advertising fields its runtime never reads.
enum class AtmosphereEnvironmentSchemaMode {
    Full,
    CloudReference,
};

namespace atmosphere_environment_schema_detail {

using config::OptionSpec;
using config::ValueType;

inline OptionSpec option(std::string path, std::string cli, std::string label,
                         std::string help, ValueType type, config::Range range = {},
                         std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Atmosphere",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

inline void bind_string(config::Schema::Builder& builder, OptionSpec spec,
                        std::optional<std::string>& target) {
    builder.bind(std::move(spec), target);
}

inline void bind_float(config::Schema::Builder& builder, OptionSpec spec,
                       std::optional<float>& target) {
    builder.bind(std::move(spec), target);
}

inline void bind_full(config::Schema::Builder& builder, AtmosphereEnvironmentOptions& a) {
    bind_string(builder,
                option("atmosphere.time_of_day_mode", "--time-of-day-mode", "Time Mode",
                        "Manual sun angles or solar-clock mode.", ValueType::Enum, {},
                        {"manual", "solar"}),
                a.time_of_day_mode);
    bind_string(builder,
                option("atmosphere.night_sky_mode", "--night-sky-mode", "Night Sky Mode",
                        "Night-sky visibility model.", ValueType::Enum, {}, {"human", "camera"}),
                a.night_sky_mode);
    bind_string(builder,
                option("atmosphere.ground_mode", "--atmosphere-ground-mode", "Ground Mode",
                        "Atmosphere ground handling for sky-only backgrounds and captures.",
                        ValueType::Enum, {}, {"ground", "sky-only", "sky-only-no-ground-occlusion"}),
                a.ground_mode);

    const auto floating = [&builder](std::string path, std::string cli, std::string label,
                                     std::string help, config::Range range,
                                     std::optional<float>& target) {
        bind_float(builder,
                   option(std::move(path), std::move(cli), std::move(label), std::move(help),
                          ValueType::Float, range),
                   target);
    };
    floating("atmosphere.sun_elevation_degrees", "--sun-elevation", "Sun Elevation",
             "Manual sun elevation in degrees.",
             {.has_min = true, .has_max = true, .min = -90.0, .max = 90.0},
             a.sun_elevation_degrees);
    floating("atmosphere.sun_azimuth_degrees", "--sun-azimuth", "Sun Azimuth",
             "Manual sun azimuth in degrees.",
             {.has_min = true, .has_max = true, .min = -360.0, .max = 360.0},
             a.sun_azimuth_degrees);
    floating("atmosphere.camera_altitude_km", "--camera-altitude-km", "Camera Altitude",
             "Observer altitude above sea level.", {.has_min = true, .min = 0.0},
             a.camera_altitude_km);
    floating("atmosphere.camera_yaw_offset_degrees", "--camera-yaw-offset-deg",
             "Camera Yaw Offset",
             "Additional yaw offset from the default atmosphere review direction.",
             {.has_min = true, .has_max = true, .min = -360.0, .max = 360.0},
             a.camera_yaw_offset_degrees);
    floating("atmosphere.camera_pitch_offset_degrees", "--camera-pitch-offset-deg",
             "Camera Pitch Offset",
             "Additional pitch offset from the default atmosphere review direction.",
             {.has_min = true, .has_max = true, .min = -89.0, .max = 89.0},
             a.camera_pitch_offset_degrees);
    floating("atmosphere.rayleigh_scale", "--rayleigh-scale", "Rayleigh Scale",
             "Rayleigh molecular scattering density multiplier.", {.has_min = true, .min = 0.0},
             a.rayleigh_scale);
    floating("atmosphere.mie_scale", "--mie-scale", "Mie Scale",
             "Mie aerosol density multiplier.", {.has_min = true, .min = 0.0}, a.mie_scale);
    floating("atmosphere.ozone_scale", "--ozone-scale", "Ozone Scale",
             "Ozone absorption density multiplier.", {.has_min = true, .min = 0.0},
             a.ozone_scale);
    floating("atmosphere.time_hours", "--time-hours", "Time Hours",
             "Solar-clock time in hours.", {.has_min = true, .has_max = true, .min = 0.0,
                                             .max = 24.0},
             a.time_hours);
    floating("atmosphere.day_of_year", "--day-of-year", "Day Of Year",
             "Solar-clock day of year.", {.has_min = true, .has_max = true, .min = 1.0,
                                           .max = 366.0},
             a.day_of_year);
    floating("atmosphere.latitude_degrees", "--latitude-degrees", "Latitude",
             "Solar-clock observer latitude.",
             {.has_min = true, .has_max = true, .min = -90.0, .max = 90.0},
             a.latitude_degrees);
    floating("atmosphere.sun_azimuth_offset_degrees", "--sun-azimuth-offset",
             "Sun Azimuth Offset", "Offset applied to computed solar azimuth.",
             {.has_min = true, .has_max = true, .min = -360.0, .max = 360.0},
             a.sun_azimuth_offset_degrees);
    floating("atmosphere.time_speed_hours_per_second", "--time-speed-hours-per-second",
             "Time Speed", "Simulated hours advanced per real second.",
             {.has_min = true, .min = 0.0}, a.time_speed_hours_per_second);
    floating("atmosphere.exposure_bias", "--exposure-bias", "Exposure Bias",
             "Exposure bias applied to automatic exposure.",
             {.has_min = true, .has_max = true, .min = -4.0, .max = 4.0}, a.exposure_bias);
    floating("atmosphere.twilight_strength", "--twilight-strength", "Twilight",
             "Twilight brightness multiplier.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 4.0}, a.twilight_strength);
    floating("atmosphere.twilight_horizon_warmth", "--twilight-horizon-warmth", "Horizon Warmth",
             "Warm color bias near the twilight horizon.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 2.0},
             a.twilight_horizon_warmth);
    floating("atmosphere.star_intensity", "--star-intensity", "Stars",
             "Procedural star brightness.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 4.0}, a.star_intensity);
    floating("atmosphere.star_density", "--star-density", "Star Density",
             "Procedural star density.", {.has_min = true, .has_max = true, .min = 0.0,
                                           .max = 1.0},
             a.star_density);
    floating("atmosphere.milky_way_intensity", "--milky-way-intensity", "Milky Way",
             "Generated Milky Way brightness.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 4.0},
             a.milky_way_intensity);
    floating("atmosphere.milky_way_contrast", "--milky-way-contrast", "Milky Way Contrast",
             "Generated Milky Way contrast.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 4.0}, a.milky_way_contrast);
    floating("atmosphere.light_pollution", "--light-pollution", "Light Pollution",
             "Skyglow amount that suppresses night-sky features.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}, a.light_pollution);
    floating("atmosphere.moon_intensity", "--moon-intensity", "Moon",
             "Visible moon brightness.", {.has_min = true, .has_max = true, .min = 0.0,
                                           .max = 4.0},
             a.moon_intensity);
    floating("atmosphere.moonlight_intensity", "--moonlight-intensity", "Moonlight",
             "Moonlight contribution.", {.has_min = true, .has_max = true, .min = 0.0,
                                          .max = 4.0},
             a.moonlight_intensity);
    floating("atmosphere.moon_phase_offset_days", "--moon-phase-offset-days", "Moon Phase Offset",
             "Offset in days through the lunar phase cycle.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 29.530588},
             a.moon_phase_offset_days);
    floating("atmosphere.moon_size_scale", "--moon-size-scale", "Moon Size",
             "Visual moon scale.", {.has_min = true, .has_max = true, .min = 0.000001,
                                     .max = 8.0},
             a.moon_size_scale);

    OptionSpec pause = option("atmosphere.time_paused", "--pause-time", "Pause Time",
                              "Start atmosphere time paused.", ValueType::Bool);
    builder.bind(std::move(pause), a.time_paused);
    OptionSpec auto_exposure = option("atmosphere.auto_exposure", "--auto-exposure",
                                      "Auto Exposure",
                                      "Enable atmosphere-driven automatic exposure.",
                                      ValueType::Bool);
    auto_exposure.negative_cli_name = "--no-auto-exposure";
    builder.bind(std::move(auto_exposure), a.auto_exposure);
    OptionSpec moon = option("atmosphere.moon", "--moon", "Moon",
                             "Enable the visible moon and moonlight.", ValueType::Bool);
    moon.negative_cli_name = "--no-moon";
    builder.bind(std::move(moon), a.moon);
}

inline void bind_cloud_reference(config::Schema::Builder& builder,
                                 AtmosphereEnvironmentOptions& a) {
    bind_string(builder,
                option("atmosphere.time_of_day_mode", "--time-of-day-mode", "Time Mode",
                        "Manual or solar clock.", ValueType::Enum, {}, {"manual", "solar"}),
                a.time_of_day_mode);
    const auto floating = [&builder](std::string path, std::string cli, std::string label,
                                     std::string help, config::Range range,
                                     std::optional<float>& target) {
        bind_float(builder,
                   option(std::move(path), std::move(cli), std::move(label), std::move(help),
                          ValueType::Float, range),
                   target);
    };
    floating("atmosphere.time_hours", "--time-hours", "Time Hours", "Solar-clock time in hours.",
             {.has_min = true, .has_max = true, .min = 0.0, .max = 24.0}, a.time_hours);
    floating("atmosphere.day_of_year", "--day-of-year", "Day Of Year", "Solar-clock day of year.",
             {.has_min = true, .has_max = true, .min = 1.0, .max = 366.0}, a.day_of_year);
    floating("atmosphere.latitude_degrees", "--latitude-degrees", "Latitude",
             "Solar-clock observer latitude.",
             {.has_min = true, .has_max = true, .min = -90.0, .max = 90.0}, a.latitude_degrees);
    floating("atmosphere.sun_azimuth_offset_degrees", "--sun-azimuth-offset",
             "Sun Azimuth Offset", "Offset applied to computed solar azimuth.",
             {.has_min = true, .has_max = true, .min = -360.0, .max = 360.0},
             a.sun_azimuth_offset_degrees);
    floating("atmosphere.time_speed_hours_per_second", "--time-speed-hours-per-second",
             "Time Speed", "Simulated hours advanced per real second.",
             {.has_min = true, .min = 0.0}, a.time_speed_hours_per_second);
    floating("atmosphere.sun_elevation_degrees", "--sun-elevation", "Sun Elevation",
             "Manual sun elevation in degrees.",
             {.has_min = true, .has_max = true, .min = -90.0, .max = 90.0},
             a.sun_elevation_degrees);
    floating("atmosphere.sun_azimuth_degrees", "--sun-azimuth", "Sun Azimuth",
             "Manual sun azimuth in degrees.",
             {.has_min = true, .has_max = true, .min = -360.0, .max = 360.0},
             a.sun_azimuth_degrees);
    OptionSpec pause = option("atmosphere.time_paused", "--pause-time", "Pause Time",
                              "Start time paused.", ValueType::Bool);
    builder.bind(std::move(pause), a.time_paused);
}

} // namespace atmosphere_environment_schema_detail

[[nodiscard]] inline config::Schema atmosphere_environment_schema(
    AtmosphereEnvironmentOptions& options,
    AtmosphereEnvironmentSchemaMode mode = AtmosphereEnvironmentSchemaMode::Full) {
    auto builder = config::Schema::builder();
    if (mode == AtmosphereEnvironmentSchemaMode::CloudReference) {
        atmosphere_environment_schema_detail::bind_cloud_reference(builder, options);
    } else {
        atmosphere_environment_schema_detail::bind_full(builder, options);
    }
    return std::move(builder).build();
}

} // namespace cubey
