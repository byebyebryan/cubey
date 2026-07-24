#pragma once

#include <cubey/terrain/terrain_backdrop_product.h>

namespace cubey::terrain {

struct TerrainBackdropSurfaceEnvelope {
    float nominal_local_height_m = 0.0F;
    float maximum_local_height_m = 0.0F;
    float footprint_radius_m = 0.0F;
};

[[nodiscard]] TerrainBackdropSurfaceEnvelope
terrain_backdrop_surface_envelope(const TerrainBackdropProduct& product, float footprint_radius_m);

} // namespace cubey::terrain
