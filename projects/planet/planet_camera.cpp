#include "planet_camera.h"

#include <cubey/scene/camera_3d.h>

#include <algorithm>
#include <cmath>

namespace cubey::projects::planet {
namespace {

constexpr float kSurfaceLookDownSlope = 0.22F;
constexpr float kSurfaceMinPitchRadians = -0.45F;
constexpr float kSurfaceMaxPitchRadians = 1.20F;

[[nodiscard]] cubey::math::Vec3 normalize_or(cubey::math::Vec3 value, cubey::math::Vec3 fallback) {
    const float length = glm::length(value);
    if (length <= 0.000001F) {
        return fallback;
    }
    return value / length;
}

[[nodiscard]] cubey::math::Vec3 tangent_right_from_up(cubey::math::Vec3 up) {
    cubey::math::Vec3 right = glm::cross(cubey::math::Vec3{0.0F, 0.0F, 1.0F}, up);
    if (glm::length(right) <= 0.000001F) {
        right = glm::cross(cubey::math::Vec3{1.0F, 0.0F, 0.0F}, up);
    }
    return normalize_or(right, cubey::math::Vec3{1.0F, 0.0F, 0.0F});
}

[[nodiscard]] float smootherstep(float value) {
    const float x = std::clamp(value, 0.0F, 1.0F);
    return x * x * x * (x * (x * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] cubey::Transform3D planet_orbit_transform(float distance_m, float yaw_radians,
                                                        float pitch_radians) {
    return cubey::orbit_camera_transform({
        .target = {0.0F, 0.0F, 0.0F},
        .distance = distance_m,
        .yaw = yaw_radians,
        .pitch = pitch_radians,
    });
}

[[nodiscard]] cubey::math::Quat surface_camera_rotation(const cubey::Transform3D& anchor_transform,
                                                        float local_yaw_radians,
                                                        float local_pitch_radians) {
    const cubey::math::Vec3 up =
        normalize_or(anchor_transform.translation, cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    cubey::math::Vec3 right = anchor_transform.rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F};
    right = normalize_or(right - up * glm::dot(right, up), tangent_right_from_up(up));

    cubey::math::Vec3 tangent_forward =
        normalize_or(glm::cross(up, right), cubey::math::Vec3{0.0F, 0.0F, -1.0F});
    const cubey::math::Quat local_yaw = cubey::math::angle_axis_quat(local_yaw_radians, up);
    right = normalize_or(local_yaw * right, right);
    tangent_forward = normalize_or(local_yaw * tangent_forward, tangent_forward);

    const float surface_pitch = std::clamp(std::atan(kSurfaceLookDownSlope) + local_pitch_radians,
                                           kSurfaceMinPitchRadians, kSurfaceMaxPitchRadians);
    const cubey::math::Vec3 forward = normalize_or(
        tangent_forward * std::cos(surface_pitch) - up * std::sin(surface_pitch), tangent_forward);
    const cubey::math::Vec3 camera_up = normalize_or(glm::cross(right, forward), up);
    const cubey::math::Vec3 camera_back = -forward;

    return glm::normalize(glm::quat_cast(cubey::math::Mat3{right, camera_up, camera_back}));
}

} // namespace

float planet_camera_min_altitude_m(const PlanetConfig& config) {
    validate_planet_config(config);
    const float terrain_clearance =
        config.terrain_enabled ? config.terrain_height_scale_m * 1.2F : 0.0F;
    return std::max({500.0F, config.radius_m * 0.003F, terrain_clearance});
}

float planet_surface_camera_blend(const PlanetConfig& config, float distance_m) {
    validate_planet_config(config);
    const float altitude_m = std::max(distance_m - config.radius_m, 0.0F);
    const float full_surface_altitude =
        std::max(planet_camera_min_altitude_m(config) * 2.0F, config.radius_m * 0.04F);
    const float orbit_altitude = std::max(full_surface_altitude * 4.0F, config.radius_m * 0.30F);
    if (altitude_m >= orbit_altitude) {
        return 0.0F;
    }
    if (altitude_m <= full_surface_altitude) {
        return 1.0F;
    }
    const float t = (orbit_altitude - altitude_m) / (orbit_altitude - full_surface_altitude);
    return smootherstep(t);
}

cubey::Transform3D make_planet_camera_transform(const PlanetConfig& config,
                                                PlanetCameraState state) {
    validate_planet_config(config);
    const cubey::Transform3D orbit_transform =
        planet_orbit_transform(state.distance_m, state.yaw_radians, state.pitch_radians);
    const float blend = planet_surface_camera_blend(config, state.distance_m);
    if (blend <= 0.0F) {
        return orbit_transform;
    }

    const float anchor_yaw =
        state.surface_anchor_active ? state.surface_anchor_yaw_radians : state.yaw_radians;
    const float anchor_pitch =
        state.surface_anchor_active ? state.surface_anchor_pitch_radians : state.pitch_radians;
    const cubey::Transform3D anchor_transform =
        planet_orbit_transform(state.distance_m, anchor_yaw, anchor_pitch);
    const cubey::math::Quat surface_rotation = surface_camera_rotation(
        anchor_transform, state.yaw_radians - anchor_yaw, state.pitch_radians - anchor_pitch);
    return {
        .translation =
            orbit_transform.translation * (1.0F - blend) + anchor_transform.translation * blend,
        .rotation = glm::normalize(glm::slerp(orbit_transform.rotation, surface_rotation, blend)),
    };
}

} // namespace cubey::projects::planet
