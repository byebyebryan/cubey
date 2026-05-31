#pragma once

#include "night_sky_atlas.h"

#include <cubey/core/math.h>
#include <cubey/core/run_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::atmosphere {

enum class AtmospherePreset : std::uint32_t {
    Noon = 0,
    LowSun = 1,
    Sunset = 2,
    Hazy = 3,
    ThinAir = 4,
    HighAltitude = 5,
    Night = 6,
    MoonlitNight = 7,
};

inline constexpr std::array<AtmospherePreset, 8> kAtmospherePresets{
    AtmospherePreset::Noon,  AtmospherePreset::LowSun,       AtmospherePreset::Sunset,
    AtmospherePreset::Hazy,  AtmospherePreset::ThinAir,      AtmospherePreset::HighAltitude,
    AtmospherePreset::Night, AtmospherePreset::MoonlitNight,
};

enum class AtmosphereRenderView : std::uint32_t {
    Final = 0,
    Rayleigh = 1,
    Mie = 2,
    Transmittance = 3,
    OpticalDepth = 4,
    SunDisk = 5,
    AerialPerspective = 6,
    NightSky = 7,
    MilkyWay = 8,
    Moon = 9,
    MoonSurface = 10,
};

inline constexpr std::array<AtmosphereRenderView, 11> kAtmosphereRenderViews{
    AtmosphereRenderView::Final,
    AtmosphereRenderView::Rayleigh,
    AtmosphereRenderView::Mie,
    AtmosphereRenderView::Transmittance,
    AtmosphereRenderView::OpticalDepth,
    AtmosphereRenderView::SunDisk,
    AtmosphereRenderView::AerialPerspective,
    AtmosphereRenderView::NightSky,
    AtmosphereRenderView::MilkyWay,
    AtmosphereRenderView::Moon,
    AtmosphereRenderView::MoonSurface,
};

enum class SunControlMode : std::uint32_t {
    ManualSun = 0,
    SolarClock = 1,
};

inline constexpr std::array<SunControlMode, 2> kSunControlModes{
    SunControlMode::ManualSun,
    SunControlMode::SolarClock,
};

enum class NightSkyVisualMode : std::uint32_t {
    HumanEye = 0,
    Camera = 1,
};

inline constexpr std::array<NightSkyVisualMode, 2> kNightSkyVisualModes{
    NightSkyVisualMode::HumanEye,
    NightSkyVisualMode::Camera,
};

struct TimeOfDayConfig {
    SunControlMode mode = SunControlMode::SolarClock;
    float time_hours = 12.0F;
    float day_of_year = 80.0F;
    float latitude_degrees = 30.0F;
    float azimuth_offset_degrees = 0.0F;
    bool playing = true;
    float speed_hours_per_second = 0.05F;
    bool auto_exposure_enabled = true;
    float exposure_bias = 0.0F;
};

struct NightSkyConfig {
    NightSkyVisualMode visual_mode = NightSkyVisualMode::HumanEye;
    NightSkyLayerView layer = NightSkyLayerView::Final;
    float twilight_strength = 1.0F;
    float twilight_horizon_warmth = 1.0F;
    float star_intensity = 1.0F;
    float star_density = 0.65F;
    float milky_way_intensity = 0.75F;
    float milky_way_contrast = 1.0F;
    float light_pollution = 0.0F;
    float procedural_variation = 0.0F;
};

struct MoonConfig {
    bool enabled = true;
    float disk_intensity = 1.0F;
    float moonlight_intensity = 1.0F;
    float phase_offset_days = 14.765F;
    float angular_radius_scale = 1.0F;
};

struct AtmosphereConfig {
    AtmospherePreset preset = AtmospherePreset::Noon;
    AtmosphereRenderView render_view = AtmosphereRenderView::Final;
    TimeOfDayConfig time_of_day{};
    NightSkyConfig night_sky{};
    MoonConfig moon{};

    float bottom_radius_km = 6371.0F;
    float top_radius_km = 6471.0F;
    cubey::math::Vec3 rayleigh_scattering{0.005802F, 0.013558F, 0.033100F};
    float rayleigh_scale_height_km = 8.0F;
    float rayleigh_density_scale = 1.0F;

