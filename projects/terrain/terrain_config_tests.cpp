#include "terrain_config.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using cubey::projects::terrain::TerrainCameraPreset;
using cubey::projects::terrain::TerrainDebugView;
using cubey::projects::terrain::TerrainMaterialMode;
using cubey::projects::terrain::TerrainPlacementMode;
using cubey::projects::terrain::TerrainProjectConfig;
using cubey::projects::terrain::TerrainStartupOptions;
using cubey::projects::terrain::TerrainSurfaceModel;

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

TerrainProjectConfig parse(std::initializer_list<std::string> arguments) {
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1U);
    storage.emplace_back("terrain");
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (std::string& argument : storage) {
        argv.push_back(argument.data());
    }
    return cubey::projects::terrain::parse_terrain_project_config(
        static_cast<int>(argv.size()), argv.data());
}

void test_defaults_and_explicit_default_paths() {
    const TerrainProjectConfig parsed = parse({});
    require(!parsed.debug_view.has_value() && !parsed.terrain.seed.has_value() &&
                !parsed.terrain.heightfield_path.has_value() &&
                !parsed.terrain.surface_fields_path.has_value(),
            "terrain parser defaults should retain absent optional assignments");

    TerrainProjectConfig resolved = parsed;
    const std::filesystem::path default_path = "/tmp/cubey-terrain-default";
    const std::filesystem::path default_surface = "/tmp/cubey-terrain-surface";
    cubey::projects::terrain::resolve_terrain_project_config(resolved, default_path,
                                                               default_surface);
    const auto& config = resolved.runtime;
    require(config.heightfield_path == default_path &&
                config.surface_fields_path == default_surface && config.foreground_sphere &&
                config.surface_model == TerrainSurfaceModel::MineralControl &&
                config.placement == TerrainPlacementMode::Selected &&
                config.placement_index == 0U && config.initial_foreground_height_m == 200.0F &&
                config.render_stride == 3U &&
                config.material == TerrainMaterialMode::FilteredDetail &&
                config.aerial_perspective_strength ==
                    cubey::projects::terrain::kTerrainDefaultAerialPerspectiveStrength &&
                config.shadows && config.debug_view == TerrainDebugView::Surface,
            "terrain defaults should select the canonical review product");

    TerrainStartupOptions explicit_heightfield;
    explicit_heightfield.heightfield_path = "/tmp/custom-heightfield";
    const auto explicit_config =
        cubey::projects::terrain::terrain_runtime_config_from_options(
            explicit_heightfield, TerrainDebugView::Surface, default_path, default_surface);
    require(explicit_config.heightfield_path == "/tmp/custom-heightfield" &&
                explicit_config.surface_fields_path.empty(),
            "an explicit heightfield should not inherit the default climate companion");

    TerrainStartupOptions empty_heightfield;
    empty_heightfield.heightfield_path = std::filesystem::path{};
    const auto empty_config = cubey::projects::terrain::terrain_runtime_config_from_options(
        empty_heightfield, TerrainDebugView::Surface, default_path, default_surface);
    require(empty_config.heightfield_path == default_path &&
                empty_config.surface_fields_path == default_surface,
            "an empty heightfield assignment should retain default path semantics");
}

