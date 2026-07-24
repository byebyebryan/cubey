#pragma once

#include <cstdint>

namespace cubey::render {

enum class BackdropSurfaceClearancePolicy : std::uint8_t {
    ExactRequestedHeight,
    EnforceMinimumClearance,
};

struct BackdropSurfaceEnvelope {
    float nominal_local_height_m = 0.0F;
    float maximum_local_height_m = 0.0F;
};

struct BackdropForegroundEnvelope {
    float anchor_world_height_m = 0.0F;
    float minimum_local_height_m = 0.0F;
};

struct BackdropSurfacePlacementRequest {
    BackdropSurfaceEnvelope surface{};
    BackdropForegroundEnvelope foreground{};
    float requested_foreground_height_m = 0.0F;
    float minimum_clearance_m = 0.0F;
    BackdropSurfaceClearancePolicy clearance_policy =
        BackdropSurfaceClearancePolicy::EnforceMinimumClearance;
};

struct BackdropSurfacePlacement {
    float surface_world_translation_y = 0.0F;
    float requested_foreground_height_m = 0.0F;
    float required_foreground_height_m = 0.0F;
    float effective_foreground_height_m = 0.0F;
    float achieved_clearance_m = 0.0F;
    bool clearance_adjusted = false;
    bool clearance_satisfied = false;
    bool intersects_foreground = false;
};

[[nodiscard]] BackdropSurfacePlacement
resolve_backdrop_surface_placement(const BackdropSurfacePlacementRequest& request);

} // namespace cubey::render
