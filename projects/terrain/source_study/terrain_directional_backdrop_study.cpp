#include "terrain_directional_backdrop_study.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain {

std::string_view
terrain_directional_backdrop_lane_name(TerrainDirectionalBackdropLane lane) noexcept {
    switch (lane) {
    case TerrainDirectionalBackdropLane::HardCut:
        return "hard-cut";
    case TerrainDirectionalBackdropLane::ContinuousCurrent:
        return "continuous-current";
    case TerrainDirectionalBackdropLane::Placement:
        return "placement";
    case TerrainDirectionalBackdropLane::Shaped:
        return "shaped";
    }
    return "placement";
}

TerrainDirectionalBackdropLane terrain_directional_backdrop_lane_from_name(std::string_view name) {
    if (name == "hard-cut") {
        return TerrainDirectionalBackdropLane::HardCut;
    }
    if (name == "continuous-current") {
        return TerrainDirectionalBackdropLane::ContinuousCurrent;
    }
    if (name.empty() || name == "placement") {
        return TerrainDirectionalBackdropLane::Placement;
    }
    if (name == "shaped") {
        return TerrainDirectionalBackdropLane::Shaped;
    }
    throw std::runtime_error("unknown terrain directional backdrop lane: " + std::string(name));
}

TerrainDirectionalPlacementRequest directional_backdrop_placement_request() {
    return {};
}

TerrainBackdropStagePlan make_directional_backdrop_stage_plan(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementPlan& placement,
    float vertical_scale) {
    if (!std::isfinite(vertical_scale) || vertical_scale <= 0.0F) {
        throw std::runtime_error("invalid directional backdrop vertical scale");
    }
    const TerrainDirectionalPlacementPlan displayed = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), placement.source_focus_xz);
    const float center_height = source.sample_height(
                                    {.world_xz = placement.source_focus_xz, .footprint_m = 16.0F}) *
                                vertical_scale;
    constexpr float kSubjectCenterHeightM = 20.0F;
    float target_height = center_height + kSubjectCenterHeightM;
    float minimum_camera_clearance = std::numeric_limits<float>::infinity();
    constexpr std::array<float, 3> kRadii{50.0F, 100.0F, 250.0F};
    constexpr std::array<float, 3> kElevationsDegrees{0.0F, 8.0F, 30.0F};
    for (std::uint32_t sector = 0U; sector < 24U; ++sector) {
        const float yaw = static_cast<float>(sector) * 2.0F * std::numbers::pi_v<float> / 24.0F;
        const cubey::math::Vec2 direction{std::sin(yaw), -std::cos(yaw)};
        for (const float radius : kRadii) {
            for (const float elevation_degrees : kElevationsDegrees) {
                const float elevation = elevation_degrees * std::numbers::pi_v<float> / 180.0F;
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
    const bool contract = displayed.contract_satisfied && minimum_camera_clearance >= 10.0F;
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
        .showcase_yaw_radians = placement.mountain_yaw_radians,
        .stage_radius_m = 300.0F,
        .orbit_min_radius_m = 50.0F,
        .orbit_default_radius_m = 100.0F,
        .orbit_max_radius_m = 250.0F,
        .orbit_min_elevation_radians = 0.0F,
        .orbit_default_elevation_radians = 8.0F * std::numbers::pi_v<float> / 180.0F,
        .orbit_max_elevation_radians = 30.0F * std::numbers::pi_v<float> / 180.0F,
        .panorama_sector_count = displayed.sector_count,
        .lower_frame_clear_sector_count = displayed.open_sector_count,
        .relief_sector_count = displayed.mountain_sector_count,
        .minimum_lower_frame_terrain_distance_m = 0.0F,
        .contract_satisfied = contract,
        .score = displayed.score,
    };
}

} // namespace cubey::projects::terrain
