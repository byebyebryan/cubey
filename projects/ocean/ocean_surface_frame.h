#pragma once

#include "ocean_horizon.h"

#include <cubey/render/local_tangent_frame.h>

namespace cubey::projects::ocean {

struct OceanSurfaceFrame {
    cubey::render::LocalTangentFrame local_frame{};
    cubey::math::DVec3 camera_world_position_m{0.0, 0.0, 0.0};
    cubey::math::Vec3 camera_local_position_m{0.0F, 0.0F, 0.0F};
    OceanConfig mesh_config{};
    OceanHorizonDiagnostics horizon{};
    float projection_far_plane_m = 1.0F;
    OceanSurfaceMode surface_mode = OceanSurfaceMode::CurvedFar;
    float curvature_start_m = 0.0F;
    float curvature_end_m = 0.0F;
    float curvature_strength = 1.0F;
    bool flat_surface = true;
};

[[nodiscard]] inline float ocean_surface_smoothstep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0F : 0.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] inline float ocean_above_surface_orbit_pitch(float distance_m,
                                                           float requested_pitch_radians,
                                                           float clearance_m) {
    if (!std::isfinite(distance_m) || !std::isfinite(requested_pitch_radians) ||
        !std::isfinite(clearance_m) || distance_m <= 0.0F || clearance_m < 0.0F) {
        throw std::runtime_error("ocean camera clearance inputs must be valid");
    }
    const float clearance_ratio = std::clamp(clearance_m / distance_m, 0.0F, 0.99F);
    const float highest_allowed_pitch = -std::asin(clearance_ratio);
    return std::min(requested_pitch_radians, highest_allowed_pitch);
}

[[nodiscard]] inline float ocean_spherical_surface_drop_m(float local_distance_m,
                                                          float planet_radius_m) {
    if (!std::isfinite(local_distance_m) || !std::isfinite(planet_radius_m) ||
        local_distance_m < 0.0F || planet_radius_m <= 0.0F) {
        throw std::runtime_error("ocean spherical surface drop inputs must be valid");
    }
    const double radius = static_cast<double>(planet_radius_m);
    const double distance = std::min(static_cast<double>(local_distance_m), radius);
    return static_cast<float>(std::sqrt(std::max(0.0, radius * radius - distance * distance)) -
                              radius);
}

[[nodiscard]] inline float ocean_surface_curvature_drop_m(float local_distance_m,
                                                          float planet_radius_m,
                                                          float curvature_start_m,
                                                          float curvature_end_m,
                                                          float curvature_strength) {
    if (!std::isfinite(curvature_start_m) || !std::isfinite(curvature_end_m) ||
        !std::isfinite(curvature_strength) || curvature_start_m < 0.0F ||
        curvature_end_m <= curvature_start_m || curvature_strength < 0.0F ||
        curvature_strength > 1.0F) {
        throw std::runtime_error("ocean surface curvature inputs must be valid");
    }
    const float blend =
        ocean_surface_smoothstep(curvature_start_m, curvature_end_m, local_distance_m);
    return ocean_spherical_surface_drop_m(local_distance_m, planet_radius_m) * blend *
           curvature_strength;
}

[[nodiscard]] inline cubey::render::LocalTangentFrame
ocean_local_tangent_frame(float planet_radius_m, float water_datum_m = 0.0F) {
    cubey::render::LocalTangentFrame frame{};
    frame.planet_radius_m = planet_radius_m;
    frame.water_datum_m = water_datum_m;
    cubey::render::validate_local_tangent_frame(frame);
    return frame;
}

[[nodiscard]] inline OceanSurfaceFrame
ocean_surface_frame_from_camera(const OceanConfig& config,
                                cubey::math::Vec3 camera_position_m,
                                float planet_radius_m,
                                float water_datum_m = 0.0F) {
    OceanSurfaceFrame frame{};
    frame.local_frame = ocean_local_tangent_frame(planet_radius_m, water_datum_m);
    frame.camera_world_position_m = {
        static_cast<double>(camera_position_m.x),
        static_cast<double>(camera_position_m.y),
        static_cast<double>(camera_position_m.z),
    };
    frame.camera_local_position_m = cubey::render::local_tangent_world_to_local_m(
        frame.local_frame, frame.camera_world_position_m);
    frame.mesh_config = ocean_horizon_effective_mesh_config(
        config, frame.camera_local_position_m.y, frame.local_frame.water_datum_m,
        frame.local_frame.planet_radius_m);
    frame.horizon =
        ocean_horizon_diagnostics(frame.mesh_config, frame.camera_local_position_m.y,
                                  frame.local_frame.water_datum_m,
                                  frame.local_frame.planet_radius_m,
                                  config.horizon_extent_margin);
    frame.projection_far_plane_m = ocean_horizon_projection_far_plane_m(frame.horizon);
    frame.surface_mode = config.surface_mode;
    frame.curvature_start_m = frame.horizon.horizon_distance_m * config.curvature_start_ratio;
    frame.curvature_end_m = frame.horizon.horizon_distance_m * config.curvature_end_ratio;
    frame.curvature_strength = frame.surface_mode == OceanSurfaceMode::CurvedFar
                                   ? std::clamp(config.curvature_strength, 0.0F, 1.0F)
                                   : 0.0F;
    frame.flat_surface = frame.curvature_strength <= 0.0F;
    return frame;
}

} // namespace cubey::projects::ocean
