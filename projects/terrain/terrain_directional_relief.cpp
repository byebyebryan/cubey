#include "terrain_directional_relief.h"

#include <cubey/procedural/noise.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] float smootherstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] cubey::procedural::CoherentDomainWarpConfig warp_config(
    const TerrainHeightSourceMetadata& metadata,
    const TerrainDirectionalReliefParameters& parameters) {
    return {
        .seed = static_cast<std::int32_t>(cubey::procedural::derive_seed(
            metadata.seed, "terrain.directional-backdrop.warp")),
        .frequency = 1.0F / parameters.warp_period_m,
        .warp_type = cubey::procedural::CoherentDomainWarpType::OpenSimplex2Reduced,
        .fractal_type = cubey::procedural::CoherentDomainWarpFractalType::Progressive,
        .octaves = parameters.warp_octaves,
        .lacunarity = 2.0F,
        .gain = 0.5F,
        .amplitude = parameters.warp_amplitude_m,
    };
}

} // namespace

void validate_terrain_directional_relief_parameters(
    const TerrainDirectionalReliefParameters& parameters) {
    const bool finite = std::isfinite(parameters.focus_xz.x) &&
                        std::isfinite(parameters.focus_xz.y) &&
                        std::isfinite(parameters.mountain_yaw_radians) &&
                        std::isfinite(parameters.floor_footprint_m) &&
                        std::isfinite(parameters.floor_relief_fraction) &&
                        std::isfinite(parameters.structure_footprint_m) &&
                        std::isfinite(parameters.broad_start_m) &&
                        std::isfinite(parameters.broad_full_m) &&
                        std::isfinite(parameters.detail_start_m) &&
                        std::isfinite(parameters.detail_full_m) &&
                        std::isfinite(parameters.warp_period_m) &&
                        std::isfinite(parameters.warp_amplitude_m);
    if (!finite || parameters.floor_footprint_m <= parameters.structure_footprint_m ||
        parameters.structure_footprint_m <= 0.0F || parameters.floor_relief_fraction < 0.0F ||
        parameters.floor_relief_fraction > 1.0F || parameters.broad_start_m < 0.0F ||
        parameters.broad_full_m <= parameters.broad_start_m ||
        parameters.detail_start_m < parameters.broad_start_m ||
        parameters.detail_full_m <= parameters.detail_start_m ||
        parameters.detail_full_m < parameters.broad_full_m || parameters.warp_period_m <= 0.0F ||
        parameters.warp_amplitude_m < 0.0F || parameters.warp_octaves == 0U ||
        parameters.warp_octaves > 8U) {
        throw std::runtime_error("invalid terrain directional relief parameters");
    }
}

TerrainDirectionalReliefSource::TerrainDirectionalReliefSource(
    const TerrainHeightSource& source, TerrainDirectionalReliefParameters parameters)
    : source_(source), parameters_(parameters), source_metadata_(source.metadata()) {
    validate_terrain_height_source_metadata(source_metadata_);
    validate_terrain_directional_relief_parameters(parameters_);
}

TerrainHeightSourceMetadata TerrainDirectionalReliefSource::metadata() const noexcept {
    return {
        .id = "terrain-directional-relief",
        .seed = source_metadata_.seed,
        .base_height_m = source_metadata_.base_height_m,
        .relief_scale_m = source_metadata_.relief_scale_m,
        .gradient_step_m = source_metadata_.gradient_step_m,
    };
}

float TerrainDirectionalReliefSource::sample_height(const TerrainQuery& query) const {
    return sample_composition(query).height_m;
}

TerrainDirectionalReliefSample TerrainDirectionalReliefSource::sample_composition(
    const TerrainQuery& query) const {
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain directional relief query");
    }
    const float source_height = source_.sample_height(query);
    TerrainQuery structure_query = query;
    structure_query.footprint_m =
        std::max(structure_query.footprint_m, parameters_.structure_footprint_m);
    const float structure_height = source_.sample_height(structure_query);
    TerrainQuery floor_query = query;
    floor_query.footprint_m = std::max(floor_query.footprint_m, parameters_.floor_footprint_m);
    const float filtered_floor = source_.sample_height(floor_query);
    const float floor_height =
        source_metadata_.base_height_m +
        parameters_.floor_relief_fraction * (filtered_floor - source_metadata_.base_height_m);

    const cubey::math::Vec2 relative = query.world_xz - parameters_.focus_xz;
    const cubey::procedural::CoherentDomainWarpConfig warp =
        warp_config(source_metadata_, parameters_);
    const cubey::procedural::CoherentWarp2D warped =
        cubey::procedural::domain_warp_2d(relative.x, relative.y, warp);
    const cubey::procedural::CoherentWarp2D center =
        cubey::procedural::domain_warp_2d(0.0F, 0.0F, warp);
    const cubey::math::Vec2 anchored{warped.x - center.x, warped.y - center.y};
    const cubey::math::Vec2 direction{std::sin(parameters_.mountain_yaw_radians),
                                      -std::cos(parameters_.mountain_yaw_radians)};
    const float directional_distance = anchored.x * direction.x + anchored.y * direction.y;
    const float broad_gate = smootherstep(parameters_.broad_start_m, parameters_.broad_full_m,
                                          directional_distance);
    const float detail_gate = smootherstep(parameters_.detail_start_m, parameters_.detail_full_m,
                                           directional_distance);
    const float height = floor_height + broad_gate * (structure_height - floor_height) +
                         detail_gate * (source_height - structure_height);
    return {
        .height_m = height,
        .source_height_m = source_height,
        .floor_height_m = floor_height,
        .structure_height_m = structure_height,
        .directional_distance_m = directional_distance,
        .broad_gate = broad_gate,
        .detail_gate = detail_gate,
    };
}

const TerrainDirectionalReliefParameters&
TerrainDirectionalReliefSource::parameters() const noexcept {
    return parameters_;
}

} // namespace cubey::projects::terrain
