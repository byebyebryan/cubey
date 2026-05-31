#include <cubey/render/atmosphere_environment.h>

#include <algorithm>
#include <array>
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

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] math::Vec3 clamp_nonnegative(math::Vec3 value) {
    return {std::max(value.x, 0.0F), std::max(value.y, 0.0F), std::max(value.z, 0.0F)};
}

[[nodiscard]] math::Vec3 mix_vec3(math::Vec3 lhs, math::Vec3 rhs, float t) {
    return (lhs * (1.0F - t)) + (rhs * t);
}

[[nodiscard]] std::array<float, 9> sh_basis(math::Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        direction = {0.0F, 1.0F, 0.0F};
    }
    const math::Vec3 normal = glm::normalize(direction);
    const float x = normal.x;
    const float y = normal.y;
    const float z = normal.z;
    return {
        0.282095F,
        0.488603F * y,
        0.488603F * z,
        0.488603F * x,
        1.092548F * x * y,
        1.092548F * y * z,
        0.315392F * ((3.0F * z * z) - 1.0F),
        1.092548F * x * z,
        0.546274F * ((x * x) - (y * y)),
    };
}

[[nodiscard]] math::Vec3 evaluate_sh(const std::array<math::Vec3, 9>& coefficients,
                                     math::Vec3 direction) {
    const std::array<float, 9> basis = sh_basis(direction);
    math::Vec3 result{0.0F, 0.0F, 0.0F};
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        result += coefficients[index] * basis[index];
    }
    return clamp_nonnegative(result);
}

[[nodiscard]] math::Vec3 atmosphere_sun_color(float sun_elevation_degrees) {
    const float high_sun = smoothstep(4.0F, 38.0F, sun_elevation_degrees);
    const math::Vec3 warm{1.0F, 0.52F, 0.24F};
    const math::Vec3 daylight{1.0F, 0.94F, 0.82F};
    return mix_vec3(warm, daylight, high_sun);
}

[[nodiscard]] float atmosphere_sun_intensity(float sun_elevation_degrees) {
    const float daylight = smoothstep(-3.0F, 14.0F, sun_elevation_degrees);
    const float twilight = smoothstep(-8.0F, 1.0F, sun_elevation_degrees) *
                           (1.0F - smoothstep(3.0F, 16.0F, sun_elevation_degrees));
    return (daylight * 2.25F) + (twilight * 0.28F);
}

[[nodiscard]] math::Vec3 atmosphere_moon_color() {
    return {0.58F, 0.62F, 0.74F};
}

[[nodiscard]] float atmosphere_moon_intensity(const AtmosphereEnvironmentConfig& config,
                                              const AtmosphereEnvironmentLunarState& moon,
                                              float sun_elevation_degrees) {
    const float night = 1.0F - smoothstep(-12.0F, -3.0F, sun_elevation_degrees);
    const float above_horizon = smoothstep(-2.0F, 8.0F, std::asin(moon.direction.y) *
                                                       (180.0F / std::numbers::pi_v<float>));
    return config.moon.enabled ? config.moon.moonlight_intensity * moon.illumination *
                                     night * above_horizon * 0.18F
                               : 0.0F;
}

