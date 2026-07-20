#include "terrain_backdrop_placement.h"

#include <cubey/procedural/seed.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

constexpr std::uint64_t kRawPlacementSequenceSeed = 0x4355'4245'5954'5250ULL;
constexpr std::string_view kRawPlacementDomain = "terrain.backdrop.raw-placement.v1";

void validate_request(const TerrainBackdropPlacementRequest& request) {
    validate_terrain_directional_placement_request(request.placement);
    if (!std::isfinite(request.outer_radius_m) || request.outer_radius_m <= 0.0F ||
        !std::isfinite(request.vertical_scale) || request.vertical_scale <= 0.0F) {
        throw std::runtime_error("invalid terrain backdrop placement request");
    }
}

[[nodiscard]] float sampled_axis(float minimum, float maximum, float spacing, float support_radius,
                                 std::uint32_t sample_index, std::uint32_t channel) {
    const float extent = maximum - minimum;
    if (extent < support_radius * 2.0F) {
        throw std::runtime_error("terrain heightfield is too small for a raw backdrop sample");
    }
    const std::uint64_t first = static_cast<std::uint64_t>(std::ceil(support_radius / spacing));
    const std::uint64_t last =
        static_cast<std::uint64_t>(std::floor((extent - support_radius) / spacing));
    if (first > last) {
        throw std::runtime_error("terrain heightfield is too small for a raw backdrop sample");
    }
    const std::uint64_t count = last - first + 1U;
    const float random = cubey::procedural::random01(kRawPlacementSequenceSeed, kRawPlacementDomain,
                                                     sample_index, channel);
    const std::uint64_t offset = std::min(
        static_cast<std::uint64_t>(std::floor(random * static_cast<float>(count))), count - 1U);
    return minimum + static_cast<float>(first + offset) * spacing;
}

} // namespace

float terrain_backdrop_selected_support_radius(const TerrainBackdropPlacementRequest& request,
                                               float gradient_step_m) {
    validate_request(request);
    if (!std::isfinite(gradient_step_m) || gradient_step_m <= 0.0F) {
        throw std::runtime_error("invalid terrain backdrop gradient step");
    }
    return request.outer_radius_m + gradient_step_m;
}

float terrain_backdrop_centered_search_support_radius(
    const TerrainBackdropPlacementRequest& request, float gradient_step_m) {
    const float selected_support =
        terrain_backdrop_selected_support_radius(request, gradient_step_m);
    const float sampled_support =
        std::max(selected_support, request.placement.remote_distance_m + gradient_step_m);
    return request.placement.search_extent_m +
           terrain_directional_placement_maximum_refinement_offset_m() + sampled_support;
}

cubey::math::Vec2 terrain_backdrop_raw_sample_focus(const TerrainHeightSourceBounds& bounds,
                                                    float sample_spacing_m, float support_radius_m,
                                                    std::uint32_t sample_index) {
    validate_terrain_height_source_bounds(bounds);
    if (!std::isfinite(sample_spacing_m) || sample_spacing_m <= 0.0F ||
        !std::isfinite(support_radius_m) || support_radius_m < 0.0F) {
        throw std::runtime_error("invalid raw terrain backdrop sample domain");
    }
    const cubey::math::Vec2 focus{
        sampled_axis(bounds.minimum_xz.x, bounds.maximum_xz.x, sample_spacing_m, support_radius_m,
                     sample_index, 0U),
        sampled_axis(bounds.minimum_xz.y, bounds.maximum_xz.y, sample_spacing_m, support_radius_m,
                     sample_index, 1U),
    };
    if (!terrain_height_source_bounds_contains_disk(bounds, focus, support_radius_m)) {
        throw std::runtime_error("raw terrain backdrop sample exceeds source coverage");
    }
    return focus;
}

TerrainBackdropPlacementPlan
plan_terrain_backdrop_placement(const TerrainHeightSource& source,
                                const TerrainHeightSourceBounds& bounds,
                                const TerrainBackdropPlacementRequest& request) {
    validate_request(request);
    validate_terrain_height_source_bounds(bounds);
    const TerrainHeightSourceMetadata metadata = source.metadata();
    validate_terrain_height_source_metadata(metadata);

    TerrainBackdropPlacementPlan result{
        .mode = request.mode,
        .sample_index = request.sample_index,
        .centered_search_support_radius_m =
            terrain_backdrop_centered_search_support_radius(request, metadata.gradient_step_m),
        .selected_support_radius_m =
            terrain_backdrop_selected_support_radius(request, metadata.gradient_step_m),
    };

    TerrainDirectionalPlacementRequest placement_request = request.placement;
    placement_request.vertical_scale = request.vertical_scale;
    switch (request.mode) {
    case TerrainPlacementMode::Selected:
        if (!terrain_height_source_bounds_contains_disk(bounds, {0.0F, 0.0F},
                                                        result.centered_search_support_radius_m)) {
            throw std::runtime_error(
                "terrain heightfield does not cover the selected placement search");
        }
        result.placement = plan_terrain_directional_placement(source, placement_request);
        if (!result.placement.contract_satisfied) {
            throw std::runtime_error("terrain heightfield has no passing selected backdrop stage");
        }
        break;
    case TerrainPlacementMode::RawCenter:
        result.placement = evaluate_terrain_directional_placement(
            source, placement_request, terrain_height_source_bounds_center(bounds));
        break;
    case TerrainPlacementMode::RawSample:
        result.placement = evaluate_terrain_directional_placement(
            source, placement_request,
            terrain_backdrop_raw_sample_focus(bounds, metadata.gradient_step_m,
                                              result.selected_support_radius_m,
                                              request.sample_index));
        break;
    }

    if (!terrain_height_source_bounds_contains_disk(bounds, result.placement.source_focus_xz,
                                                    result.selected_support_radius_m)) {
        throw std::runtime_error("terrain heightfield does not cover the backdrop placement");
    }

    result.stage = plan_terrain_focused_backdrop_stage(
        source, result.placement, request.vertical_scale, request.stage, placement_request);
    if (!result.stage.contract_satisfied) {
        throw std::runtime_error("terrain backdrop placement does not satisfy camera clearance");
    }
    if (request.mode != TerrainPlacementMode::Selected) {
        result.stage.showcase_yaw_radians = 0.0F;
    }
    return result;
}

} // namespace cubey::projects::terrain
