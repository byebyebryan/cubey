#include "gltf_viewer_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Fn> void require_throws(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_defaults_named_flags_and_typed_runtime() {
    char arg0[] = "gltf_viewer";
    char arg1[] = "--debug-view";
    char arg2[] = "metallic";
    char arg3[] = "--ocean-map-size";
    char arg4[] = "128";
    char arg5[] = "--ocean-sea-state";
    char arg6[] = "calm";
    char arg7[] = "--no-ocean-backdrop";
    char arg8[] = "--cloud-weather-preset";
    char arg9[] = "storm";
    char arg10[] = "--no-clouds";
    char arg11[] = "--terrain-surface-detail";
    char arg12[] = "flat";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6,
                    arg7, arg8, arg9, arg10, arg11, arg12};
    const auto config = cubey::projects::gltf_viewer::parse_gltf_viewer_project_config(
        static_cast<int>(std::size(argv)), argv);
    require(config.common.width == 1280U && config.common.height == 720U,
            "glTF common defaults should remain stable");
    require(config.debug_view == "metallic", "glTF debug view should bind");
    require(config.ocean.map_size == 128U && config.ocean.sea_state == "calm",
            "glTF ocean startup options should bind");
    require(config.ocean.backdrop.has_value() && !*config.ocean.backdrop,
            "glTF negative ocean backdrop should bind");
    require(config.clouds.weather_preset == "storm" && config.clouds.enabled.has_value() &&
                !*config.clouds.enabled,
            "glTF cloud options should bind aliases and negative bools");
    require(config.terrain.surface_detail == "flat", "glTF terrain option should bind");
    const auto ocean = cubey::projects::gltf_viewer::gltf_viewer_ocean_config_from_options(config);
    require(ocean.map_size == 128U && ocean.sea_state == cubey::render::OceanSeaState::Calm,
            "glTF typed ocean runtime should consume startup options");
    require(ocean.render_view == cubey::render::OceanRenderView::Final &&
                ocean.cloud_reflection_source == cubey::render::OceanCloudReflectionSource::CachedEnvironment,
            "glTF ocean backdrop should retain final/cached legacy semantics");
}

void test_set_json_template_and_unknown_scope() {
    char arg0[] = "gltf_viewer";
    char arg1[] = "--ocean-map-size";
    char arg2[] = "128";
    char arg3[] = "--set";
    char arg4[] = "ocean.map_size=256";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4};
    const auto config = cubey::projects::gltf_viewer::parse_gltf_viewer_project_config(5, argv);
    require(config.ocean.map_size == 256U, "glTF --set should override named options");

    cubey::projects::gltf_viewer::GltfViewerProjectConfig json_config;
    auto schema = cubey::projects::gltf_viewer::gltf_viewer_project_config_schema(json_config);
    schema.apply_json({{"gltf", {{"animation_index", 3}}},
                       {"ocean", {{"map_size", 128}}},
                       {"pbr", {{"environment_source", "static"}}}});
    require(json_config.gltf.animation_index == 3U && json_config.ocean.map_size == 128U &&
                json_config.pbr.environment_source == "static",
            "glTF JSON paths should bind");

    const auto path = std::filesystem::temp_directory_path() / "cubey-gltf-viewer-template-v2.json";
    schema.write_template(path);
    std::ifstream file(path);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::filesystem::remove(path);
    require(text.find("gltf") != std::string::npos && text.find("ocean") != std::string::npos &&
                text.find("terrain") != std::string::npos && text.find("smoke") == std::string::npos,
            "glTF template should expose only owned groups");
    const auto document = schema.template_json();
    require(document.at("pbr").at("environment").is_null() &&
                document.at("pbr").at("exposure").is_null() &&
                document.at("ocean").at("backdrop").is_null(),
            "glTF template should preserve absent optional PBR and backdrop values as null");
    bool rejected = false;
    try {
        schema.apply_json({{"smoke", {{"injectors", 1}}}});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "glTF schema should reject unrelated options");
}

void test_shared_schema_validation_and_scope() {
    cubey::projects::gltf_viewer::GltfViewerProjectConfig config;
    const auto schema = cubey::projects::gltf_viewer::gltf_viewer_project_config_schema(config);
    require_throws([&] { schema.set("atmosphere.sun_elevation_degrees", "91"); },
                   "glTF atmosphere range should reject an invalid sun elevation");
    require_throws([&] { schema.set("clouds.view_samples", "5"); },
                   "glTF cloud range should reject five view samples");
    require_throws([&] { schema.set("ocean.mesh_cells", "31"); },
                   "glTF ocean range should reject undersized meshes");
    require_throws([&] { schema.set("pbr.exposure", "4.1"); },
                   "glTF shared PBR exposure should enforce its upper range");
    require(schema.find_by_cli_name("--play-time") == nullptr,
            "glTF atmosphere should not invent a play-time alias");
    require(schema.find("ocean.cloud_environment_extent") == nullptr,
            "glTF should omit ocean-only cloud reflection controls");

    const char* invalid_samples[] = {"gltf_viewer", "--cloud-view-samples", "3"};
    require_throws(
        [&] {
            static_cast<void>(cubey::projects::gltf_viewer::parse_gltf_viewer_project_config(
                3, const_cast<char**>(invalid_samples)));
        },
        "glTF parser should reject the unsupported three-sample cloud mode");
}

} // namespace

int main() {
    try {
        test_defaults_named_flags_and_typed_runtime();
        test_set_json_template_and_unknown_scope();
        test_shared_schema_validation_and_scope();
    } catch (const std::exception& error) {
        std::cerr << "gltf_viewer_config_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