[[nodiscard]] math::Vec3 atmosphere_sample_lighting_radiance(
    const AtmosphereEnvironmentConfig& config, math::Vec3 direction, math::Vec3 sun_direction,
    math::Vec3 sun_color, float sun_intensity, const AtmosphereEnvironmentLunarState& moon,
    float moon_intensity) {
    const math::Vec3 ray = glm::normalize(direction);
    const float sun_elevation = config.sun_elevation_degrees;
    const float sky_factor = std::clamp((ray.y * 0.5F) + 0.5F, 0.0F, 1.0F);
    const float day = smoothstep(-6.0F, 14.0F, sun_elevation);
    const float high_sun = smoothstep(5.0F, 42.0F, sun_elevation);
    const float night = 1.0F - smoothstep(-18.0F, -5.0F, sun_elevation);

    if (ray.y < 0.0F) {
        const float ground_light = 0.010F + (0.070F * day) + (0.010F * moon_intensity);
        const math::Vec3 ground_tint =
            mix_vec3(math::Vec3{0.018F, 0.020F, 0.024F},
                     math::Vec3{config.ground_albedo, config.ground_albedo, config.ground_albedo},
                     0.65F);
        return ground_tint * ground_light;
    }

    const math::Vec3 night_zenith{0.003F, 0.005F, 0.012F};
    const math::Vec3 night_horizon{0.010F, 0.010F, 0.016F};
    const math::Vec3 low_sun_zenith{0.115F, 0.170F, 0.310F};
    const math::Vec3 low_sun_horizon{0.55F, 0.30F, 0.145F};
    const math::Vec3 day_zenith{0.36F, 0.50F, 0.82F};
    const math::Vec3 day_horizon{0.42F, 0.55F, 0.72F};

    const math::Vec3 zenith =
        mix_vec3(mix_vec3(night_zenith, low_sun_zenith, day), day_zenith, high_sun);
    const math::Vec3 horizon =
        mix_vec3(mix_vec3(night_horizon, low_sun_horizon, day), day_horizon, high_sun);
    math::Vec3 color = mix_vec3(horizon, zenith, std::pow(sky_factor, 0.72F));

    const float sun_alignment = std::max(glm::dot(ray, sun_direction), 0.0F);
    const float sun_power = 8.0F + ((26.0F - 8.0F) * high_sun);
    const float broad_sun = std::pow(sun_alignment, sun_power);
    color += sun_color * sun_intensity * broad_sun * 0.050F;

    const float moon_alignment = std::max(glm::dot(ray, moon.direction), 0.0F);
    color += atmosphere_moon_color() * moon_intensity * std::pow(moon_alignment, 18.0F) * 0.050F;
    color += atmosphere_moon_color() * moon_intensity * night * 0.018F;

    return clamp_nonnegative(color);
}

[[nodiscard]] std::array<math::Vec3, 9> project_atmosphere_lighting_sh(
    const AtmosphereEnvironmentConfig& config, math::Vec3 sun_direction, math::Vec3 sun_color,
    float sun_intensity, const AtmosphereEnvironmentLunarState& moon, float moon_intensity) {
    constexpr std::uint32_t kSampleCount = 128;
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kGoldenAngle = 2.39996322972865332F;
    constexpr std::array<float, 9> kLambertConvolution{
        kPi,       (2.0F * kPi) / 3.0F, (2.0F * kPi) / 3.0F,
        (2.0F * kPi) / 3.0F, kPi / 4.0F,          kPi / 4.0F,
        kPi / 4.0F,          kPi / 4.0F,          kPi / 4.0F,
    };
    std::array<math::Vec3, 9> coefficients{};
    const float sample_weight = (4.0F * kPi) / static_cast<float>(kSampleCount);
    for (std::uint32_t sample = 0; sample < kSampleCount; ++sample) {
        const float y = 1.0F - (2.0F * (static_cast<float>(sample) + 0.5F) /
                                static_cast<float>(kSampleCount));
        const float radius = std::sqrt(std::max(1.0F - (y * y), 0.0F));
        const float phi = static_cast<float>(sample) * kGoldenAngle;
        const math::Vec3 direction{std::cos(phi) * radius, y, std::sin(phi) * radius};
        const math::Vec3 radiance = atmosphere_sample_lighting_radiance(
            config, direction, sun_direction, sun_color, sun_intensity, moon, moon_intensity);
        const std::array<float, 9> basis = sh_basis(direction);
        for (std::size_t index = 0; index < coefficients.size(); ++index) {
            coefficients[index] += radiance * basis[index] * sample_weight;
        }
    }
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        coefficients[index] *= kLambertConvolution[index];
    }
    return coefficients;
}

