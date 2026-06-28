#include "planet_atmosphere_adapter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cubey::projects::planet {
namespace {

[[nodiscard]] cubey::math::Vec3 normalized_or_up(cubey::math::Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(direction);
}

[[nodiscard]] float moon_illumination(float phase_fraction) {
    const float wrapped = phase_fraction - std::floor(phase_fraction);
    return std::clamp(0.5F - 0.5F * std::cos(wrapped * 2.0F * std::numbers::pi_v<float>), 0.0F,
                      1.0F);
}

struct PlanetAtmosphereTangentFrame {
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, 1.0F};
};

[[nodiscard]] cubey::math::Vec3 normalized_or_fallback(cubey::math::Vec3 value,
                                                       cubey::math::Vec3 fallback) {
    if (glm::dot(value, value) <= 0.00000001F) {
        return glm::normalize(fallback);
    }
    return glm::normalize(value);
}

[[nodiscard]] PlanetAtmosphereTangentFrame planet_atmosphere_tangent_frame(
    const PlanetAtmosphereInputs& inputs, const cubey::render::LocalTangentFrame& local_frame) {
    PlanetAtmosphereTangentFrame frame;
    frame.up = normalized_or_fallback(local_frame.up, normalized_or_up(inputs.camera_position_m));
    frame.right = normalized_or_fallback(local_frame.right, {1.0F, 0.0F, 0.0F});
    frame.forward = normalized_or_fallback(local_frame.forward, glm::cross(frame.up, frame.right));
    return frame;
}

[[nodiscard]] cubey::math::Vec3 to_unified_atmosphere_space(
    cubey::math::Vec3 direction, const PlanetAtmosphereTangentFrame& frame) {
    const cubey::math::Vec3 normalized = normalized_or_up(direction);
    return {
        glm::dot(normalized, frame.right),
        glm::dot(normalized, frame.up),
        glm::dot(normalized, frame.forward),
    };
}

[[nodiscard]] cubey::math::Vec3 to_unified_atmosphere_position_km(
    cubey::math::Vec3 position_m, const PlanetAtmosphereTangentFrame& frame,
    const PlanetAtmosphereInputs& inputs) {
    if (glm::dot(position_m, position_m) <= 0.000001F) {
        return {0.0F, (inputs.planet_radius_m + std::max(inputs.camera_altitude_m, 0.0F)) * 0.001F,
                0.0F};
    }
    constexpr float kMetersToKm = 0.001F;
    return {
        glm::dot(position_m, frame.right) * kMetersToKm,
        glm::dot(position_m, frame.up) * kMetersToKm,
        glm::dot(position_m, frame.forward) * kMetersToKm,
    };
}

[[nodiscard]] cubey::render::ViewRayBasis3D unified_atmosphere_view_rays(
    const cubey::render::ViewRayBasis3D& view_rays, const PlanetAtmosphereTangentFrame& frame) {
    const cubey::math::Vec3 right =
        to_unified_atmosphere_space(cubey::math::Vec3{view_rays.right_aspect}, frame);
    const cubey::math::Vec3 up =
        to_unified_atmosphere_space(cubey::math::Vec3{view_rays.up_tan_half_fovy}, frame);
    const cubey::math::Vec3 forward =
        to_unified_atmosphere_space(cubey::math::Vec3{view_rays.forward}, frame);
    return {
        .right_aspect = {right.x, right.y, right.z, view_rays.right_aspect.w},
        .up_tan_half_fovy = {up.x, up.y, up.z, view_rays.up_tan_half_fovy.w},
        .forward = {forward.x, forward.y, forward.z, 0.0F},
    };
}

} // namespace

cubey::render::AtmosphereEnvironmentConfig planet_atmosphere_environment_config(
    const PlanetAtmosphereInputs& inputs,
    const cubey::render::AtmosphereEnvironmentConfig& look_config) {
    constexpr float kMetersToKm = 0.001F;
    constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
    const cubey::math::Vec3 sun_direction = normalized_or_up(inputs.sun_direction);
    const float elevation_degrees =
        std::asin(std::clamp(sun_direction.y, -1.0F, 1.0F)) * kRadiansToDegrees;
    const float azimuth_degrees = std::atan2(sun_direction.x, -sun_direction.z) * kRadiansToDegrees;

    cubey::render::AtmosphereEnvironmentConfig config = look_config;
    config.bottom_radius_km = std::max(inputs.planet_radius_m * kMetersToKm, 0.001F);
    config.top_radius_km =
        std::max(inputs.atmosphere_outer_radius_m * kMetersToKm, config.bottom_radius_km);
    config.camera_altitude_km = std::max(inputs.camera_altitude_m * kMetersToKm, 0.0F);
    config.sun_elevation_degrees = elevation_degrees;
    config.sun_azimuth_degrees =
        cubey::render::atmosphere_environment_wrap_signed_degrees(azimuth_degrees);
    config.sun_angular_radius = inputs.sun_angular_radius_rad;
    config.render_celestial_content = true;
    config.render_sun_disk = true;
    config.render_night_sky = true;
    config.render_moon_disk = false;
    config.reference_geometry_enabled = false;
    config.ground_mode =
        cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    config.moon.enabled = true;
    return config;
}

cubey::render::AtmosphereEnvironmentFrameUniforms planet_unified_atmosphere_frame_uniforms(
    const PlanetAtmosphereInputs& inputs, const PlanetUnifiedAtmosphereFrameInputs& frame_inputs) {
    const PlanetAtmosphereTangentFrame tangent =
        planet_atmosphere_tangent_frame(inputs, frame_inputs.local_frame);
    const cubey::render::ViewRayBasis3D view_rays =
        unified_atmosphere_view_rays(frame_inputs.view_rays, tangent);
    const cubey::math::Vec3 camera_position_km =
        to_unified_atmosphere_position_km(inputs.camera_position_m, tangent, inputs);
    cubey::render::AtmosphereEnvironmentConfig config =
        planet_atmosphere_environment_config(inputs, frame_inputs.look_config);
    cubey::render::AtmosphereEnvironmentFrameUniforms uniforms =
        cubey::render::atmosphere_environment_frame_uniforms(
            config, {
                        .view_rays = view_rays,
                        .render_view = frame_inputs.render_view,
                        .camera_position_km = camera_position_km,
                        .camera_position_km_explicit = true,
                    });

    const cubey::math::Vec3 sun_direction =
        to_unified_atmosphere_space(inputs.sun_direction, tangent);
    const cubey::math::Vec3 moon_direction =
        to_unified_atmosphere_space(inputs.moon_direction, tangent);
    uniforms.sun_direction_radius = {
        sun_direction.x,
        sun_direction.y,
        sun_direction.z,
        inputs.sun_angular_radius_rad,
    };
    uniforms.moon_direction_radius = {
        moon_direction.x,
        moon_direction.y,
        moon_direction.z,
        inputs.moon_angular_radius_rad,
    };
    uniforms.moon_options.w = moon_illumination(inputs.moon_phase_fraction);
    uniforms.moon_phase_options.x = inputs.moon_phase_fraction;
    uniforms.moon_phase_options.y =
        std::sin(inputs.moon_phase_fraction * 2.0F * std::numbers::pi_v<float>);
    return uniforms;
}

} // namespace cubey::projects::planet
