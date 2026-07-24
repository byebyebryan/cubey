#include <cubey/render/backdrop_surface_placement.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::render {

BackdropSurfacePlacement
resolve_backdrop_surface_placement(const BackdropSurfacePlacementRequest& request) {
    if (!std::isfinite(request.surface.nominal_local_height_m) ||
        !std::isfinite(request.surface.maximum_local_height_m) ||
        !std::isfinite(request.foreground.anchor_world_height_m) ||
        !std::isfinite(request.foreground.minimum_local_height_m) ||
        !std::isfinite(request.requested_foreground_height_m) ||
        !std::isfinite(request.minimum_clearance_m) ||
        request.surface.maximum_local_height_m < request.surface.nominal_local_height_m ||
        request.minimum_clearance_m < 0.0F) {
        throw std::runtime_error("invalid backdrop surface placement request");
    }

    const float surface_excursion_m =
        request.surface.maximum_local_height_m - request.surface.nominal_local_height_m;
    const float required_foreground_height_m = surface_excursion_m -
                                               request.foreground.minimum_local_height_m +
                                               request.minimum_clearance_m;
    const bool enforce_clearance =
        request.clearance_policy == BackdropSurfaceClearancePolicy::EnforceMinimumClearance;
    const float effective_foreground_height_m =
        enforce_clearance
            ? std::max(request.requested_foreground_height_m, required_foreground_height_m)
            : request.requested_foreground_height_m;
    const float achieved_clearance_m = effective_foreground_height_m +
                                       request.foreground.minimum_local_height_m -
                                       surface_excursion_m;
    constexpr float kComparisonEpsilonM = 1.0e-4F;

    return {
        .surface_world_translation_y = request.foreground.anchor_world_height_m -
                                       effective_foreground_height_m -
                                       request.surface.nominal_local_height_m,
        .requested_foreground_height_m = request.requested_foreground_height_m,
        .required_foreground_height_m = required_foreground_height_m,
        .effective_foreground_height_m = effective_foreground_height_m,
        .achieved_clearance_m = achieved_clearance_m,
        .clearance_adjusted = effective_foreground_height_m >
                              request.requested_foreground_height_m + kComparisonEpsilonM,
        .clearance_satisfied =
            achieved_clearance_m + kComparisonEpsilonM >= request.minimum_clearance_m,
        .intersects_foreground = achieved_clearance_m < -kComparisonEpsilonM,
    };
}

} // namespace cubey::render
