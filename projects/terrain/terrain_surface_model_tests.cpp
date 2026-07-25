#include "terrain_surface_model.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

cubey::projects::terrain::TerrainSurfaceInputs low_plain() {
    return {
        .normalized_height = 0.12F,
        .slope = 0.02F,
        .normal_y = 0.98F,
        .concavity_m = 120.0F,
        .relief_scale_m = 3'500.0F,
    };
}

void test_mineral_control_preserves_existing_channels() {
    const auto weights = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::MineralControl, low_plain());
    require(weights.vegetation == 0.0F && weights.moisture == 0.0F,
            "mineral control must not introduce ecological channels");
    require(weights.rock >= 0.0F && weights.snow >= 0.0F &&
                weights.ambient_visibility >= 0.65F,
            "mineral control should retain bounded material channels");
}

void test_mineral_control_reserves_snow_for_upper_summits() {
    auto shoulder = low_plain();
    shoulder.normalized_height = 0.48F;
    shoulder.slope = 0.12F;
    shoulder.normal_y = 0.91F;
    const auto shoulder_weights = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::MineralControl, shoulder);

    auto summit = shoulder;
    summit.normalized_height = 0.88F;
    const auto summit_weights = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::MineralControl, summit);

    auto summit_face = summit;
    summit_face.slope = 0.62F;
    summit_face.normal_y = 0.34F;
    const auto face_weights = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::MineralControl, summit_face);

    require(shoulder_weights.snow == 0.0F,
            "mineral control should keep middle-elevation shoulders exposed");
    require(summit_weights.snow > 0.8F,
            "mineral control should retain snow on upper summits");
    require(face_weights.snow < 0.05F && face_weights.rock > 0.8F,
            "mineral control should expose steep summit faces as rock");
}

void test_landform_capacity_prefers_low_flat_terrain() {
    const auto plain = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::LandformTransition, low_plain());
    auto mountain_inputs = low_plain();
    mountain_inputs.normalized_height = 0.78F;
    mountain_inputs.slope = 0.42F;
    mountain_inputs.normal_y = 0.58F;
    mountain_inputs.concavity_m = -20.0F;
    const auto mountain = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::LandformTransition, mountain_inputs);
    require(plain.vegetation > 0.5F && mountain.vegetation < plain.vegetation * 0.1F,
            "landform transition should separate plains from steep mountains");
}

void test_climate_modulates_landform_capacity_continuously() {
    auto wet = low_plain();
    wet.climate = cubey::projects::terrain::TerrainClimateSample{
        .temperature_mean_c = 12.0F,
        .temperature_stddev_c = 7.0F,
        .precipitation_annual_mm = 1'000.0F,
        .precipitation_cv = 0.2F,
    };
    auto dry = wet;
    dry.climate->precipitation_annual_mm = 100.0F;
    auto cold = wet;
    cold.climate->temperature_mean_c = -8.0F;
    const auto wet_weights = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::ClimateTransition, wet);
    const auto dry_weights = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::ClimateTransition, dry);
    const auto cold_weights = cubey::projects::terrain::terrain_surface_weights(
        cubey::projects::terrain::TerrainSurfaceModel::ClimateTransition, cold);
    require(wet_weights.vegetation > dry_weights.vegetation &&
                wet_weights.vegetation > cold_weights.vegetation,
            "climate transition should suppress arid and cold vegetation");
}

void test_climate_potential_exposes_existing_empirical_proxies() {
    using namespace cubey::projects::terrain;
    const TerrainClimateSample climate{
        .temperature_mean_c = 12.0F,
        .temperature_stddev_c = 7.0F,
        .precipitation_annual_mm = 1'000.0F,
        .precipitation_cv = 0.2F,
    };
    const TerrainClimatePotential potential = terrain_climate_potential(climate);
    auto inputs = low_plain();
    inputs.climate = climate;
    const TerrainSurfaceWeights weights =
        terrain_surface_weights(TerrainSurfaceModel::ClimateTransition, inputs);

    require(potential.growing_season_days > 150.0F && potential.growing_season_days <= 365.0F,
            "temperate climate should expose a substantial growing season");
    require(potential.thermal_growth > 0.0F &&
                potential.thermal_water_demand_proxy_mm > 250.0F &&
                potential.climate_moisture_ratio > 0.0F,
            "climate diagnostics should expose the existing thermal and moisture proxies");
    require(std::abs(potential.seasonality_factor - 0.93F) < 1.0e-6F &&
                std::abs(potential.effective_moisture -
                         potential.climate_moisture_ratio * potential.seasonality_factor) <
                    1.0e-6F,
            "effective moisture should retain the existing seasonality penalty");
    require(weights.moisture == potential.moisture_weight,
            "surface output should consume the exposed moisture proxy without retuning");
}

void test_weights_remain_bounded() {
    auto inputs = low_plain();
    inputs.climate = cubey::projects::terrain::TerrainClimateSample{
        .temperature_mean_c = -2.0F,
        .temperature_stddev_c = 5.0F,
        .precipitation_annual_mm = 800.0F,
        .precipitation_cv = 0.4F,
    };
    for (const auto model : {cubey::projects::terrain::TerrainSurfaceModel::MineralControl,
                             cubey::projects::terrain::TerrainSurfaceModel::LandformTransition,
                             cubey::projects::terrain::TerrainSurfaceModel::ClimateTransition}) {
        const auto weights = cubey::projects::terrain::terrain_surface_weights(model, inputs);
        require(weights.rock >= 0.0F && weights.rock <= 1.0F && weights.snow >= 0.0F &&
                    weights.snow <= 1.0F && weights.vegetation >= 0.0F &&
                    weights.vegetation <= 1.0F && weights.moisture >= 0.0F &&
                    weights.moisture <= 1.0F &&
                    weights.rock + weights.snow + weights.vegetation <= 1.00001F,
                "surface weights must remain normalized and bounded");
    }
}

} // namespace

int main() {
    try {
        test_mineral_control_preserves_existing_channels();
        test_mineral_control_reserves_snow_for_upper_summits();
        test_landform_capacity_prefers_low_flat_terrain();
        test_climate_modulates_landform_capacity_continuously();
        test_climate_potential_exposes_existing_empirical_proxies();
        test_weights_remain_bounded();
        std::cout << "terrain surface model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain surface model tests failed: " << error.what() << '\n';
        return 1;
    }
}
