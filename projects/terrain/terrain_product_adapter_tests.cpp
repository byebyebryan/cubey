#include "terrain_product_adapter.h"

#include <cubey/procedural/artifact_cache.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifndef CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET
#error "CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET must be defined by the terrain test target"
#endif

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class CacheFixture {
  public:
    CacheFixture() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("cubey-project-terrain-cache-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
    }

    ~CacheFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root{};
};

class AnalyticSource final : public cubey::asset::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::asset::TerrainHeightSourceMetadata metadata() const noexcept override {
        return {
            .id = "terrain-product-adapter-test",
            .seed = 9012U,
            .base_height_m = 600.0F,
            .relief_scale_m = 1'600.0F,
            .gradient_step_m = 16.0F,
        };
    }

    [[nodiscard]] float sample_height(const cubey::asset::TerrainQuery& query) const override {
        return 1'200.0F + 520.0F * std::sin(query.world_xz.x / 5'200.0F) +
               340.0F * std::cos(query.world_xz.y / 3'800.0F) +
               120.0F * std::sin((query.world_xz.x + query.world_xz.y) / 1'300.0F);
    }
};

[[nodiscard]] cubey::terrain::TerrainBackdropProductRequest product_request() {
    return {
        .source_focus_xz = {4'000.0F, -8'000.0F},
        .density = cubey::terrain::TerrainBackdropMeshDensity::Low,
        .vertical_offset_m = -1'200.0F,
    };
}

void test_surface_models_preserve_geometry_and_change_only_surface_channels() {
    using namespace cubey::projects::terrain;
    const AnalyticSource source;
    const cubey::terrain::TerrainBackdropProduct mineral = make_project_terrain_backdrop_product(
        product_request(), source, TerrainSurfaceModel::MineralControl);
    const cubey::terrain::TerrainBackdropProduct landform = make_project_terrain_backdrop_product(
        product_request(), source, TerrainSurfaceModel::LandformTransition);

    require(mineral.diagnostics.geometry_hash == landform.diagnostics.geometry_hash,
            "surface adapters must preserve identical terrain geometry");
    require(mineral.diagnostics.content_hash != landform.diagnostics.content_hash,
            "surface adapters should produce distinct semantic products");
    require(mineral.diagnostics.source_sample_count == landform.diagnostics.source_sample_count &&
                mineral.diagnostics.render_triangle_count ==
                    landform.diagnostics.render_triangle_count,
            "surface adapters must preserve source and topology budgets");
    require(mineral.diagnostics.mean_vegetation == 0.0F &&
                landform.diagnostics.mean_vegetation > 0.0F,
            "landform transition should populate vegetation without changing geometry");
}

void test_climate_surface_requires_a_bound_climate_source() {
    using namespace cubey::projects::terrain;
    bool rejected = false;
    try {
        static_cast<void>(make_project_terrain_backdrop_product(
            product_request(), AnalyticSource{}, TerrainSurfaceModel::ClimateTransition));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "climate transition should reject an unbound climate source");
}

void test_climate_diagnostics_codec_round_trips_every_field() {
    using namespace cubey::projects::terrain;
    const TerrainBackdropClimateDiagnostics diagnostics{
        .sample_count = 123'456U,
        .mean_temperature_c = 12.5F,
        .mean_temperature_stddev_c = 7.25F,
        .mean_precipitation_annual_mm = 812.0F,
        .mean_precipitation_cv = 0.42F,
        .mean_growing_season_days = 238.0F,
        .mean_thermal_growth = 0.84F,
        .mean_thermal_water_demand_proxy_mm = 730.0F,
        .mean_climate_moisture_ratio = 1.1F,
        .mean_seasonality_factor = 0.87F,
        .mean_effective_moisture = 0.95F,
        .mean_moisture_weight = 0.72F,
        .mean_cover_weight = 0.64F,
        .mean_annual_cold_potential = 0.18F,
        .mean_wet_snow_potential = 0.55F,
    };
    const std::vector<std::uint8_t> encoded =
        encode_terrain_backdrop_climate_diagnostics(diagnostics);
    const TerrainBackdropClimateDiagnostics decoded =
        decode_terrain_backdrop_climate_diagnostics(encoded);
    require(decoded.sample_count == diagnostics.sample_count &&
                encode_terrain_backdrop_climate_diagnostics(decoded) == encoded,
            "terrain climate diagnostics codec should preserve every field");

    bool rejected = false;
    try {
        static_cast<void>(
            decode_terrain_backdrop_climate_diagnostics(std::span<const std::uint8_t>{}));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain climate diagnostics codec should reject malformed payloads");
}

void test_prepared_product_uses_the_shared_cache_on_a_warm_build() {
    using namespace cubey::projects::terrain;
    CacheFixture fixture;
    cubey::procedural::ProceduralArtifactCache cache({.root = fixture.root});
    const TerrainRasterHeightSource source(CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET);
    const PreparedProjectTerrainBackdropProduct cold = prepare_project_terrain_backdrop_product(
        cache, product_request(), source, TerrainSurfaceModel::LandformTransition, nullptr,
        0x12345678U);
    require(cold.cache.source == TerrainProductPreparationSource::Generated &&
                cold.cache.lookup == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Miss &&
                cold.cache.stored && std::filesystem::is_regular_file(cold.cache.path),
            "cold terrain preparation should generate and publish a cache entry");

    const PreparedProjectTerrainBackdropProduct warm = prepare_project_terrain_backdrop_product(
        cache, product_request(), source, TerrainSurfaceModel::LandformTransition, nullptr,
        0x12345678U);
    require(warm.cache.source == TerrainProductPreparationSource::Cache &&
                warm.cache.lookup == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Hit &&
                warm.cache.generation_milliseconds == 0.0 &&
                warm.product.diagnostics.content_hash == cold.product.diagnostics.content_hash &&
                warm.product.diagnostics.geometry_hash == cold.product.diagnostics.geometry_hash &&
                warm.climate.sample_count == 0U,
            "warm terrain preparation should decode the identical product without generation");
}

} // namespace

int main() {
    try {
        test_surface_models_preserve_geometry_and_change_only_surface_channels();
        test_climate_surface_requires_a_bound_climate_source();
        test_climate_diagnostics_codec_round_trips_every_field();
        test_prepared_product_uses_the_shared_cache_on_a_warm_build();
        std::cout << "terrain_product_adapter_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_product_adapter_tests: " << error.what() << '\n';
        return 1;
    }
}