    float mie_scattering = 0.003996F;
    float mie_extinction = 0.004400F;
    float mie_scale_height_km = 1.2F;
    float mie_anisotropy = 0.80F;
    float mie_density_scale = 1.0F;

    cubey::math::Vec3 ozone_absorption{0.000650F, 0.001881F, 0.000085F};
    float ozone_center_altitude_km = 25.0F;
    float ozone_half_width_km = 15.0F;

    float ground_albedo = 0.10F;
    float sun_angular_radius = 0.004675F;
    float sun_elevation_degrees = 60.0F;
    float sun_azimuth_degrees = 0.0F;
    float camera_altitude_km = 0.15F;
    float exposure = 0.0F;
    bool reference_geometry_enabled = true;
    float reference_grid_km = 1.0F;
    float reference_intensity = 0.72F;
};

struct SolarPosition {
    float elevation_degrees = 0.0F;
    float azimuth_degrees = 0.0F;
};

struct LunarState {
    cubey::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    float phase_fraction = 0.5F;
    float illumination = 1.0F;
    float angular_radius = 0.00452F;
};

[[nodiscard]] inline const char* atmosphere_preset_name(AtmospherePreset preset) {
    switch (preset) {
    case AtmospherePreset::Noon:
        return "noon";
    case AtmospherePreset::LowSun:
        return "low-sun";
    case AtmospherePreset::Sunset:
        return "sunset";
    case AtmospherePreset::Hazy:
        return "hazy";
    case AtmospherePreset::ThinAir:
        return "thin-air";
    case AtmospherePreset::HighAltitude:
        return "high-altitude";
    case AtmospherePreset::Night:
        return "night";
    case AtmospherePreset::MoonlitNight:
        return "moonlit-night";
    }
    return "noon";
}

[[nodiscard]] inline AtmospherePreset atmosphere_preset_from_name(std::string_view name) {
    if (name.empty()) {
        return AtmospherePreset::Noon;
    }
    for (const AtmospherePreset preset : kAtmospherePresets) {
        if (name == atmosphere_preset_name(preset)) {
            return preset;
        }
    }
    throw std::runtime_error("unknown atmosphere preset: " + std::string(name));
}

[[nodiscard]] inline const char* atmosphere_render_view_name(AtmosphereRenderView view) {
    switch (view) {
    case AtmosphereRenderView::Final:
        return "final";
    case AtmosphereRenderView::Rayleigh:
        return "rayleigh";
    case AtmosphereRenderView::Mie:
        return "mie";
    case AtmosphereRenderView::Transmittance:
        return "transmittance";
    case AtmosphereRenderView::OpticalDepth:
        return "optical-depth";
    case AtmosphereRenderView::SunDisk:
        return "sun-disk";
    case AtmosphereRenderView::AerialPerspective:
        return "aerial-perspective";
    case AtmosphereRenderView::NightSky:
        return "night-sky";
    case AtmosphereRenderView::MilkyWay:
        return "milky-way";
    case AtmosphereRenderView::Moon:
        return "moon";
    case AtmosphereRenderView::MoonSurface:
        return "moon-surface";
    }
    return "final";
}

[[nodiscard]] inline AtmosphereRenderView atmosphere_render_view_from_name(std::string_view name) {
    if (name.empty()) {
        return AtmosphereRenderView::Final;
    }
    for (const AtmosphereRenderView view : kAtmosphereRenderViews) {
        if (name == atmosphere_render_view_name(view)) {
            return view;
        }
    }
    throw std::runtime_error("unknown atmosphere render view: " + std::string(name));
}

[[nodiscard]] inline const char* night_sky_visual_mode_name(NightSkyVisualMode mode) {
    switch (mode) {
    case NightSkyVisualMode::HumanEye:
        return "human";
    case NightSkyVisualMode::Camera:
        return "camera";
    }
    return "human";
}

