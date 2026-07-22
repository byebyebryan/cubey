#pragma once

#include <cubey/render/terrain_backdrop_profile.h>
#include <cubey/render/terrain_placement_mode.h>

#include <cstdint>

namespace cubey::render {

using cubey::asset::TerrainHeightSource;
using cubey::asset::TerrainHeightSourceBounds;
using cubey::asset::TerrainHeightSourceMetadata;

struct TerrainBackdropPlacementRequest {
    TerrainPlacementMode mode = TerrainPlacementMode::Selected;
    std::uint32_t sample_index = 0U;
    TerrainDirectionalPlacementRequest placement{
        .search_extent_m = 12'000.0F,
        .search_step_m = 4'000.0F,
        .detailed_search = true,
    };
    TerrainFocusedBackdropStageParameters stage{
        .focus_height_m = 500.0F,
    };
    float outer_radius_m = 16'384.0F;
    float vertical_scale = 1.0F;
};

struct TerrainBackdropPlacementPlan {
    TerrainPlacementMode mode = TerrainPlacementMode::Selected;
    std::uint32_t sample_index = 0U;
    TerrainDirectionalPlacementPlan placement{};
    TerrainBackdropStagePlan stage{};
    float centered_search_support_radius_m = 0.0F;
    float selected_support_radius_m = 0.0F;
};

[[nodiscard]] float
terrain_backdrop_centered_search_support_radius(const TerrainBackdropPlacementRequest& request,
                                                float gradient_step_m);

[[nodiscard]] float
terrain_backdrop_selected_support_radius(const TerrainBackdropPlacementRequest& request,
                                         float gradient_step_m);

[[nodiscard]] cubey::math::Vec2
terrain_backdrop_raw_sample_focus(const TerrainHeightSourceBounds& bounds, float sample_spacing_m,
                                  float support_radius_m, std::uint32_t sample_index);

[[nodiscard]] TerrainBackdropPlacementPlan
plan_terrain_backdrop_placement(const TerrainHeightSource& source,
                                const TerrainHeightSourceBounds& bounds,
                                const TerrainBackdropPlacementRequest& request = {});

} // namespace cubey::render