void test_supported_overrides_and_aliases() {
    const TerrainProjectConfig parsed = parse({
        "--debug-view", "slope", "--terrain-heightfield", "/tmp/custom-heightfield.json",
        "--terrain-surface-fields", "/tmp/custom-surface-fields.json",
        "--terrain-surface-model", "climate-transition", "--terrain-seed", "9012",
        "--terrain-placement", "raw-sample", "--terrain-placement-index", "9",
        "--terrain-foreground-height", "500", "--terrain-camera-preset", "backdrop",
        "--terrain-surface-detail", "filtered-detail", "--terrain-aerial-perspective", "0.6",
        "--no-terrain-shadows", "--terrain-render-stride", "1", "--terrain-backdrop-azimuth",
        "-90", "--terrain-backdrop-orbit-radius", "1000", "--terrain-backdrop-elevation",
        "30", "--time-of-day-mode", "manual", "--sun-elevation", "20",
        "--cloud-weather-preset", "storm", "--no-clouds"});
    require(parsed.debug_view == TerrainDebugView::Slope &&
                parsed.terrain.heightfield_path == "/tmp/custom-heightfield.json" &&
                parsed.terrain.surface_fields_path == "/tmp/custom-surface-fields.json" &&
                parsed.terrain.seed == 9012U && parsed.terrain.camera_preset ==
                                                   TerrainCameraPreset::Backdrop &&
                parsed.terrain.surface_detail == TerrainMaterialMode::FilteredDetail &&
                parsed.terrain.shadows == false && parsed.atmosphere.time_of_day_mode == "manual" &&
                parsed.clouds.weather_preset == "storm" && parsed.clouds.enabled == false,
            "terrain parser should retain live typed overrides and aliases");

    TerrainProjectConfig resolved = parsed;
    cubey::projects::terrain::resolve_terrain_project_config(
        resolved, "/tmp/default", "/tmp/default-surface");
    const auto& config = resolved.runtime;
    require(config.heightfield_path == parsed.terrain.heightfield_path.value() &&
                config.surface_fields_path == parsed.terrain.surface_fields_path.value() &&
                config.surface_model == TerrainSurfaceModel::ClimateTransition &&
                config.expected_seed == 9012U && !config.foreground_sphere &&
                config.placement == TerrainPlacementMode::RawSample &&
                config.placement_index == 9U && config.initial_foreground_height_m == 500.0F &&
                config.render_stride == 1U && config.material == TerrainMaterialMode::FilteredDetail &&
                config.aerial_perspective_strength == 0.6F && !config.shadows &&
                config.debug_view == TerrainDebugView::Slope,
            "terrain resolver should retain only product review overrides");
    require(std::abs(config.initial_azimuth_radians.value() +
                     std::numbers::pi_v<float> * 0.5F) < 0.0001F &&
                config.initial_orbit_radius_m == 1'000.0F &&
                std::abs(config.initial_elevation_radians.value() -
                         30.0F * std::numbers::pi_v<float> / 180.0F) < 0.0001F,
            "terrain resolver should convert supported camera overrides");
}

void test_file_named_set_layering_and_seed_semantics() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey-terrain-config-v2-layering.json";
    {
        std::ofstream file(path);
        file << R"({"terrain":{"heightfield":"file-heightfield","foreground_height_m":100,"seed":0},"atmosphere":{"time_hours":7}})";
    }
    std::vector<std::string> storage{
        "terrain", "--config", path.string(), "--terrain-heightfield", "named-heightfield",
        "--set", "terrain.heightfield=set-heightfield", "--set", "terrain.seed=0"};
    std::vector<char*> argv;
    for (std::string& argument : storage) {
        argv.push_back(argument.data());
    }
    const TerrainProjectConfig parsed = cubey::projects::terrain::parse_terrain_project_config(
        static_cast<int>(argv.size()), argv.data());
    std::filesystem::remove(path);

    require(parsed.terrain.heightfield_path == "set-heightfield" &&
                parsed.terrain.foreground_height_m == 100.0F && parsed.terrain.seed.has_value() &&
                parsed.terrain.seed.value() == 0U && parsed.atmosphere.time_hours == 7.0F,
            "terrain config layering should apply file, named, then --set values");
    const auto resolved = cubey::projects::terrain::terrain_runtime_config_from_options(
        parsed.terrain, parsed.debug_view.value_or(TerrainDebugView::Surface), "/tmp/default",
        "/tmp/default-surface");
    require(resolved.expected_seed.has_value() && resolved.expected_seed.value() == 0U,
            "terrain seed zero should remain an explicit expected seed");
}

void test_template_scope_and_retired_rejection() {
    TerrainProjectConfig config;
    const auto schema = cubey::projects::terrain::terrain_project_config_schema(config);
    const auto document = schema.template_json();
    require(document.contains("common") == false && document.contains("terrain") &&
                document.contains("atmosphere") && document.contains("clouds") &&
                document.at("terrain").contains("heightfield") &&
                document.at("terrain").contains("surface_fields") &&
                !document.at("terrain").contains("preset") &&
                !document.at("terrain").contains("weathering") &&
                !document.at("terrain").contains("study_field"),
            "terrain template should expose only active and shared schema groups");
    require(schema.find("terrain.preset") == nullptr &&
                schema.find("terrain.weathering") == nullptr &&
                schema.find("terrain.backdrop_profile") == nullptr &&
                schema.find("ocean.map_size") == nullptr,
            "terrain schema should omit retired and unrelated options");
    require_throws([&] { schema.set("terrain.preset", "mountain"); },
                   "retired terrain options should be unknown");
    require_throws([&] { schema.set("terrain.weathering", "local"); },
                   "retired weathering options should be unknown");
    require_throws([&] { schema.set("smoke.injectors", "2"); },
                   "unrelated project options should be unknown");
    require_throws(
        [&] { parse({"--terrain-preset", "mountain"}); },
        "retired terrain CLI options should be rejected by the typed parser");
    require_throws(
        [&] { parse({"--terrain-camera-preset", "ground"}); },
        "non-product terrain camera presets should be rejected");
}

