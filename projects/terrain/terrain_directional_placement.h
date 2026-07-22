#pragma once

#include "terrain_height_source.h"

#include <cstdint>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainDirectionalPlacementRequest {
    float search_extent_m = 32'000.0F;
    float search_step_m = 4'000.0F;
    bool detailed_search = false;
    float local_radius_m = 500.0F;
    float near_distance_m = 2'000.0F;
    float middle_distance_m = 6'000.0F;
    float far_distance_m = 9'000.0F;
    float remote_distance_m = 12'000.0F;
    float mountain_prominence_m = 500.0F;
    float open_prominence_m = 200.0F;
    float maximum_local_relief_m = 120.0F;
    float maximum_local_p95_slope = 0.275F;
    std::uint32_t sector_count = 24U;
    std::uint32_t minimum_mountain_sectors = 4U;
    std::uint32_t maximum_mountain_sectors = 14U;
    std::uint32_t minimum_mountain_arc_sectors = 3U;
    std::uint32_t minimum_open_arc_sectors = 4U;
    float vertical_scale = 1.0F;
};

[[nodiscard]] constexpr float
terrain_directional_placement_maximum_refinement_offset_m() noexcept {
    return 1'500.0F;
}

struct TerrainDirectionalSectorSample {
    float yaw_radians = 0.0F;
    float near_height_m = 0.0F;
    float middle_height_m = 0.0F;
    float far_height_m = 0.0F;
    float remote_height_m = 0.0F;
    float prominence_m = 0.0F;
    bool mountain = false;
    bool open = false;
    bool gradual_rise = false;
};

struct TerrainDirectionalPlacementPlan {
    cubey::math::Vec2 source_focus_xz{0.0F, 0.0F};
    float mountain_yaw_radians = 0.0F;
    float center_height_m = 0.0F;
    float local_radius_m = 0.0F;
    float local_relief_m = 0.0F;
    float maximum_local_relief_m = 0.0F;
    float local_p95_slope = 0.0F;
    float maximum_local_p95_slope = 0.0F;
    float mean_mountain_prominence_m = 0.0F;
    std::uint32_t sector_count = 0U;
    std::uint32_t mountain_sector_count = 0U;
    std::uint32_t open_sector_count = 0U;
    std::uint32_t gradual_rise_sector_count = 0U;
    std::uint32_t largest_mountain_arc_sectors = 0U;
    std::uint32_t largest_open_arc_sectors = 0U;
    std::uint32_t coarse_candidate_count = 0U;
    std::uint32_t refined_candidate_count = 0U;
    std::uint32_t full_candidate_count = 0U;
    bool contract_satisfied = false;
    float score = 0.0F;
    std::vector<TerrainDirectionalSectorSample> sectors{};
};

void validate_terrain_directional_placement_request(
    const TerrainDirectionalPlacementRequest& request);

[[nodiscard]] TerrainDirectionalPlacementPlan evaluate_terrain_directional_placement(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementRequest& request,
    cubey::math::Vec2 focus);

[[nodiscard]] TerrainDirectionalPlacementPlan plan_terrain_directional_placement(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementRequest& request = {});

} // namespace cubey::projects::terrain
