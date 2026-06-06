#include "planet_frame.h"

#include <algorithm>
#include <cmath>

namespace cubey::projects::planet {
namespace {

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

} // namespace

PlanetFrame make_planet_frame(const PlanetConfig& config,
                              const cubey::Transform3D& camera_transform) {
    validate_planet_config(config);

    const cubey::math::Vec3 camera_position = camera_transform.translation;
    const float requested_camera_radius = glm::length(camera_position);
    const float minimum_camera_radius = config.radius_m + std::max(1.0F, config.radius_m * 0.001F);
    const float camera_radius = std::max(requested_camera_radius, minimum_camera_radius);
    const float camera_altitude = std::max(camera_radius - config.radius_m, 0.0F);
    const float atmosphere_outer_radius = config.radius_m + config.atmosphere_height_m;
    const float horizon_sq =
        std::max((camera_radius * camera_radius) - (config.radius_m * config.radius_m), 0.0F);
    const float horizon_distance = std::sqrt(horizon_sq);
    const float near_plane =
        std::max(1.0F, std::min(camera_altitude * 0.001F, config.radius_m * 0.005F));
    const float far_plane = camera_radius + atmosphere_outer_radius;

    const cubey::math::Vec3 up = normalize_or(camera_position, cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    const cubey::math::Vec3 right = tangent_right_from_up(up);
    const cubey::math::Vec3 forward =
        normalize_or(glm::cross(up, right), cubey::math::Vec3{0.0F, 0.0F, 1.0F});
    const cubey::math::DVec3 camera_world{
        static_cast<double>(up.x) * static_cast<double>(camera_radius),
        static_cast<double>(up.y) * static_cast<double>(camera_radius),
        static_cast<double>(up.z) * static_cast<double>(camera_radius),
    };
    const cubey::math::DVec3 surface_origin{
        static_cast<double>(up.x) * static_cast<double>(config.radius_m),
        static_cast<double>(up.y) * static_cast<double>(config.radius_m),
        static_cast<double>(up.z) * static_cast<double>(config.radius_m),
    };

    return {
        .planet_radius_m = config.radius_m,
        .atmosphere_outer_radius_m = atmosphere_outer_radius,
        .camera_radius_m = camera_radius,
        .camera_altitude_m = camera_altitude,
        .horizon_distance_m = horizon_distance,
        .near_plane_m = near_plane,
        .far_plane_m = far_plane,
        .camera_world_position_m = camera_world,
        .render_origin_world_m = camera_world,
        .surface_origin_m = surface_origin,
        .local_frame =
            {
                .world_origin_m = surface_origin,
                .right = right,
                .up = up,
                .forward = forward,
                .planet_radius_m = config.radius_m,
                .water_datum_m = config.sea_level_m,
            },
    };
}

cubey::math::Vec3 planet_frame_world_to_render_m(const PlanetFrame& frame,
                                                 cubey::math::DVec3 world_position_m) {
    const cubey::math::DVec3 relative = world_position_m - frame.render_origin_world_m;
    return {
        static_cast<float>(relative.x),
        static_cast<float>(relative.y),
        static_cast<float>(relative.z),
    };
}

cubey::math::DVec3 planet_frame_render_to_world_m(const PlanetFrame& frame,
                                                  cubey::math::Vec3 render_position_m) {
    return frame.render_origin_world_m +
           cubey::math::DVec3{static_cast<double>(render_position_m.x),
                              static_cast<double>(render_position_m.y),
                              static_cast<double>(render_position_m.z)};
}

} // namespace cubey::projects::planet
