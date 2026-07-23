#pragma once

#include "terrain_raster_climate_source.h"
#include "terrain_surface_model.h"

#include <cubey/procedural/artifact_cache.h>
#include <cubey/terrain/terrain_backdrop_product.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cubey::projects::terrain {

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

enum class TerrainProductPreparationSource : std::uint8_t {
    Cache,
    Generated,
};

struct TerrainProductCacheDiagnostics {
    TerrainProductPreparationSource source = TerrainProductPreparationSource::Generated;
    cubey::procedural::ProceduralArtifactCacheLoadOutcome lookup =
        cubey::procedural::ProceduralArtifactCacheLoadOutcome::Miss;
    bool stored = false;
    double load_milliseconds = 0.0;
    double decode_milliseconds = 0.0;
    double generation_milliseconds = 0.0;
    double encode_milliseconds = 0.0;
    double store_milliseconds = 0.0;
    std::filesystem::path path{};
    std::string diagnostic{};
};

struct PreparedProjectTerrainBackdropProduct {
    cubey::terrain::TerrainBackdropProduct product{};
    TerrainBackdropClimateDiagnostics climate{};
    TerrainProductCacheDiagnostics cache{};
};

[[nodiscard]] cubey::terrain::TerrainBackdropProduct make_project_terrain_backdrop_product(
    const cubey::terrain::TerrainBackdropProductRequest& request,
    const cubey::asset::TerrainHeightSource& source,
    TerrainSurfaceModel surface_model = TerrainSurfaceModel::MineralControl,
    const TerrainRasterClimateSource* climate_source = nullptr,
    TerrainBackdropClimateDiagnostics* climate_diagnostics = nullptr);

[[nodiscard]] std::vector<std::uint8_t>
encode_terrain_backdrop_climate_diagnostics(const TerrainBackdropClimateDiagnostics& diagnostics);
[[nodiscard]] TerrainBackdropClimateDiagnostics
decode_terrain_backdrop_climate_diagnostics(std::span<const std::uint8_t> payload);

[[nodiscard]] PreparedProjectTerrainBackdropProduct prepare_project_terrain_backdrop_product(
    cubey::procedural::ProceduralArtifactCache& cache,
    const cubey::terrain::TerrainBackdropProductRequest& request,
    const TerrainRasterHeightSource& source, TerrainSurfaceModel surface_model,
    const TerrainRasterClimateSource* climate_source, std::uint64_t placement_parameter_hash);

} // namespace cubey::projects::terrain
