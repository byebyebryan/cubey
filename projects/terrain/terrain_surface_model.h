#pragma once

#include "terrain_raster_climate_source.h"

#include <cstdint>
#include <optional>

namespace cubey::projects::terrain {

enum class TerrainSurfaceModel : std::uint8_t {
    MineralControl,
    LandformTransition,
    ClimateTransition,
};

struct TerrainSurfaceInputs {
    float normalized_height = 0.0F;
    float slope = 0.0F;
    float normal_y = 1.0F;
    float concavity_m = 0.0F;
    float relief_scale_m = 1.0F;
    std::optional<TerrainClimateSample> climate{};
};

struct TerrainSurfaceWeights {
    float rock = 0.0F;
    float snow = 0.0F;
    float ambient_visibility = 1.0F;
    float vegetation = 0.0F;
    float moisture = 0.0F;
};

[[nodiscard]] TerrainSurfaceWeights terrain_surface_weights(TerrainSurfaceModel model,
                                                            const TerrainSurfaceInputs& inputs);

} // namespace cubey::projects::terrain
