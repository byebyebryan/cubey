#include "terrain_natural_backdrop_stage.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

void validate_request(const TerrainNaturalBackdropStageRequest& request) {
    validate_terrain_directional_placement_request(request.placement);
    if (!std::isfinite(request.outer_radius_m) || request.outer_radius_m <= 0.0F ||
        !std::isfinite(request.vertical_scale) || request.vertical_scale <= 0.0F) {
        throw std::runtime_error("invalid natural terrain backdrop stage request");
    }
}

} // namespace

float terrain_natural_backdrop_centered_support_radius(
    const TerrainNaturalBackdropStageRequest& request, float gradient_step_m) {
    validate_request(request);
    if (!std::isfinite(gradient_step_m) || gradient_step_m < 0.0F) {
        throw std::runtime_error("invalid natural terrain backdrop gradient step");
    }
    const float selected_support = request.outer_radius_m + gradient_step_m;
    const float sampled_support =
        std::max(selected_support, request.placement.remote_distance_m + gradient_step_m);
    return request.placement.search_extent_m +
           terrain_directional_placement_maximum_refinement_offset_m() + sampled_support;
}

TerrainNaturalBackdropStagePlan plan_terrain_natural_backdrop_stage(
    const TerrainHeightSource& source, const TerrainNaturalBackdropStageRequest& request) {
    validate_request(request);
    TerrainDirectionalPlacementRequest placement_request = request.placement;
    placement_request.vertical_scale = request.vertical_scale;
    const TerrainDirectionalPlacementPlan placement =
        plan_terrain_directional_placement(source, placement_request);
    const TerrainBackdropStagePlan stage = plan_terrain_focused_backdrop_stage(
        source, placement, request.vertical_scale, request.stage, placement_request);
    const float gradient_step_m = source.metadata().gradient_step_m;
    return {
        .placement = placement,
        .stage = stage,
        .centered_search_support_radius_m =
            terrain_natural_backdrop_centered_support_radius(request, gradient_step_m),
        .selected_support_radius_m = request.outer_radius_m + gradient_step_m,
    };
}

} // namespace cubey::projects::terrain
