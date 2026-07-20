#include "terrain_height_source.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::terrain {

void validate_terrain_height_source_metadata(const TerrainHeightSourceMetadata& metadata) {
    if (metadata.id.empty() || !std::isfinite(metadata.base_height_m) ||
        !std::isfinite(metadata.relief_scale_m) || metadata.relief_scale_m <= 0.0F ||
        !std::isfinite(metadata.gradient_step_m) || metadata.gradient_step_m <= 0.0F) {
        throw std::runtime_error("invalid terrain height source metadata");
    }
}

TerrainSample TerrainHeightSource::sample(const TerrainQuery& query) const {
    const TerrainHeightSourceMetadata source_metadata = metadata();
    validate_terrain_height_source_metadata(source_metadata);
    const float center = sample_height(query);
    const float step_m = std::max(source_metadata.gradient_step_m, query.footprint_m * 0.5F);
    TerrainQuery offset = query;
    offset.world_xz.x -= step_m;
    const float x0 = sample_height(offset);
    offset.world_xz.x += 2.0F * step_m;
    const float x1 = sample_height(offset);
    offset = query;
    offset.world_xz.y -= step_m;
    const float z0 = sample_height(offset);
    offset.world_xz.y += 2.0F * step_m;
    const float z1 = sample_height(offset);
    return {
        .height_m = center,
        .gradient_xz = {(x1 - x0) / (2.0F * step_m), (z1 - z0) / (2.0F * step_m)},
    };
}

} // namespace cubey::projects::terrain