[[nodiscard]] inline NightSkyVisualMode night_sky_visual_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return NightSkyVisualMode::HumanEye;
    }
    for (const NightSkyVisualMode mode : kNightSkyVisualModes) {
        if (name == night_sky_visual_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown night sky visual mode: " + std::string(name));
}

[[nodiscard]] inline const char* night_sky_layer_view_name(NightSkyLayerView layer) {
    switch (layer) {
    case NightSkyLayerView::Final:
        return "final";
    case NightSkyLayerView::StellarEmission:
        return "stellar-emission";
    case NightSkyLayerView::DustTau:
        return "dust-tau";
    case NightSkyLayerView::StarClouds:
        return "star-clouds";
    case NightSkyLayerView::HiiEmission:
        return "hii-emission";
    case NightSkyLayerView::Speckles:
        return "speckles";
    }
    return "final";
}

[[nodiscard]] inline NightSkyLayerView night_sky_layer_view_from_name(std::string_view name) {
    if (name.empty()) {
        return NightSkyLayerView::Final;
    }
    for (const NightSkyLayerView layer : kNightSkyLayerViews) {
        if (name == night_sky_layer_view_name(layer)) {
            return layer;
        }
    }
    throw std::runtime_error("unknown Milky Way layer: " + std::string(name));
}

[[nodiscard]] inline AtmosphereRenderView next_atmosphere_render_view(AtmosphereRenderView view) {
    for (std::size_t index = 0; index < kAtmosphereRenderViews.size(); ++index) {
        if (kAtmosphereRenderViews[index] == view) {
            return kAtmosphereRenderViews[(index + 1U) % kAtmosphereRenderViews.size()];
        }
    }
    return AtmosphereRenderView::Final;
}

[[nodiscard]] inline const char* sun_control_mode_name(SunControlMode mode) {
    switch (mode) {
    case SunControlMode::ManualSun:
        return "manual";
    case SunControlMode::SolarClock:
        return "solar";
    }
    return "solar";
}

[[nodiscard]] inline SunControlMode sun_control_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return SunControlMode::SolarClock;
    }
    for (const SunControlMode mode : kSunControlModes) {
        if (name == sun_control_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere time-of-day mode: " + std::string(name));
}

[[nodiscard]] inline float atmosphere_degrees_to_radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

[[nodiscard]] inline float atmosphere_radians_to_degrees(float radians) {
    return radians * (180.0F / std::numbers::pi_v<float>);
}

[[nodiscard]] inline float atmosphere_wrap_time_hours(float time_hours) {
    float wrapped = std::fmod(time_hours, 24.0F);
    if (wrapped < 0.0F) {
        wrapped += 24.0F;
    }
    return wrapped;
}

[[nodiscard]] inline float atmosphere_wrap_signed_degrees(float degrees) {
    float wrapped = std::fmod(degrees + 180.0F, 360.0F);
    if (wrapped < 0.0F) {
        wrapped += 360.0F;
    }
    return wrapped - 180.0F;
}

[[nodiscard]] inline float atmosphere_advance_day_of_year(float day_of_year, int day_delta) {
    float wrapped = std::fmod(day_of_year - 1.0F + static_cast<float>(day_delta), 366.0F);
    if (wrapped < 0.0F) {
        wrapped += 366.0F;
    }
    return wrapped + 1.0F;
}

[[nodiscard]] inline float atmosphere_smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] inline float atmosphere_wrap_unit(float value) {
    float wrapped = std::fmod(value, 1.0F);
    if (wrapped < 0.0F) {
        wrapped += 1.0F;
    }
    return wrapped;
}

[[nodiscard]] inline SolarPosition atmosphere_solar_position(const TimeOfDayConfig& time_of_day) {
    const float day_angle =
        atmosphere_degrees_to_radians((360.0F / 365.0F) * (time_of_day.day_of_year - 80.0F));
    const float declination = atmosphere_degrees_to_radians(23.44F) * std::sin(day_angle);
    const float latitude = atmosphere_degrees_to_radians(time_of_day.latitude_degrees);
    const float hour_angle = atmosphere_degrees_to_radians(
        15.0F * (atmosphere_wrap_time_hours(time_of_day.time_hours) - 12.0F));

    const float sin_elevation = std::sin(latitude) * std::sin(declination) +
                                std::cos(latitude) * std::cos(declination) * std::cos(hour_angle);
    const float elevation = std::asin(std::clamp(sin_elevation, -1.0F, 1.0F));
    const float east = -std::cos(declination) * std::sin(hour_angle);
    const float south = -std::cos(latitude) * std::sin(declination) +
                        std::sin(latitude) * std::cos(declination) * std::cos(hour_angle);
    const float azimuth =
        atmosphere_radians_to_degrees(std::atan2(east, south)) + time_of_day.azimuth_offset_degrees;

    return {
        .elevation_degrees = atmosphere_radians_to_degrees(elevation),
        .azimuth_degrees = atmosphere_wrap_signed_degrees(azimuth),
    };
}

