#pragma once

#include "terrain_source.h"

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainBackdropStageMode : std::uint8_t {
    Detached,
    Grounded,
};

[[nodiscard]] std::string_view
terrain_backdrop_stage_mode_name(TerrainBackdropStageMode mode) noexcept;

struct TerrainBackdropStageRequest {
    TerrainBackdropStageMode mode = TerrainBackdropStageMode::Detached;
    float stage_radius_m = 300.0F;
    float guard_radius_m = 400.0F;
    float orbit_min_radius_m = 50.0F;
    float orbit_default_radius_m = 100.0F;
    float orbit_max_radius_m = 150.0F;
    float orbit_min_elevation_radians = 0.0F;
    float orbit_default_elevation_radians = 0.0F;
    float orbit_max_elevation_radians = 0.0F;
    float subject_center_height_m = 20.0F;
    float detached_stage_clearance_m = 240.0F;
    float vertical_fov_radians = 0.0F;
    float aspect_ratio = 16.0F / 9.0F;
    float vertical_scale = 1.0F;
};

[[nodiscard]] TerrainBackdropStageRequest terrain_backdrop_stage_request(
    TerrainBackdropStageMode mode, float aspect_ratio = 16.0F / 9.0F,
    float vertical_scale = 1.0F);

struct TerrainBackdropStagePlan {
    TerrainBackdropStageMode mode = TerrainBackdropStageMode::Detached;
    cubey::math::Vec2 source_focus_xz{0.0F, 0.0F};
    float source_center_height_m = 0.0F;
    float stage_plane_height_m = 0.0F;
    float target_height_m = 0.0F;
    float local_relief_m = 0.0F;
    float local_p95_slope = 0.0F;
    float minimum_camera_clearance_m = 0.0F;
    float showcase_yaw_radians = 0.0F;
    float stage_radius_m = 0.0F;
    float orbit_min_radius_m = 0.0F;
    float orbit_default_radius_m = 0.0F;
    float orbit_max_radius_m = 0.0F;
    float orbit_min_elevation_radians = 0.0F;
    float orbit_default_elevation_radians = 0.0F;
    float orbit_max_elevation_radians = 0.0F;
    std::uint32_t panorama_sector_count = 0U;
    std::uint32_t horizon_clear_sector_count = 0U;
    std::uint32_t relief_sector_count = 0U;
    float minimum_horizon_clearance_distance_m = 0.0F;
    std::uint32_t coarse_candidate_count = 0U;
    std::uint32_t refined_candidate_count = 0U;
    std::uint32_t full_candidate_count = 0U;
    bool contract_satisfied = false;
    float score = 0.0F;
};

[[nodiscard]] TerrainBackdropStagePlan
plan_terrain_backdrop_stage(const TerrainSourceParameters& source,
                            const TerrainBackdropStageRequest& request);

} // namespace cubey::projects::terrain
