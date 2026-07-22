#pragma once

#include "terrain_raster_climate_source.h"
#include "terrain_surface_model.h"

#include <cubey/render/terrain_backdrop_product.h>

#include <cstdint>

namespace cubey::projects::terrain {

using cubey::render::TerrainBackdropCenterMode;
using cubey::render::TerrainBackdropCenterSampling;
using cubey::render::TerrainBackdropDensityProfile;
using cubey::render::TerrainBackdropMeshDensity;
using cubey::render::TerrainBackdropProduct;
using cubey::render::TerrainBackdropProductInfo;
using cubey::render::TerrainBackdropProductRequest;
using cubey::render::TerrainBackdropProductDiagnostics;
using cubey::render::TerrainBackdropSectorBounds;
using cubey::render::TerrainBackdropSectorMesh;
using cubey::render::TerrainBackdropSurfaceChannels;
using cubey::render::TerrainBackdropSurfaceClassifier;
using cubey::render::TerrainBackdropSurfaceQuery;
using cubey::render::terrain_backdrop_density_profile;
using cubey::render::terrain_backdrop_mesh_density_from_name;
using cubey::render::terrain_backdrop_mesh_density_name;

struct TerrainBackdropClimateDiagnostics {
    std::uint64_t sample_count = 0U;
    float mean_temperature_c = 0.0F;
    float mean_temperature_stddev_c = 0.0F;
    float mean_precipitation_annual_mm = 0.0F;
    float mean_precipitation_cv = 0.0F;
    float mean_growing_season_days = 0.0F;
    float mean_thermal_growth = 0.0F;
    float mean_thermal_water_demand_proxy_mm = 0.0F;
    float mean_climate_moisture_ratio = 0.0F;
    float mean_seasonality_factor = 0.0F;
    float mean_effective_moisture = 0.0F;
    float mean_moisture_weight = 0.0F;
    float mean_cover_weight = 0.0F;
    float mean_annual_cold_potential = 0.0F;
    float mean_wet_snow_potential = 0.0F;
};

[[nodiscard]] TerrainBackdropProduct make_project_terrain_backdrop_product(
    const TerrainBackdropProductRequest& request, const TerrainHeightSource& source,
    TerrainSurfaceModel surface_model = TerrainSurfaceModel::MineralControl,
    const TerrainRasterClimateSource* climate_source = nullptr,
    TerrainBackdropClimateDiagnostics* climate_diagnostics = nullptr);

} // namespace cubey::projects::terrain