[[nodiscard]] inline cubey::math::Vec3 atmosphere_direction_from_alt_az(float elevation_degrees,
                                                                        float azimuth_degrees) {
    const float elevation = atmosphere_degrees_to_radians(elevation_degrees);
    const float azimuth = atmosphere_degrees_to_radians(azimuth_degrees);
    const float horizontal = std::cos(elevation);
    return {
        horizontal * std::sin(azimuth),
        std::sin(elevation),
        -horizontal * std::cos(azimuth),
    };
}

[[nodiscard]] inline LunarState atmosphere_lunar_state(const TimeOfDayConfig& time_of_day,
                                                       const MoonConfig& moon) {
    constexpr float kLunarCycleDays = 29.530588F;
    constexpr float kTropicalYearDays = 365.2422F;
    constexpr float kMeanMoonAngularRadius = 0.00452F;
    const float lunar_age_days = std::fmod(
        time_of_day.day_of_year - 80.0F + time_of_day.time_hours / 24.0F + moon.phase_offset_days,
        kLunarCycleDays);
    const float phase_fraction = atmosphere_wrap_unit(lunar_age_days / kLunarCycleDays);
    const float phase_angle = phase_fraction * 2.0F * std::numbers::pi_v<float>;
    const float illumination = 0.5F - 0.5F * std::cos(phase_angle);

    TimeOfDayConfig moon_clock = time_of_day;
    moon_clock.time_hours =
        atmosphere_wrap_time_hours(time_of_day.time_hours - phase_fraction * 24.0F);
    moon_clock.day_of_year = atmosphere_advance_day_of_year(
        time_of_day.day_of_year, static_cast<int>(std::floor(phase_fraction * kTropicalYearDays)));
    const SolarPosition moon_position = atmosphere_solar_position(moon_clock);
    return {
        .direction = atmosphere_direction_from_alt_az(moon_position.elevation_degrees,
                                                      moon_position.azimuth_degrees),
        .phase_fraction = phase_fraction,
        .illumination = illumination,
        .angular_radius = kMeanMoonAngularRadius * moon.angular_radius_scale,
    };
}

[[nodiscard]] inline float atmosphere_sidereal_angle_radians(const TimeOfDayConfig& time_of_day) {
    constexpr float kSiderealSolarRatio = 1.00273790935F;
    constexpr float kTropicalYearDays = 365.2422F;
    float rotations =
        (atmosphere_wrap_time_hours(time_of_day.time_hours) / 24.0F) * kSiderealSolarRatio +
        (time_of_day.day_of_year - 80.0F) / kTropicalYearDays;
    rotations = rotations - std::floor(rotations);
    if (rotations < 0.0F) {
        rotations += 1.0F;
    }
    return rotations * 2.0F * std::numbers::pi_v<float>;
}

[[nodiscard]] inline float atmosphere_auto_exposure(float sun_elevation_degrees,
                                                    float exposure_bias) {
    const float daylight = atmosphere_smoothstep(-6.0F, 20.0F, sun_elevation_degrees);
    const float full_night = 1.0F - atmosphere_smoothstep(-18.0F, -6.0F, sun_elevation_degrees);
    return std::clamp(exposure_bias + 2.2F * (1.0F - daylight) + 0.6F * full_night, -4.0F, 4.0F);
}

inline void resolve_atmosphere_time_of_day(AtmosphereConfig& config) {
    if (config.time_of_day.mode == SunControlMode::SolarClock) {
        const SolarPosition solar_position = atmosphere_solar_position(config.time_of_day);
        config.sun_elevation_degrees = solar_position.elevation_degrees;
        config.sun_azimuth_degrees = solar_position.azimuth_degrees;
    }
    if (config.time_of_day.auto_exposure_enabled) {
        config.exposure = atmosphere_auto_exposure(config.sun_elevation_degrees,
                                                   config.time_of_day.exposure_bias);
    }
}

