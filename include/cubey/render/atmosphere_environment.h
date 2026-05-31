#pragma once

#include <cubey/core/math.h>
#include <cubey/render/view_ray_basis_3d.h>

#include <cstdint>

namespace cubey::render {

enum class AtmosphereEnvironmentRenderView : std::uint32_t {
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

struct AtmosphereEnvironmentTimeOfDay {
    float time_hours = 12.0F;
    float day_of_year = 80.0F;
    float latitude_degrees = 30.0F;
    float azimuth_offset_degrees = 0.0F;
};

struct AtmosphereEnvironmentSolarPosition {
    float elevation_degrees = 0.0F;
    float azimuth_degrees = 0.0F;
};

struct AtmosphereEnvironmentNightSky {
    float twilight_strength = 1.0F;
    float twilight_horizon_warmth = 1.0F;
    float star_intensity = 1.0F;
    float star_density = 0.65F;
    float milky_way_intensity = 0.75F;
    float milky_way_contrast = 1.0F;
    float light_pollution = 0.0F;
    bool camera_visual_mode = false;
};

struct AtmosphereEnvironmentMoon {
    bool enabled = true;
    float disk_intensity = 1.0F;
    float moonlight_intensity = 1.0F;
    float phase_offset_days = 14.765F;
    float angular_radius_scale = 1.0F;
};

struct AtmosphereEnvironmentConfig {
    AtmosphereEnvironmentTimeOfDay time_of_day{};
    AtmosphereEnvironmentNightSky night_sky{};
    AtmosphereEnvironmentMoon moon{};

    float bottom_radius_km = 6371.0F;
    float top_radius_km = 6471.0F;
    math::Vec3 rayleigh_scattering{0.005802F, 0.013558F, 0.033100F};
    float rayleigh_scale_height_km = 8.0F;
    float rayleigh_density_scale = 1.0F;

    float mie_scattering = 0.003996F;
    float mie_extinction = 0.004400F;
    float mie_scale_height_km = 1.2F;
    float mie_anisotropy = 0.80F;
    float mie_density_scale = 1.0F;

    math::Vec3 ozone_absorption{0.000650F, 0.001881F, 0.000085F};
    float ozone_center_altitude_km = 25.0F;
    float ozone_half_width_km = 15.0F;

    float ground_albedo = 0.10F;
    float sun_angular_radius = 0.004675F;
    float sun_elevation_degrees = 60.0F;
    float sun_azimuth_degrees = 0.0F;
    float camera_altitude_km = 0.15F;
    bool reference_geometry_enabled = true;
    float reference_grid_km = 1.0F;
    float reference_intensity = 0.72F;
};

struct AtmosphereEnvironmentLunarState {
    math::Vec3 direction{0.0F, 1.0F, 0.0F};
    float phase_fraction = 0.5F;
    float illumination = 1.0F;
    float angular_radius = 0.00452F;
};

struct AtmosphereEnvironmentFrameUniforms {
    math::Vec4 camera_right_aspect;
    math::Vec4 camera_up_tan_half_fovy;
    math::Vec4 camera_forward_debug_view;
    math::Vec4 radii_ground;
    math::Vec4 rayleigh;
    math::Vec4 mie;
    math::Vec4 ozone;
    math::Vec4 sun_direction_radius;
    math::Vec4 atmosphere_options;
    math::Vec4 night_options;
    math::Vec4 celestial_options;
    math::Vec4 moon_direction_radius;
    math::Vec4 moon_options;
    math::Vec4 moon_phase_options;
    math::Vec4 milky_way_options;
};

static_assert(sizeof(AtmosphereEnvironmentFrameUniforms) == sizeof(float) * 60U);

struct AtmosphereEnvironmentFrameUniformInputs {
    ViewRayBasis3D view_rays{};
    AtmosphereEnvironmentRenderView render_view = AtmosphereEnvironmentRenderView::Final;
};

[[nodiscard]] float atmosphere_environment_degrees_to_radians(float degrees);
[[nodiscard]] float atmosphere_environment_radians_to_degrees(float radians);
[[nodiscard]] float atmosphere_environment_wrap_time_hours(float time_hours);
[[nodiscard]] float atmosphere_environment_wrap_signed_degrees(float degrees);
[[nodiscard]] float atmosphere_environment_advance_day_of_year(float day_of_year, int day_delta);
[[nodiscard]] float atmosphere_environment_wrap_unit(float value);
[[nodiscard]] AtmosphereEnvironmentSolarPosition
atmosphere_environment_solar_position(const AtmosphereEnvironmentTimeOfDay& time_of_day);
[[nodiscard]] math::Vec3 atmosphere_environment_direction_from_alt_az(float elevation_degrees,
                                                                      float azimuth_degrees);
[[nodiscard]] math::Vec3
atmosphere_environment_sun_direction(const AtmosphereEnvironmentConfig& config);
[[nodiscard]] AtmosphereEnvironmentLunarState atmosphere_environment_lunar_state(
    const AtmosphereEnvironmentTimeOfDay& time_of_day, const AtmosphereEnvironmentMoon& moon);
[[nodiscard]] float atmosphere_environment_sidereal_angle_radians(
    const AtmosphereEnvironmentTimeOfDay& time_of_day);
[[nodiscard]] float atmosphere_environment_auto_exposure(float sun_elevation_degrees,
                                                        float exposure_bias);
[[nodiscard]] AtmosphereEnvironmentFrameUniforms atmosphere_environment_frame_uniforms(
    const AtmosphereEnvironmentConfig& config,
    const AtmosphereEnvironmentFrameUniformInputs& inputs);

} // namespace cubey::render
