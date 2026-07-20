#include "terrain_config.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function> void require_throws(Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void test_defaults_publish_the_product_contract() {
    const cubey::RunConfig run_config;
    const std::filesystem::path default_path = "/tmp/cubey-terrain-default";
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config, default_path);
    require(config.heightfield_path == default_path && config.foreground_sphere &&
                config.placement == cubey::projects::terrain::TerrainPlacementMode::Selected &&
                config.placement_index == 0U && config.initial_foreground_height_m == 100.0F &&
                config.material == cubey::projects::terrain::TerrainMaterialMode::FilteredDetail &&
                config.shadows &&
                config.debug_view == cubey::projects::terrain::TerrainDebugView::Surface,
            "terrain defaults should select the canonical review product");
}

void test_supported_overrides_remain_narrow() {
    cubey::RunConfig run_config;
    run_config.terrain.heightfield_path = "/tmp/custom-heightfield.json";
    run_config.terrain.seed = 9012U;
    run_config.terrain.seed_set = true;
    run_config.terrain.placement = "raw-sample";
    run_config.terrain.placement_index = 9U;
    run_config.terrain.foreground_height_m = 500.0F;
    run_config.terrain.camera_preset = "backdrop";
    run_config.terrain.surface_detail = "flat";
    run_config.terrain.shadows = 0;
    run_config.terrain.backdrop_azimuth_degrees = -90.0F;
    run_config.terrain.backdrop_orbit_radius_m = 200.0F;
    run_config.terrain.backdrop_elevation_degrees = 24.0F;
    run_config.debug_view = "slope";
    const auto config = cubey::projects::terrain::terrain_runtime_config_from_run_config(
        run_config, "/tmp/default");
    require(config.heightfield_path == run_config.terrain.heightfield_path &&
                config.expected_seed == 9012U && !config.foreground_sphere &&
                config.placement == cubey::projects::terrain::TerrainPlacementMode::RawSample &&
                config.placement_index == 9U && config.initial_foreground_height_m == 500.0F &&
                config.material == cubey::projects::terrain::TerrainMaterialMode::Flat &&
                !config.shadows &&
                config.debug_view == cubey::projects::terrain::TerrainDebugView::Slope,
            "terrain should retain only product review overrides");
    require(std::abs(config.initial_azimuth_radians.value() + std::numbers::pi_v<float> * 0.5F) <
                    0.0001F &&
                config.initial_orbit_radius_m == 200.0F &&
                std::abs(config.initial_elevation_radians.value() -
                         24.0F * std::numbers::pi_v<float> / 180.0F) < 0.0001F,
            "terrain should convert supported camera overrides");
}

void test_retired_modes_fail_explicitly() {
    cubey::RunConfig retired;
    retired.terrain.render_path = "quality";
    require_throws(
        [&retired] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_run_config(
                retired, "/tmp/default"));
        },
        "retired terrain render paths should be rejected");

    cubey::RunConfig invalid_camera;
    invalid_camera.terrain.camera_preset = "ground";
    require_throws(
        [&invalid_camera] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_run_config(
                invalid_camera, "/tmp/default"));
        },
        "close terrain cameras should be rejected");

    cubey::RunConfig invalid_material;
    invalid_material.terrain.surface_detail = "layered";
    require_throws(
        [&invalid_material] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_run_config(
                invalid_material, "/tmp/default"));
        },
        "retired terrain materials should be rejected");

    cubey::RunConfig invalid_placement;
    invalid_placement.terrain.placement = "curated";
    require_throws(
        [&invalid_placement] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_run_config(
                invalid_placement, "/tmp/default"));
        },
        "unsupported terrain placement should be rejected");

    cubey::RunConfig invalid_foreground;
    invalid_foreground.terrain.foreground_height_m = 1.0F;
    require_throws(
        [&invalid_foreground] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_run_config(
                invalid_foreground, "/tmp/default"));
        },
        "unsupported terrain foreground height should be rejected");
}

} // namespace

int main() {
    try {
        test_defaults_publish_the_product_contract();
        test_supported_overrides_remain_narrow();
        test_retired_modes_fail_explicitly();
        std::cout << "terrain config tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain config tests failed: " << error.what() << '\n';
        return 1;
    }
}