inline void set_atmosphere_time_from_elapsed(TimeOfDayConfig& time_of_day, float base_time_hours,
                                             float base_day_of_year, double elapsed_seconds) {
    const float elapsed_hours =
        std::max(static_cast<float>(elapsed_seconds), 0.0F) * time_of_day.speed_hours_per_second;
    const float total_hours = base_time_hours + elapsed_hours;
    const int day_delta = static_cast<int>(std::floor(total_hours / 24.0F));
    time_of_day.time_hours = atmosphere_wrap_time_hours(total_hours);
    time_of_day.day_of_year = atmosphere_advance_day_of_year(base_day_of_year, day_delta);
}

inline void advance_atmosphere_time_of_day(AtmosphereConfig& config, double delta_seconds) {
    if (config.time_of_day.mode != SunControlMode::SolarClock || !config.time_of_day.playing ||
        delta_seconds <= 0.0) {
        return;
    }
    set_atmosphere_time_from_elapsed(config.time_of_day, config.time_of_day.time_hours,
                                     config.time_of_day.day_of_year, delta_seconds);
}

[[nodiscard]] inline AtmosphereConfig atmosphere_config_for_preset(AtmospherePreset preset) {
    AtmosphereConfig config;
    config.preset = preset;
    switch (preset) {
    case AtmospherePreset::Noon:
        config.time_of_day.time_hours = 12.0F;
        config.sun_elevation_degrees = 60.0F;
        config.sun_azimuth_degrees = 0.0F;
        config.camera_altitude_km = 0.15F;
        config.exposure = 0.0F;
        break;
    case AtmospherePreset::LowSun:
        config.time_of_day.time_hours = 17.1F;
        config.sun_elevation_degrees = 12.0F;
        config.sun_azimuth_degrees = -32.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 1.15F;
        break;
    case AtmospherePreset::Sunset:
        config.time_of_day.time_hours = 17.8F;
        config.sun_elevation_degrees = 2.0F;
        config.sun_azimuth_degrees = -48.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 1.45F;
        break;
    case AtmospherePreset::Hazy:
        config.time_of_day.time_hours = 16.6F;
        config.sun_elevation_degrees = 18.0F;
        config.sun_azimuth_degrees = -24.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 3.50F;
        break;
    case AtmospherePreset::ThinAir:
        config.time_of_day.time_hours = 13.9F;
        config.sun_elevation_degrees = 50.0F;
        config.sun_azimuth_degrees = 15.0F;
        config.camera_altitude_km = 0.15F;
        config.rayleigh_density_scale = 0.45F;
        config.mie_density_scale = 0.25F;
        break;
    case AtmospherePreset::HighAltitude:
        config.time_of_day.time_hours = 17.3F;
        config.sun_elevation_degrees = 10.0F;
        config.sun_azimuth_degrees = -58.0F;
        config.camera_altitude_km = 25.0F;
        config.mie_density_scale = 0.35F;
        break;
    case AtmospherePreset::Night:
        config.time_of_day.time_hours = 0.0F;
        config.sun_elevation_degrees = -60.0F;
        config.sun_azimuth_degrees = 180.0F;
        config.camera_altitude_km = 0.15F;
        config.exposure = 2.8F;
        config.night_sky.star_intensity = 1.25F;
        config.night_sky.star_density = 0.72F;
        config.night_sky.milky_way_intensity = 0.90F;
        config.night_sky.milky_way_contrast = 1.10F;
        break;
    case AtmospherePreset::MoonlitNight:
        config.time_of_day.time_hours = 1.75F;
        config.time_of_day.day_of_year = 23.0F;
        config.time_of_day.latitude_degrees = 78.0F;
        config.time_of_day.exposure_bias = -0.40F;
        config.camera_altitude_km = 0.15F;
        config.night_sky.star_intensity = 0.90F;
        config.night_sky.star_density = 0.58F;
        config.night_sky.milky_way_intensity = 0.55F;
        config.night_sky.milky_way_contrast = 0.90F;
        config.moon.disk_intensity = 1.25F;
        config.moon.moonlight_intensity = 1.35F;
        config.moon.angular_radius_scale = 3.0F;
        break;
    }
    resolve_atmosphere_time_of_day(config);
    return config;
}