void test_shared_environment_validation_and_defaults() {
    TerrainProjectConfig config;
    const auto schema = cubey::projects::terrain::terrain_project_config_schema(config);
    require_throws([&] { schema.set("atmosphere.sun_elevation_degrees", "91"); },
                   "terrain atmosphere range should reject invalid sun elevation");
    require_throws([&] { schema.set("clouds.view_samples", "5"); },
                   "terrain cloud range should reject five view samples");
    require_throws(
        [&] { parse({"--time-of-day-mode", "solar", "--sun-elevation", "20"}); },
        "terrain parser should retain atmosphere manual/solar conflict validation");
    require_throws([&] { parse({"--cloud-view-samples", "3"}); },
                   "terrain parser should retain shared cloud validation");

    const cubey::AtmosphereEnvironmentOptions defaults;
    const auto atmosphere = cubey::projects::terrain::terrain_atmosphere_state_from_options(defaults);
    require(atmosphere.solar_time_enabled &&
                std::abs(atmosphere.environment.time_of_day.time_hours - 9.0F) < 0.0001F,
            "terrain atmosphere defaults should use the nine-hour solar review time");

    cubey::AtmosphereEnvironmentOptions manual;
    manual.time_of_day_mode = "manual";
    manual.sun_elevation_degrees = 15.0F;
    const auto manual_state =
        cubey::projects::terrain::terrain_atmosphere_state_from_options(manual);
    require(!manual_state.solar_time_enabled &&
                manual_state.environment.sun_elevation_degrees == 15.0F,
            "terrain atmosphere helper should preserve explicit manual sun settings");
}

void test_runtime_validation_and_diagnostics() {
    using cubey::projects::terrain::terrain_debug_view_from_name;
    using cubey::projects::terrain::terrain_debug_view_name;
    require(terrain_debug_view_from_name("ambient-light") == TerrainDebugView::AmbientLighting &&
                terrain_debug_view_name(TerrainDebugView::AmbientLighting) == "ambient-light" &&
                terrain_debug_view_from_name("direct-light") == TerrainDebugView::DirectLighting &&
                terrain_debug_view_name(TerrainDebugView::DirectLighting) == "direct-light",
            "terrain lighting diagnostics should retain stable runtime aliases");

    TerrainStartupOptions invalid_foreground;
    invalid_foreground.foreground_height_m = 1.0F;
    require_throws(
        [&] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_options(
                invalid_foreground, TerrainDebugView::Surface, "/tmp/default"));
        },
        "terrain runtime validation should retain the effective [2, 1000] foreground range");

    TerrainStartupOptions invalid_orbit;
    invalid_orbit.backdrop_orbit_radius_m = 1'001.0F;
    require_throws(
        [&] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_options(
                invalid_orbit, TerrainDebugView::Surface, "/tmp/default"));
        },
        "terrain runtime validation should reject an oversized orbit radius");

    TerrainStartupOptions missing_climate;
    missing_climate.surface_model = TerrainSurfaceModel::ClimateTransition;
    require_throws(
        [&] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_options(
                missing_climate, TerrainDebugView::Surface, "/tmp/default"));
        },
        "climate terrain should require an explicit or default companion field");

    TerrainStartupOptions invalid_surface_detail;
    require_throws(
        [&] {
            static_cast<void>(cubey::projects::terrain::terrain_runtime_config_from_options(
                invalid_surface_detail, TerrainDebugView::Surface, "/tmp/default"));
            static_cast<void>(cubey::projects::terrain::terrain_material_mode_from_name("layered"));
        },
        "unsupported terrain materials should be rejected");
}

} // namespace

int main() {
    try {
        test_defaults_and_explicit_default_paths();
        test_supported_overrides_and_aliases();
        test_file_named_set_layering_and_seed_semantics();
        test_template_scope_and_retired_rejection();
        test_shared_environment_validation_and_defaults();
        test_runtime_validation_and_diagnostics();
        std::cout << "terrain config tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain config tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
