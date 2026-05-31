#include <cubey/render/atmosphere_environment.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cubey::render {
namespace {

[[nodiscard]] float wrap_time_hours(float time_hours) {
    float wrapped = std::fmod(time_hours, 24.0F);
    if (wrapped < 0.0F) {
        wrapped += 24.0F;
    }
    return wrapped;
}

[[nodiscard]] float wrap_signed_degrees(float degrees) {
    float wrapped = std::fmod(degrees + 180.0F, 360.0F);
    if (wrapped < 0.0F) {
        wrapped += 360.0F;
    }
    return wrapped - 180.0F;
}

[[nodiscard]] float wrap_unit(float value) {
    float wrapped = std::fmod(value, 1.0F);
    if (wrapped < 0.0F) {
        wrapped += 1.0F;
    }
    return wrapped;
}

[[nodiscard]] float advance_day_of_year(float day_of_year, int day_delta) {
    float wrapped = std::fmod(day_of_year - 1.0F + static_cast<float>(day_delta), 366.0F);
    if (wrapped < 0.0F) {
        wrapped += 366.0F;
    }
    return wrapped + 1.0F;
}

struct SolarPosition {
    float elevation_degrees = 0.0F;
    float azimuth_degrees = 0.0F;
};

[[nodiscard]] SolarPosition solar_position(const AtmosphereEnvironmentTimeOfDay& time_of_day) {
    const float day_angle = atmosphere_environment_degrees_to_radians(
        (360.0F / 365.0F) * (time_of_day.day_of_year - 80.0F));
    const float declination =
        atmosphere_environment_degrees_to_radians(23.44F) * std::sin(day_angle);
    const float latitude =
        atmosphere_environment_degrees_to_radians(time_of_day.latitude_degrees);
    const float hour_angle = atmosphere_environment_degrees_to_radians(
        15.0F * (wrap_time_hours(time_of_day.time_hours) - 12.0F));

    const float sin_elevation = std::sin(latitude) * std::sin(declination) +
                                std::cos(latitude) * std::cos(declination) * std::cos(hour_angle);
    const float elevation = std::asin(std::clamp(sin_elevation, -1.0F, 1.0F));
    const float east = -std::cos(declination) * std::sin(hour_angle);
    const float south = -std::cos(latitude) * std::sin(declination) +
                        std::sin(latitude) * std::cos(declination) * std::cos(hour_angle);
    const float azimuth = (std::atan2(east, south) * (180.0F / std::numbers::pi_v<float>)) +
                          time_of_day.azimuth_offset_degrees;

    return {
        .elevation_degrees = elevation * (180.0F / std::numbers::pi_v<float>),
        .azimuth_degrees = wrap_signed_degrees(azimuth),
    };
}

[[nodiscard]] math::Vec3 direction_from_alt_az(float elevation_degrees, float azimuth_degrees) {
    const float elevation = atmosphere_environment_degrees_to_radians(elevation_degrees);
    const float azimuth = atmosphere_environment_degrees_to_radians(azimuth_degrees);
    const float horizontal = std::cos(elevation);
    return {
        horizontal * std::sin(azimuth),
        std::sin(elevation),
        -horizontal * std::cos(azimuth),
    };
}

} // namespace

float atmosphere_environment_degrees_to_radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

math::Vec3 atmosphere_environment_sun_direction(const AtmosphereEnvironmentConfig& config) {
    return glm::normalize(direction_from_alt_az(config.sun_elevation_degrees,
                                                config.sun_azimuth_degrees));
}

AtmosphereEnvironmentLunarState atmosphere_environment_lunar_state(
    const AtmosphereEnvironmentTimeOfDay& time_of_day, const AtmosphereEnvironmentMoon& moon) {
    constexpr float kLunarCycleDays = 29.530588F;
    constexpr float kTropicalYearDays = 365.2422F;
    constexpr float kMeanMoonAngularRadius = 0.00452F;
    const float lunar_age_days = std::fmod(
        time_of_day.day_of_year - 80.0F + time_of_day.time_hours / 24.0F + moon.phase_offset_days,
        kLunarCycleDays);
    const float phase_fraction = wrap_unit(lunar_age_days / kLunarCycleDays);
    const float phase_angle = phase_fraction * 2.0F * std::numbers::pi_v<float>;
    const float illumination = 0.5F - 0.5F * std::cos(phase_angle);

    AtmosphereEnvironmentTimeOfDay moon_clock = time_of_day;
    moon_clock.time_hours = wrap_time_hours(time_of_day.time_hours - phase_fraction * 24.0F);
    moon_clock.day_of_year = advance_day_of_year(
        time_of_day.day_of_year, static_cast<int>(std::floor(phase_fraction * kTropicalYearDays)));
    const SolarPosition moon_position = solar_position(moon_clock);
    return {
        .direction =
            direction_from_alt_az(moon_position.elevation_degrees, moon_position.azimuth_degrees),
        .phase_fraction = phase_fraction,
        .illumination = illumination,
        .angular_radius = kMeanMoonAngularRadius * moon.angular_radius_scale,
    };
}