[[nodiscard]] inline bool atmosphere_vec3_nonnegative(cubey::math::Vec3 value) {
    return value.x >= 0.0F && value.y >= 0.0F && value.z >= 0.0F;
}

inline void validate_atmosphere_config(const AtmosphereConfig& config) {
    const auto require_finite = [](float value, const char* name) {
        if (!std::isfinite(value)) {
            throw std::runtime_error(std::string(name) + " must be finite");
        }
    };
    require_finite(config.bottom_radius_km, "atmosphere bottom radius");
    require_finite(config.top_radius_km, "atmosphere top radius");
    require_finite(config.rayleigh_scale_height_km, "atmosphere Rayleigh scale height");
    require_finite(config.mie_scale_height_km, "atmosphere Mie scale height");
    require_finite(config.mie_anisotropy, "atmosphere Mie anisotropy");
    require_finite(config.ozone_half_width_km, "atmosphere ozone half width");
    require_finite(config.sun_angular_radius, "atmosphere sun angular radius");
    require_finite(config.sun_elevation_degrees, "atmosphere sun elevation");
    require_finite(config.sun_azimuth_degrees, "atmosphere sun azimuth");
    require_finite(config.camera_altitude_km, "atmosphere camera altitude");
    require_finite(config.exposure, "atmosphere exposure");
    require_finite(config.reference_grid_km, "atmosphere reference grid scale");
    require_finite(config.reference_intensity, "atmosphere reference intensity");
    require_finite(config.night_sky.twilight_strength, "atmosphere twilight strength");
    require_finite(config.night_sky.twilight_horizon_warmth, "atmosphere twilight horizon warmth");
    require_finite(config.night_sky.star_intensity, "atmosphere star intensity");
    require_finite(config.night_sky.star_density, "atmosphere star density");
    require_finite(config.night_sky.milky_way_intensity, "atmosphere Milky Way intensity");
    require_finite(config.night_sky.milky_way_contrast, "atmosphere Milky Way contrast");
    require_finite(config.night_sky.light_pollution, "atmosphere light pollution");
    require_finite(config.night_sky.procedural_variation, "atmosphere Milky Way variation");
    require_finite(config.moon.disk_intensity, "atmosphere moon intensity");
    require_finite(config.moon.moonlight_intensity, "atmosphere moonlight intensity");
    require_finite(config.moon.phase_offset_days, "atmosphere moon phase offset");
    require_finite(config.moon.angular_radius_scale, "atmosphere moon size scale");
    require_finite(config.time_of_day.time_hours, "atmosphere time hours");
    require_finite(config.time_of_day.day_of_year, "atmosphere day of year");
    require_finite(config.time_of_day.latitude_degrees, "atmosphere latitude");
    require_finite(config.time_of_day.azimuth_offset_degrees, "atmosphere sun azimuth offset");
    require_finite(config.time_of_day.speed_hours_per_second, "atmosphere time speed");
    require_finite(config.time_of_day.exposure_bias, "atmosphere exposure bias");

    if (config.bottom_radius_km <= 0.0F || config.top_radius_km <= config.bottom_radius_km) {
        throw std::runtime_error("atmosphere radii must be positive and ordered bottom < top");
    }
    if (!atmosphere_vec3_nonnegative(config.rayleigh_scattering) ||
        !atmosphere_vec3_nonnegative(config.ozone_absorption)) {
        throw std::runtime_error("atmosphere coefficients must be nonnegative");
    }
    if (config.rayleigh_scale_height_km <= 0.0F || config.mie_scale_height_km <= 0.0F ||
        config.ozone_half_width_km <= 0.0F) {
        throw std::runtime_error("atmosphere scale heights must be positive");
    }
    if (config.rayleigh_density_scale < 0.0F || config.mie_density_scale < 0.0F ||
        config.mie_scattering < 0.0F || config.mie_extinction < 0.0F) {
        throw std::runtime_error("atmosphere density and Mie coefficients must be nonnegative");
    }
    if (config.mie_anisotropy < 0.0F || config.mie_anisotropy >= 1.0F) {
        throw std::runtime_error("atmosphere Mie anisotropy must be in [0, 1)");
    }
    if (config.ground_albedo < 0.0F || config.ground_albedo > 1.0F) {
        throw std::runtime_error("atmosphere ground albedo must be in [0, 1]");
    }
    if (config.sun_elevation_degrees < -90.0F || config.sun_elevation_degrees > 90.0F ||
        config.sun_azimuth_degrees < -360.0F || config.sun_azimuth_degrees > 360.0F) {
        throw std::runtime_error("atmosphere sun angles must be within supported ranges");
    }
    if (config.sun_angular_radius <= 0.0F || config.camera_altitude_km < 0.0F) {
        throw std::runtime_error("atmosphere sun radius must be positive and altitude nonnegative");
    }
    if (config.reference_grid_km <= 0.0F || config.reference_intensity < 0.0F) {
        throw std::runtime_error("atmosphere reference grid scale must be positive");
    }
    if (config.night_sky.twilight_strength < 0.0F || config.night_sky.twilight_strength > 4.0F ||
        config.night_sky.twilight_horizon_warmth < 0.0F ||
        config.night_sky.twilight_horizon_warmth > 2.0F || config.night_sky.star_intensity < 0.0F ||
        config.night_sky.star_intensity > 4.0F || config.night_sky.star_density < 0.0F ||
        config.night_sky.star_density > 1.0F || config.night_sky.milky_way_intensity < 0.0F ||
        config.night_sky.milky_way_intensity > 4.0F || config.night_sky.milky_way_contrast < 0.0F ||
        config.night_sky.milky_way_contrast > 4.0F || config.night_sky.light_pollution < 0.0F ||
        config.night_sky.light_pollution > 1.0F || config.night_sky.procedural_variation < 0.0F ||
        config.night_sky.procedural_variation > 16.0F) {
        throw std::runtime_error("atmosphere night-sky controls are out of range");
    }
    if (config.moon.disk_intensity < 0.0F || config.moon.disk_intensity > 4.0F ||
        config.moon.moonlight_intensity < 0.0F || config.moon.moonlight_intensity > 4.0F ||
        config.moon.phase_offset_days < 0.0F || config.moon.phase_offset_days > 29.530588F ||
        config.moon.angular_radius_scale <= 0.0F || config.moon.angular_radius_scale > 8.0F) {
        throw std::runtime_error("atmosphere moon controls are out of range");
    }
    if (config.time_of_day.time_hours < 0.0F || config.time_of_day.time_hours > 24.0F ||
        config.time_of_day.day_of_year < 1.0F || config.time_of_day.day_of_year > 366.0F) {
        throw std::runtime_error("atmosphere time fields must be within supported ranges");
    }
    if (config.time_of_day.latitude_degrees < -90.0F ||
        config.time_of_day.latitude_degrees > 90.0F ||
        config.time_of_day.azimuth_offset_degrees < -360.0F ||
        config.time_of_day.azimuth_offset_degrees > 360.0F) {
        throw std::runtime_error("atmosphere solar clock angles must be within supported ranges");
    }
    if (config.time_of_day.speed_hours_per_second < 0.0F ||
        config.time_of_day.exposure_bias < -4.0F || config.time_of_day.exposure_bias > 4.0F) {
        throw std::runtime_error("atmosphere time speed and exposure bias are out of range");
    }
}

