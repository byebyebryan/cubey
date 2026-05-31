#include "atmosphere_environment.h"

#include <cstdint>

namespace cubey::projects::atmosphere {

cubey::render::AtmosphereEnvironmentRenderView
atmosphere_environment_render_view(AtmosphereRenderView view) {
    return static_cast<cubey::render::AtmosphereEnvironmentRenderView>(
        static_cast<std::uint32_t>(view));
}

cubey::render::AtmosphereEnvironmentConfig
atmosphere_environment_config(const AtmosphereConfig& config) {
    return {
        .time_of_day =
            {
                .time_hours = config.time_of_day.time_hours,
                .day_of_year = config.time_of_day.day_of_year,
                .latitude_degrees = config.time_of_day.latitude_degrees,
                .azimuth_offset_degrees = config.time_of_day.azimuth_offset_degrees,
            },
        .night_sky =
            {
                .twilight_strength = config.night_sky.twilight_strength,
                .twilight_horizon_warmth = config.night_sky.twilight_horizon_warmth,
                .star_intensity = config.night_sky.star_intensity,
                .star_density = config.night_sky.star_density,
                .milky_way_intensity = config.night_sky.milky_way_intensity,
                .milky_way_contrast = config.night_sky.milky_way_contrast,
                .light_pollution = config.night_sky.light_pollution,
                .camera_visual_mode =
                    config.night_sky.visual_mode == NightSkyVisualMode::Camera,
            },
        .moon =
            {
                .enabled = config.moon.enabled,
                .disk_intensity = config.moon.disk_intensity,
                .moonlight_intensity = config.moon.moonlight_intensity,
                .phase_offset_days = config.moon.phase_offset_days,
                .angular_radius_scale = config.moon.angular_radius_scale,
            },
        .bottom_radius_km = config.bottom_radius_km,
        .top_radius_km = config.top_radius_km,
        .rayleigh_scattering = config.rayleigh_scattering,
        .rayleigh_scale_height_km = config.rayleigh_scale_height_km,
        .rayleigh_density_scale = config.rayleigh_density_scale,
        .mie_scattering = config.mie_scattering,
        .mie_extinction = config.mie_extinction,
        .mie_scale_height_km = config.mie_scale_height_km,
        .mie_anisotropy = config.mie_anisotropy,
        .mie_density_scale = config.mie_density_scale,
        .ozone_absorption = config.ozone_absorption,
        .ozone_center_altitude_km = config.ozone_center_altitude_km,
        .ozone_half_width_km = config.ozone_half_width_km,
        .ground_albedo = config.ground_albedo,
        .sun_angular_radius = config.sun_angular_radius,
        .sun_elevation_degrees = config.sun_elevation_degrees,
        .sun_azimuth_degrees = config.sun_azimuth_degrees,
        .camera_altitude_km = config.camera_altitude_km,
        .reference_geometry_enabled = config.reference_geometry_enabled,
        .reference_grid_km = config.reference_grid_km,
        .reference_intensity = config.reference_intensity,
    };
}

cubey::math::Vec3 atmosphere_sun_direction(const AtmosphereConfig& config) {
    return cubey::render::atmosphere_environment_sun_direction(
        atmosphere_environment_config(config));
}

AtmosphereFrameUniforms atmosphere_frame_uniforms(const AtmosphereConfig& config,
                                                  const AtmosphereFrameUniformInputs& inputs) {
    return cubey::render::atmosphere_environment_frame_uniforms(
        atmosphere_environment_config(config),
        {
            .view_rays = inputs.view_rays,
            .render_view = atmosphere_environment_render_view(inputs.render_view),
            .display_transform = inputs.display_transform,
        });
}

} // namespace cubey::projects::atmosphere