float atmosphere_environment_sidereal_angle_radians(
    const AtmosphereEnvironmentTimeOfDay& time_of_day) {
    constexpr float kSiderealSolarRatio = 1.00273790935F;
    constexpr float kTropicalYearDays = 365.2422F;
    float rotations = (wrap_time_hours(time_of_day.time_hours) / 24.0F) * kSiderealSolarRatio +
                      (time_of_day.day_of_year - 80.0F) / kTropicalYearDays;
    rotations = rotations - std::floor(rotations);
    if (rotations < 0.0F) {
        rotations += 1.0F;
    }
    return rotations * 2.0F * std::numbers::pi_v<float>;
}

AtmosphereEnvironmentFrameUniforms atmosphere_environment_frame_uniforms(
    const AtmosphereEnvironmentConfig& config,
    const AtmosphereEnvironmentFrameUniformInputs& inputs) {
    const math::Vec3 sun = atmosphere_environment_sun_direction(config);
    const float sidereal_angle =
        atmosphere_environment_sidereal_angle_radians(config.time_of_day);
    const float latitude =
        atmosphere_environment_degrees_to_radians(config.time_of_day.latitude_degrees);
    const AtmosphereEnvironmentLunarState lunar_state =
        atmosphere_environment_lunar_state(config.time_of_day, config.moon);

    return {
        .camera_right_aspect = inputs.view_rays.right_aspect,
        .camera_up_tan_half_fovy = inputs.view_rays.up_tan_half_fovy,
        .camera_forward_debug_view =
            {
                inputs.view_rays.forward.x,
                inputs.view_rays.forward.y,
                inputs.view_rays.forward.z,
                static_cast<float>(static_cast<std::uint32_t>(inputs.render_view)),
            },
        .radii_ground =
            {
                config.bottom_radius_km,
                config.top_radius_km,
                config.camera_altitude_km,
                config.ground_albedo,
            },
        .rayleigh =
            {
                config.rayleigh_scattering.x * config.rayleigh_density_scale,
                config.rayleigh_scattering.y * config.rayleigh_density_scale,
                config.rayleigh_scattering.z * config.rayleigh_density_scale,
                config.rayleigh_scale_height_km,
            },
        .mie =
            {
                config.mie_scattering * config.mie_density_scale,
                config.mie_extinction * config.mie_density_scale,
                config.mie_scale_height_km,
                config.mie_anisotropy,
            },
        .ozone =
            {
                config.ozone_absorption.x,
                config.ozone_absorption.y,
                config.ozone_absorption.z,
                config.ozone_center_altitude_km,
            },
        .sun_direction_radius =
            {
                sun.x,
                sun.y,
                sun.z,
                config.sun_angular_radius,
            },
        .atmosphere_options =
            {
                config.ozone_half_width_km,
                config.reference_geometry_enabled ? 1.0F : 0.0F,
                config.reference_grid_km,
                config.reference_intensity,
            },
        .night_options =
            {
                config.night_sky.twilight_strength,
                config.night_sky.twilight_horizon_warmth,
                config.night_sky.star_intensity,
                config.night_sky.star_density,
            },
        .celestial_options =
            {
                std::cos(sidereal_angle),
                std::sin(sidereal_angle),
                std::sin(latitude),
                std::cos(latitude),
            },
        .moon_direction_radius =
            {
                lunar_state.direction.x,
                lunar_state.direction.y,
                lunar_state.direction.z,
                lunar_state.angular_radius,
            },
        .moon_options =
            {
                config.moon.enabled ? 1.0F : 0.0F,
                config.moon.disk_intensity,
                config.moon.moonlight_intensity,
                lunar_state.illumination,
            },
        .moon_phase_options =
            {
                lunar_state.phase_fraction,
                std::sin(lunar_state.phase_fraction * 2.0F * std::numbers::pi_v<float>),
                0.0F,
                0.0F,
            },
        .milky_way_options =
            {
                config.night_sky.milky_way_intensity,
                config.night_sky.milky_way_contrast,
                config.night_sky.light_pollution,
                config.night_sky.camera_visual_mode ? 1.0F : 0.0F,
            },
    };
}

} // namespace cubey::render