[[nodiscard]] inline AtmosphereConfig atmosphere_config_from_run_config(const RunConfig& run) {
    AtmosphereConfig config =
        atmosphere_config_for_preset(atmosphere_preset_from_name(run.atmosphere.preset));
    config.render_view = atmosphere_render_view_from_name(run.debug_view);
    if (!run.atmosphere.time_of_day_mode.empty()) {
        config.time_of_day.mode = sun_control_mode_from_name(run.atmosphere.time_of_day_mode);
    }
    if (!run.atmosphere.night_sky_mode.empty()) {
        config.night_sky.visual_mode =
            night_sky_visual_mode_from_name(run.atmosphere.night_sky_mode);
    }
    if (!run.atmosphere.milky_way_layer.empty()) {
        config.night_sky.layer = night_sky_layer_view_from_name(run.atmosphere.milky_way_layer);
    }
    const bool manual_sun_override =
        run_config_float_is_set(run.atmosphere.sun_elevation_degrees) ||
        run_config_float_is_set(run.atmosphere.sun_azimuth_degrees);
    if (manual_sun_override) {
        config.time_of_day.mode = SunControlMode::ManualSun;
    }
    if (run_config_float_is_set(run.atmosphere.sun_elevation_degrees)) {
        config.sun_elevation_degrees = run.atmosphere.sun_elevation_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.sun_azimuth_degrees)) {
        config.sun_azimuth_degrees = run.atmosphere.sun_azimuth_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.camera_altitude_km)) {
        config.camera_altitude_km = run.atmosphere.camera_altitude_km;
    }
    if (run_config_float_is_set(run.atmosphere.mie_scale)) {
        config.mie_density_scale = run.atmosphere.mie_scale;
    }
    if (run_config_float_is_set(run.atmosphere.time_hours)) {
        config.time_of_day.time_hours = run.atmosphere.time_hours;
    }
    if (run_config_float_is_set(run.atmosphere.day_of_year)) {
        config.time_of_day.day_of_year = run.atmosphere.day_of_year;
    }
    if (run_config_float_is_set(run.atmosphere.latitude_degrees)) {
        config.time_of_day.latitude_degrees = run.atmosphere.latitude_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.sun_azimuth_offset_degrees)) {
        config.time_of_day.azimuth_offset_degrees = run.atmosphere.sun_azimuth_offset_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.time_speed_hours_per_second)) {
        config.time_of_day.speed_hours_per_second = run.atmosphere.time_speed_hours_per_second;
    }
    if (run.atmosphere.time_paused == 1) {
        config.time_of_day.playing = false;
    }
    if (config.time_of_day.mode == SunControlMode::ManualSun && run.atmosphere.auto_exposure < 0) {
        config.time_of_day.auto_exposure_enabled = false;
    }
    if (run.atmosphere.auto_exposure >= 0) {
        config.time_of_day.auto_exposure_enabled = run.atmosphere.auto_exposure == 1;
    }
    if (run.pbr.exposure_explicit) {
        config.exposure = run.pbr.exposure;
        config.time_of_day.auto_exposure_enabled = false;
    }
    if (run_config_float_is_set(run.atmosphere.exposure_bias)) {
        config.time_of_day.exposure_bias = run.atmosphere.exposure_bias;
    }
    if (run_config_float_is_set(run.atmosphere.twilight_strength)) {
        config.night_sky.twilight_strength = run.atmosphere.twilight_strength;
    }
    if (run_config_float_is_set(run.atmosphere.twilight_horizon_warmth)) {
        config.night_sky.twilight_horizon_warmth = run.atmosphere.twilight_horizon_warmth;
    }
    if (run_config_float_is_set(run.atmosphere.star_intensity)) {
        config.night_sky.star_intensity = run.atmosphere.star_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.star_density)) {
        config.night_sky.star_density = run.atmosphere.star_density;
    }
    if (run_config_float_is_set(run.atmosphere.milky_way_intensity)) {
        config.night_sky.milky_way_intensity = run.atmosphere.milky_way_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.milky_way_contrast)) {
        config.night_sky.milky_way_contrast = run.atmosphere.milky_way_contrast;
    }
    if (run_config_float_is_set(run.atmosphere.light_pollution)) {
        config.night_sky.light_pollution = run.atmosphere.light_pollution;
    }
    if (run_config_float_is_set(run.atmosphere.milky_way_variation)) {
        config.night_sky.procedural_variation = run.atmosphere.milky_way_variation;
    }
    if (run.atmosphere.moon >= 0) {
        config.moon.enabled = run.atmosphere.moon == 1;
    }
    if (run_config_float_is_set(run.atmosphere.moon_intensity)) {
        config.moon.disk_intensity = run.atmosphere.moon_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.moonlight_intensity)) {
        config.moon.moonlight_intensity = run.atmosphere.moonlight_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.moon_phase_offset_days)) {
        config.moon.phase_offset_days = run.atmosphere.moon_phase_offset_days;
    }
    if (run_config_float_is_set(run.atmosphere.moon_size_scale)) {
        config.moon.angular_radius_scale = run.atmosphere.moon_size_scale;
    }
    resolve_atmosphere_time_of_day(config);
    validate_atmosphere_config(config);
    return config;
}

} // namespace cubey::projects::atmosphere
