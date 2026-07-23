#pragma once

#include "terrain_raster_climate_source.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cubey::projects::terrain {

inline constexpr std::string_view kTerrainSurfaceModelFormulaVersion = "terrain-surface-model-v1";

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

struct TerrainClimatePotential {
    float growing_season_days = 0.0F;
    float thermal_growth = 0.0F;
    float thermal_water_demand_proxy_mm = 0.0F;
    float climate_moisture_ratio = 0.0F;
    float seasonality_factor = 0.0F;
    float effective_moisture = 0.0F;
    float moisture_weight = 0.0F;
    float cover_weight = 0.0F;
    float annual_cold_potential = 0.0F;
    float wet_snow_potential = 0.0F;
};

[[nodiscard]] TerrainClimatePotential
terrain_climate_potential(const TerrainClimateSample& climate);

[[nodiscard]] TerrainSurfaceWeights terrain_surface_weights(TerrainSurfaceModel model,
                                                            const TerrainSurfaceInputs& inputs);
[[nodiscard]] std::uint64_t terrain_surface_model_parameter_hash(TerrainSurfaceModel model);

} // namespace cubey::projects::terrain