[[nodiscard]] AtmosphereEnvironmentSolarPosition
solar_position(const AtmosphereEnvironmentTimeOfDay& time_of_day) {
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

float atmosphere_environment_radians_to_degrees(float radians) {
    return radians * (180.0F / std::numbers::pi_v<float>);
}

float atmosphere_environment_wrap_time_hours(float time_hours) {
    return wrap_time_hours(time_hours);
}

float atmosphere_environment_wrap_signed_degrees(float degrees) {
    return wrap_signed_degrees(degrees);
}

float atmosphere_environment_advance_day_of_year(float day_of_year, int day_delta) {
    return advance_day_of_year(day_of_year, day_delta);
}

float atmosphere_environment_wrap_unit(float value) {
    return wrap_unit(value);
}

AtmosphereEnvironmentSolarPosition atmosphere_environment_solar_position(
    const AtmosphereEnvironmentTimeOfDay& time_of_day) {
    return solar_position(time_of_day);
}

math::Vec3 atmosphere_environment_direction_from_alt_az(float elevation_degrees,
                                                       float azimuth_degrees) {
    return direction_from_alt_az(elevation_degrees, azimuth_degrees);
}

math::Vec3 atmosphere_environment_sun_direction(const AtmosphereEnvironmentConfig& config) {
    return glm::normalize(atmosphere_environment_direction_from_alt_az(
        config.sun_elevation_degrees, config.sun_azimuth_degrees));
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
    const AtmosphereEnvironmentSolarPosition moon_position =
        atmosphere_environment_solar_position(moon_clock);
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

float atmosphere_environment_auto_exposure(float sun_elevation_degrees, float exposure_bias) {
    const float daylight = smoothstep(-6.0F, 20.0F, sun_elevation_degrees);
    const float full_night = 1.0F - smoothstep(-18.0F, -6.0F, sun_elevation_degrees);
    return std::clamp(exposure_bias + 2.2F * (1.0F - daylight) + 0.6F * full_night,
                      -4.0F, 4.0F);
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
        .render_options =
            {
                static_cast<float>(static_cast<std::uint32_t>(config.ground_mode)),
                0.0F,
                0.0F,
                0.0F,
            },
    };
}

std::array<float, 9> atmosphere_environment_sh_basis(math::Vec3 direction) {
    return sh_basis(direction);
}

math::Vec3 atmosphere_environment_evaluate_sh(const std::array<math::Vec3, 9>& coefficients,
                                              math::Vec3 direction) {
    return evaluate_sh(coefficients, direction);
}

AtmosphereEnvironmentLighting atmosphere_environment_lighting(
    const AtmosphereEnvironmentConfig& config) {
    const math::Vec3 sun_direction = atmosphere_environment_sun_direction(config);
    const AtmosphereEnvironmentLunarState moon =
        atmosphere_environment_lunar_state(config.time_of_day, config.moon);
    const math::Vec3 sun_color = atmosphere_sun_color(config.sun_elevation_degrees);
    const float sun_intensity = atmosphere_sun_intensity(config.sun_elevation_degrees);
    const float moon_intensity = atmosphere_moon_intensity(config, moon,
                                                          config.sun_elevation_degrees);
    const std::array<math::Vec3, 9> diffuse_irradiance_sh = project_atmosphere_lighting_sh(
        config, sun_direction, sun_color, sun_intensity, moon, moon_intensity);
    const math::Vec3 upward_irradiance =
        evaluate_sh(diffuse_irradiance_sh, {0.0F, 1.0F, 0.0F});
    const bool use_sun = sun_intensity >= moon_intensity;

    return {
        .sun_direction = sun_direction,
        .sun_color = sun_color,
        .sun_intensity = sun_intensity,
        .moon_direction = moon.direction,
        .moon_color = atmosphere_moon_color(),
        .moon_intensity = moon_intensity,
        .primary_light_direction = use_sun ? sun_direction : moon.direction,
        .primary_light_color = use_sun ? sun_color : atmosphere_moon_color(),
        .primary_light_intensity = use_sun ? sun_intensity : moon_intensity,
        .ambient_color = upward_irradiance,
        .ambient_intensity = 1.0F,
        .diffuse_irradiance_sh = diffuse_irradiance_sh,
    };
}

} // namespace cubey::render
