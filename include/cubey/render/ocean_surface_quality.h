#pragma once

#include "ocean_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace cubey::render {

struct OceanPatchShadingPlan {
    OceanDetailFilter detail_filter = OceanDetailFilter::Adaptive;
    std::uint32_t self_shadow_steps = 8U;
    float conservative_footprint_m = 0.0F;
    bool detail_filter_reduced = false;
    bool self_shadow_reduced = false;
};

[[nodiscard]] inline float ocean_interval_distance(float value, float minimum, float maximum) {
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        minimum > maximum) {
        throw std::runtime_error("ocean interval distance inputs must be finite and ordered");
    }
    if (value < minimum) {
        return minimum - value;
    }
    if (value > maximum) {
        return value - maximum;
    }
    return 0.0F;
}

[[nodiscard]] inline float
ocean_patch_nearest_horizontal_distance_m(const OceanMeshPatch& patch,
                                          float camera_x_m,
                                          float camera_z_m) {
    if (!std::isfinite(camera_x_m) || !std::isfinite(camera_z_m)) {
        throw std::runtime_error("ocean patch camera position must be finite");
    }
    const float snap = ocean_mesh_patch_snap_size(patch);
    const float snapped_x = std::floor(camera_x_m / snap) * snap;
    const float snapped_z = std::floor(camera_z_m / snap) * snap;
    const float distance_x =
        ocean_interval_distance(camera_x_m, snapped_x + patch.bounds.min_x,
                                snapped_x + patch.bounds.max_x);
    const float distance_z =
        ocean_interval_distance(camera_z_m, snapped_z + patch.bounds.min_z,
                                snapped_z + patch.bounds.max_z);
    return std::hypot(distance_x, distance_z);
}

[[nodiscard]] inline float ocean_projected_pixel_footprint_m(float distance_m,
                                                             float fovy_radians,
                                                             std::uint32_t viewport_height) {
    if (!std::isfinite(distance_m) || !std::isfinite(fovy_radians) || distance_m < 0.0F ||
        fovy_radians <= 0.0F || fovy_radians >= std::numbers::pi_v<float> ||
        viewport_height == 0U) {
        throw std::runtime_error("ocean projected footprint inputs are invalid");
    }
    return 2.0F * distance_m * std::tan(fovy_radians * 0.5F) /
           static_cast<float>(viewport_height);
}

[[nodiscard]] inline float ocean_patch_conservative_footprint_m(
    const OceanMeshPatch& patch,
    float camera_x_m,
    float camera_z_m,
    float camera_altitude_m,
    float fovy_radians,
    std::uint32_t viewport_height) {
    if (!std::isfinite(camera_altitude_m) || camera_altitude_m < 0.0F) {
        throw std::runtime_error("ocean patch camera altitude must be nonnegative");
    }
    const float horizontal_distance =
        ocean_patch_nearest_horizontal_distance_m(patch, camera_x_m, camera_z_m);
    const float slant_distance = std::hypot(horizontal_distance, camera_altitude_m);
    return std::max(ocean_mesh_patch_cell_size(patch),
                    ocean_projected_pixel_footprint_m(slant_distance, fovy_radians,
                                                      viewport_height));
}

[[nodiscard]] inline OceanPatchShadingPlan ocean_patch_shading_plan(
    const OceanSurfaceConfig& config,
    const OceanMeshPatch& patch,
    float camera_x_m,
    float camera_z_m,
    float camera_altitude_m,
    float fovy_radians,
    std::uint32_t viewport_height) {
    validate_ocean_config(config);
    const float footprint =
        ocean_patch_conservative_footprint_m(patch, camera_x_m, camera_z_m,
                                             camera_altitude_m, fovy_radians, viewport_height);
    OceanPatchShadingPlan plan{
        .detail_filter = config.detail_filter,
        .self_shadow_steps = config.self_shadow_steps,
        .conservative_footprint_m = footprint,
    };
    if (config.surface_shading_policy != OceanSurfaceShadingPolicy::FootprintAdaptive ||
        footprint < config.far_detail_footprint_end_m) {
        return plan;
    }

    if (config.detail_filter == OceanDetailFilter::Adaptive) {
        plan.detail_filter = OceanDetailFilter::Bilinear;
        plan.detail_filter_reduced = true;
    }
    plan.self_shadow_steps =
        std::min(config.self_shadow_steps, config.self_shadow_far_steps);
    plan.self_shadow_reduced = plan.self_shadow_steps < config.self_shadow_steps;
    return plan;
}

} // namespace cubey::render
