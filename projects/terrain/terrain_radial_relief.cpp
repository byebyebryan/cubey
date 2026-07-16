#include "terrain_radial_relief.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] float smootherstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

} // namespace

void validate_terrain_radial_relief_parameters(const TerrainRadialReliefParameters& parameters) {
    const bool finite =
        std::isfinite(parameters.focus_xz.x) && std::isfinite(parameters.focus_xz.y) &&
        std::isfinite(parameters.floor_footprint_m) &&
        std::isfinite(parameters.floor_relief_fraction) &&
        std::isfinite(parameters.structure_footprint_m) &&
        std::isfinite(parameters.detail_footprint_m) && std::isfinite(parameters.broad_start_m) &&
        std::isfinite(parameters.broad_full_m) && std::isfinite(parameters.detail_start_m) &&
        std::isfinite(parameters.detail_full_m);
    if (!finite || parameters.floor_footprint_m <= parameters.structure_footprint_m ||
        parameters.structure_footprint_m <= 0.0F || parameters.detail_footprint_m < 0.0F ||
        (parameters.detail_footprint_m > 0.0F &&
         parameters.detail_footprint_m >= parameters.structure_footprint_m) ||
        parameters.floor_relief_fraction < 0.0F || parameters.floor_relief_fraction > 1.0F ||
        parameters.broad_start_m < 0.0F || parameters.broad_full_m <= parameters.broad_start_m ||
        parameters.detail_start_m < parameters.broad_start_m ||
        parameters.detail_full_m <= parameters.detail_start_m ||
        parameters.detail_full_m < parameters.broad_full_m) {
        throw std::runtime_error("invalid terrain radial relief parameters");
    }
}

TerrainRadialReliefSource::TerrainRadialReliefSource(const TerrainHeightSource& source,
                                                     TerrainRadialReliefParameters parameters)
    : source_(source), parameters_(parameters), source_metadata_(source.metadata()) {
    validate_terrain_height_source_metadata(source_metadata_);
    validate_terrain_radial_relief_parameters(parameters_);
}

TerrainHeightSourceMetadata TerrainRadialReliefSource::metadata() const noexcept {
    return {
        .id = "terrain-radial-relief",
        .seed = source_metadata_.seed,
        .base_height_m = source_metadata_.base_height_m,
        .relief_scale_m = source_metadata_.relief_scale_m,
        .gradient_step_m = source_metadata_.gradient_step_m,
    };
}

float TerrainRadialReliefSource::sample_height(const TerrainQuery& query) const {
    return sample_composition(query).height_m;
}

TerrainRadialReliefSample
TerrainRadialReliefSource::sample_composition(const TerrainQuery& query) const {
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain radial relief query");
    }

    const float source_height = source_.sample_height(query);
    TerrainQuery structure_query = query;
    structure_query.footprint_m =
        std::max(structure_query.footprint_m, parameters_.structure_footprint_m);
    const float structure_height = source_.sample_height(structure_query);
    float detail_height = source_height;
    if (parameters_.detail_footprint_m > query.footprint_m) {
        TerrainQuery detail_query = query;
        detail_query.footprint_m = parameters_.detail_footprint_m;
        detail_height = source_.sample_height(detail_query);
    }
    TerrainQuery floor_query = query;
    floor_query.footprint_m = std::max(floor_query.footprint_m, parameters_.floor_footprint_m);
    const float filtered_floor = source_.sample_height(floor_query);
    const float floor_height =
        source_metadata_.base_height_m +
        parameters_.floor_relief_fraction * (filtered_floor - source_metadata_.base_height_m);

    const cubey::math::Vec2 relative = query.world_xz - parameters_.focus_xz;
    const float radial_distance = std::sqrt(relative.x * relative.x + relative.y * relative.y);
    const float broad_gate =
        smootherstep(parameters_.broad_start_m, parameters_.broad_full_m, radial_distance);
    const float detail_gate =
        smootherstep(parameters_.detail_start_m, parameters_.detail_full_m, radial_distance);
    const float height = floor_height + broad_gate * (structure_height - floor_height) +
                         detail_gate * (detail_height - structure_height);
    return {
        .height_m = height,
        .source_height_m = source_height,
        .floor_height_m = floor_height,
        .structure_height_m = structure_height,
        .detail_height_m = detail_height,
        .radial_distance_m = radial_distance,
        .broad_gate = broad_gate,
        .detail_gate = detail_gate,
    };
}

const TerrainRadialReliefParameters& TerrainRadialReliefSource::parameters() const noexcept {
    return parameters_;
}

} // namespace cubey::projects::terrain
