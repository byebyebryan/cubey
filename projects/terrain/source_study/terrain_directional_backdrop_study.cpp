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
    case TerrainDirectionalBackdropLane::ExpandedShaped:
        return "expanded-shaped";
    case TerrainDirectionalBackdropLane::ExpandedRadial:
        return "expanded-radial";
    case TerrainDirectionalBackdropLane::CachedRadial:
        return "cached-radial";
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
    if (name == "expanded-shaped") {
        return TerrainDirectionalBackdropLane::ExpandedShaped;
    }
    if (name == "expanded-radial") {
        return TerrainDirectionalBackdropLane::ExpandedRadial;
    }
    if (name == "cached-radial") {
        return TerrainDirectionalBackdropLane::CachedRadial;
    }
    throw std::runtime_error("unknown terrain directional backdrop lane: " + std::string(name));
}

std::string_view terrain_radial_fidelity_name(TerrainRadialFidelity fidelity) noexcept {
    switch (fidelity) {
    case TerrainRadialFidelity::Control:
        return "control";
    case TerrainRadialFidelity::Source:
        return "source";
    case TerrainRadialFidelity::Material:
        return "material";
    case TerrainRadialFidelity::Combined:
        return "combined";
    }
    return "control";
}

TerrainRadialFidelity terrain_radial_fidelity_from_name(std::string_view name) {
    if (name.empty() || name == "control") {
        return TerrainRadialFidelity::Control;
    }
    if (name == "source") {
        return TerrainRadialFidelity::Source;
    }
    if (name == "material") {
        return TerrainRadialFidelity::Material;
    }
    if (name == "combined") {
        return TerrainRadialFidelity::Combined;
    }
    throw std::runtime_error("unknown terrain radial fidelity variant: " + std::string(name));
}

bool terrain_radial_fidelity_uses_source_detail(TerrainRadialFidelity fidelity) noexcept {
    return fidelity == TerrainRadialFidelity::Source || fidelity == TerrainRadialFidelity::Combined;
}

bool terrain_radial_fidelity_uses_material_detail(TerrainRadialFidelity fidelity) noexcept {
    return fidelity == TerrainRadialFidelity::Material ||
           fidelity == TerrainRadialFidelity::Combined;
}

TerrainDirectionalPlacementRequest directional_backdrop_placement_request() {
    return {};
}

TerrainDirectionalReliefParameters
expanded_directional_backdrop_relief_parameters(const TerrainDirectionalPlacementPlan& placement) {
    return {
        .focus_xz = placement.source_focus_xz,
        .mountain_yaw_radians = placement.mountain_yaw_radians,
        .floor_footprint_m = 8'000.0F,
        .floor_relief_fraction = 0.08F,
        .structure_footprint_m = 2'500.0F,
        .broad_start_m = 6'000.0F,
        .broad_full_m = 18'000.0F,
        .detail_start_m = 10'000.0F,
        .detail_full_m = 26'000.0F,
        .warp_period_m = 28'000.0F,
        .warp_amplitude_m = 2'500.0F,
        .warp_octaves = 2U,
    };
}

TerrainRadialReliefParameters
expanded_radial_backdrop_relief_parameters(const TerrainDirectionalPlacementPlan& placement) {
    return terrain_radial_backdrop_relief_parameters(placement);
}

TerrainRadialReliefParameters
radial_fidelity_backdrop_relief_parameters(const TerrainDirectionalPlacementPlan& placement,
                                           TerrainRadialFidelity fidelity) {
    TerrainRadialReliefParameters parameters =
        expanded_radial_backdrop_relief_parameters(placement);
    if (terrain_radial_fidelity_uses_source_detail(fidelity)) {
        parameters.structure_footprint_m = 900.0F;
        parameters.detail_footprint_m = 180.0F;
        parameters.detail_full_m = 24'000.0F;
    }
    return parameters;
}

std::uint32_t cached_radial_backdrop_render_stride(std::optional<std::uint32_t> requested_stride) {
    const std::uint32_t stride = requested_stride.value_or(3U);
    if (stride < 1U || stride > 3U) {
        throw std::runtime_error("cached radial render stride must be 1, 2, or 3");
    }
    return stride;
}

TerrainBackdropStagePlan make_directional_backdrop_stage_plan(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementPlan& placement,
    float vertical_scale, TerrainDirectionalBackdropStageParameters parameters) {
    return plan_terrain_focused_backdrop_stage(source, placement, vertical_scale, parameters);
}

} // namespace cubey::projects::terrain
