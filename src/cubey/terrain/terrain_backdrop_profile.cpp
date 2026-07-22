#include <cubey/terrain/terrain_backdrop_profile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace cubey::terrain {

TerrainBackdropStagePlan plan_terrain_focused_backdrop_stage(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementPlan& placement,
    float vertical_scale, TerrainFocusedBackdropStageParameters parameters,
    TerrainDirectionalPlacementRequest placement_request) {
    const bool finite = std::isfinite(vertical_scale) && std::isfinite(parameters.focus_height_m) &&
                        std::isfinite(parameters.orbit_min_radius_m) &&
                        std::isfinite(parameters.orbit_default_radius_m) &&
                        std::isfinite(parameters.orbit_max_radius_m) &&
                        std::isfinite(parameters.orbit_min_elevation_radians) &&
                        std::isfinite(parameters.orbit_default_elevation_radians) &&
                        std::isfinite(parameters.orbit_max_elevation_radians);
    if (!finite || vertical_scale <= 0.0F || parameters.focus_height_m <= 0.0F ||
        parameters.orbit_min_radius_m <= 0.0F ||
        parameters.orbit_default_radius_m < parameters.orbit_min_radius_m ||
        parameters.orbit_max_radius_m < parameters.orbit_default_radius_m ||
        parameters.orbit_min_elevation_radians < 0.0F ||
        parameters.orbit_default_elevation_radians < parameters.orbit_min_elevation_radians ||
        parameters.orbit_max_elevation_radians < parameters.orbit_default_elevation_radians ||
        parameters.orbit_max_elevation_radians >= std::numbers::pi_v<float> * 0.5F) {
        throw std::runtime_error("invalid focused backdrop stage parameters");
    }
    placement_request.vertical_scale = vertical_scale;
    const TerrainDirectionalPlacementPlan displayed = evaluate_terrain_directional_placement(
        source, placement_request, placement.source_focus_xz);
    const float center_height =
        source.sample_height({.world_xz = placement.source_focus_xz, .footprint_m = 16.0F}) *
        vertical_scale;
    float target_height = center_height + parameters.focus_height_m;
    float minimum_camera_clearance = std::numeric_limits<float>::infinity();
    const std::array<float, 3> radii{parameters.orbit_min_radius_m,
                                     parameters.orbit_default_radius_m,
                                     parameters.orbit_max_radius_m};
    const std::array<float, 3> elevations{parameters.orbit_min_elevation_radians,
                                          parameters.orbit_default_elevation_radians,
                                          parameters.orbit_max_elevation_radians};
    for (std::uint32_t sector = 0U; sector < 24U; ++sector) {
        const float yaw = static_cast<float>(sector) * 2.0F * std::numbers::pi_v<float> / 24.0F;
        const cubey::math::Vec2 direction{std::sin(yaw), -std::cos(yaw)};
        for (const float radius : radii) {
            for (const float elevation : elevations) {
                const float horizontal_radius = std::cos(elevation) * radius;
                const cubey::math::Vec2 camera_local = direction * -horizontal_radius;
                const float camera_height = target_height + std::sin(elevation) * radius;
                const float terrain_height =
                    source.sample_height({.world_xz = placement.source_focus_xz + camera_local,
                                          .footprint_m = 16.0F}) *
                    vertical_scale;
                minimum_camera_clearance =
                    std::min(minimum_camera_clearance, camera_height - terrain_height);
            }
        }
    }
    if (minimum_camera_clearance < 10.0F) {
        target_height += 10.0F - minimum_camera_clearance;
        minimum_camera_clearance = 10.0F;
    }
    const bool contract = minimum_camera_clearance >= 10.0F;
    return {
        .mode = TerrainBackdropStageMode::Grounded,
        .source_focus_xz = placement.source_focus_xz,
        .source_center_height_m = center_height,
        .stage_plane_height_m = center_height,
        .target_height_m = target_height,
        .terrain_vertical_offset_m = -target_height,
        .local_relief_m = displayed.local_relief_m,
        .local_p95_slope = displayed.local_p95_slope,
        .minimum_camera_clearance_m = minimum_camera_clearance,
        .showcase_yaw_radians =
            terrain_backdrop_camera_yaw_for_source_direction(placement.mountain_yaw_radians),
        .stage_radius_m = 300.0F,
        .orbit_min_radius_m = parameters.orbit_min_radius_m,
        .orbit_default_radius_m = parameters.orbit_default_radius_m,
        .orbit_max_radius_m = parameters.orbit_max_radius_m,
        .orbit_min_elevation_radians = parameters.orbit_min_elevation_radians,
        .orbit_default_elevation_radians = parameters.orbit_default_elevation_radians,
        .orbit_max_elevation_radians = parameters.orbit_max_elevation_radians,
        .panorama_sector_count = displayed.sector_count,
        .lower_frame_clear_sector_count = displayed.open_sector_count,
        .relief_sector_count = displayed.mountain_sector_count,
        .minimum_lower_frame_terrain_distance_m = 0.0F,
        .contract_satisfied = contract,
        .score = displayed.score,
    };
}

} // namespace cubey::terrain
