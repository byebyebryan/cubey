#pragma once

#include "terrain_backdrop_profile.h"

namespace cubey::projects::terrain {

struct TerrainNaturalBackdropStageRequest {
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

struct TerrainNaturalBackdropStagePlan {
    TerrainDirectionalPlacementPlan placement{};
    TerrainBackdropStagePlan stage{};
    float centered_search_support_radius_m = 0.0F;
    float selected_support_radius_m = 0.0F;
};

[[nodiscard]] float terrain_natural_backdrop_centered_support_radius(
    const TerrainNaturalBackdropStageRequest& request, float gradient_step_m);

[[nodiscard]] TerrainNaturalBackdropStagePlan plan_terrain_natural_backdrop_stage(
    const TerrainHeightSource& source, const TerrainNaturalBackdropStageRequest& request = {});

} // namespace cubey::projects::terrain
