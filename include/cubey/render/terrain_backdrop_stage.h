#pragma once

#include <cubey/asset/terrain_height_source.h>

#include <cstdint>

namespace cubey::render {

enum class TerrainBackdropStageMode : std::uint8_t {
    Detached,
    Grounded,
};

[[nodiscard]] constexpr float
terrain_backdrop_camera_yaw_for_source_direction(float source_yaw_radians) noexcept {
    return -source_yaw_radians;
}

struct TerrainBackdropStagePlan {
    TerrainBackdropStageMode mode = TerrainBackdropStageMode::Detached;
    cubey::math::Vec2 source_focus_xz{0.0F, 0.0F};
    float source_center_height_m = 0.0F;
    float stage_plane_height_m = 0.0F;
    float target_height_m = 0.0F;
    float terrain_vertical_offset_m = 0.0F;
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
    std::uint32_t lower_frame_clear_sector_count = 0U;
    std::uint32_t relief_sector_count = 0U;
    float minimum_lower_frame_terrain_distance_m = 0.0F;
    std::uint32_t coarse_candidate_count = 0U;
    std::uint32_t refined_candidate_count = 0U;
    std::uint32_t full_candidate_count = 0U;
    bool contract_satisfied = false;
    float score = 0.0F;
};

} // namespace cubey::render
