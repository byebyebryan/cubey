#include "atmosphere_environment.h"

#include <cmath>
#include <numbers>

namespace cubey::projects::atmosphere {

cubey::math::Vec3 atmosphere_sun_direction(const AtmosphereConfig& config) {
    const float elevation = atmosphere_degrees_to_radians(config.sun_elevation_degrees);
    const float azimuth = atmosphere_degrees_to_radians(config.sun_azimuth_degrees);
    const float horizontal = std::cos(elevation);
    return glm::normalize(cubey::math::Vec3{
        horizontal * std::sin(azimuth),
        std::sin(elevation),
        -horizontal * std::cos(azimuth),
    });
}

AtmosphereFrameUniforms atmosphere_frame_uniforms(const AtmosphereConfig& config,
                                                  const AtmosphereFrameUniformInputs& inputs) {
    const cubey::math::Vec3 sun = atmosphere_sun_direction(config);
    const float sidereal_angle = atmosphere_sidereal_angle_radians(config.time_of_day);
    const float latitude = atmosphere_degrees_to_radians(config.time_of_day.latitude_degrees);
    const LunarState lunar_state = atmosphere_lunar_state(config.time_of_day, config.moon);

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
        .display_transform = inputs.display_transform,
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
                config.night_sky.visual_mode == NightSkyVisualMode::Camera ? 1.0F : 0.0F,
            },
    };
}

} // namespace cubey::projects::atmosphere
