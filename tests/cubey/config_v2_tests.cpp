#include "../../examples/headless_cube/headless_cube_config.h"
#include "../../examples/instanced_cubes/instanced_cubes_config.h"
#include "../../examples/material_cubes/material_cubes_config.h"
#include "../../examples/particle_cubes/particle_cubes_config.h"
#include "../../examples/shadow_cube/shadow_cube_config.h"
#include "../../examples/spinning_cube/spinning_cube_config.h"
#include "../../examples/textured_cube/textured_cube_config.h"
#include "../../projects/fractal_2d/fractal_2d_config.h"
#include "../../projects/gltf_viewer/gltf_viewer_config.h"
#include "../../projects/pbr_furnace/pbr_furnace_config.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

struct Args {
    std::vector<std::string> values;
    std::vector<char*> pointers;

    explicit Args(std::initializer_list<std::string> arguments) : values{"config-v2-test"} {
        values.insert(values.end(), arguments.begin(), arguments.end());
        pointers.reserve(values.size());
        for (std::string& value : values) {
            pointers.push_back(value.data());
        }
    }
};

template <typename Config, typename ParseFn>
Config parse(ParseFn parse_fn, std::initializer_list<std::string> arguments) {
    Args args(arguments);
    return parse_fn(static_cast<int>(args.pointers.size()), args.pointers.data(), nullptr);
}

void require_common_only(const cubey::config::Schema& schema, const char* name) {
    require(schema.find("debug_view") == nullptr, name);
    require(schema.find("pbr.environment") == nullptr, name);
    require(schema.find("planet.camera_mode") == nullptr, name);
}

void test_common_facade_defaults_and_host_normalization() {
    const auto config = parse<cubey::examples::headless_cube::HeadlessCubeConfig>(
        cubey::examples::headless_cube::parse_headless_cube_config,
        {"--headless", "--capture", "video", "--no-validation", "--require-validation"});
    require(config.common.width == 1280U && config.common.height == 720U,
            "common-only facade should preserve host defaults");
    require(config.common.frames == 300U && config.common.output_path == "cubey-output.mp4",
            "common-only facade should preserve video normalization");
    require(config.common.validation && config.common.require_validation,
            "require-validation should restore validation after an earlier disable");

    const auto named = parse<cubey::examples::spinning_cube::SpinningCubeConfig>(
        cubey::examples::spinning_cube::parse_spinning_cube_config,
        {"--width", "640", "--height", "480", "--set", "width=800"});
    require(named.common.width == 800U && named.common.height == 480U,
            "deferred --set should override named common flags");

    const auto textured = parse<cubey::examples::textured_cube::TexturedCubeConfig>(
        cubey::examples::textured_cube::parse_textured_cube_config, {"--print-frame-stats"});
    require(textured.common.print_frame_stats,
            "common-only facade should preserve named bool flags");
}

void test_configured_app_sets_default_title_and_returns_runner_status() {
    Args args({});
    std::string observed_title;

    const int status = cubey::host::run_configured_app(
        static_cast<int>(args.pointers.size()), args.pointers.data(),
        {.app_name = "config-v2-test", .default_title = "cubey config v2 test"},
        cubey::examples::headless_cube::parse_headless_cube_config,
        [&observed_title](const cubey::examples::headless_cube::HeadlessCubeConfig& config) {
            observed_title = config.common.title;
            return 7;
        });

    require(status == 7, "configured app helper should return runner status");
    require(observed_title == "cubey config v2 test",
            "configured app helper should apply the target default title");
}

void test_common_facade_file_precedence_and_scope() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey-config-v2-common.json";
    {
        std::ofstream output(path);
        output << R"({"width":700,"height":500,"validation":false})";
    }
    const auto config = parse<cubey::examples::instanced_cubes::InstancedCubesConfig>(
        cubey::examples::instanced_cubes::parse_instanced_cubes_config,
        {"--config", path.string(), "--width", "750", "--set", "width=900"});
    require(config.common.width == 900U && config.common.height == 500U &&
                !config.common.validation,
            "common facade should apply file, named, then --set precedence");
    std::filesystem::remove(path);

    require_throws(
        [] {
            static_cast<void>(parse<cubey::examples::particle_cubes::ParticleCubesConfig>(
                cubey::examples::particle_cubes::parse_particle_cubes_config,
                {"--planet-view", "night"}));
        },
        "common-only facade should reject unrelated named project flags");

    const std::filesystem::path invalid =
        std::filesystem::temp_directory_path() / "cubey-config-v2-common-invalid.json";
    {
        std::ofstream output(invalid);
        output << R"({"pbr":{"exposure":1.0}})";
    }
    require_throws(
        [&] {
            static_cast<void>(parse<cubey::examples::shadow_cube::ShadowCubeConfig>(
                cubey::examples::shadow_cube::parse_shadow_cube_config,
                {"--config", invalid.string()}));
        },
        "common-only facade should reject unrelated JSON groups");
    std::filesystem::remove(invalid);
}

void test_common_facade_templates_cover_all_targets() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey-config-v2-template.json";
    {
        cubey::examples::headless_cube::HeadlessCubeConfig config;
        const auto schema = cubey::examples::headless_cube::headless_cube_config_schema(config);
        require_common_only(schema, "headless template schema should contain only common options");
        const nlohmann::json document = schema.template_json();
        require(document.contains("profile") && document.contains("capture"),
                "common template should include host profiling and capture options");
    }
    {
        cubey::examples::spinning_cube::SpinningCubeConfig config;
        const auto schema = cubey::examples::spinning_cube::spinning_cube_config_schema(config);
        require_common_only(schema, "spinning template schema should contain only common options");
    }
    {
        cubey::examples::textured_cube::TexturedCubeConfig config;
        const auto schema = cubey::examples::textured_cube::textured_cube_config_schema(config);
        require_common_only(schema, "textured template schema should contain only common options");
    }
    {
        cubey::examples::instanced_cubes::InstancedCubesConfig config;
        const auto schema = cubey::examples::instanced_cubes::instanced_cubes_config_schema(config);
        require_common_only(schema, "instanced template schema should contain only common options");
    }
    {
        cubey::examples::particle_cubes::ParticleCubesConfig config;
        const auto schema = cubey::examples::particle_cubes::particle_cubes_config_schema(config);
        require_common_only(schema, "particle template schema should contain only common options");
    }
    {
        cubey::examples::shadow_cube::ShadowCubeConfig config;
        const auto schema = cubey::examples::shadow_cube::shadow_cube_config_schema(config);
        require_common_only(schema, "shadow template schema should contain only common options");
    }
    {
        cubey::projects::fractal_2d::Fractal2dConfig config;
        const auto schema = cubey::projects::fractal_2d::fractal_2d_config_schema(config);
        require_common_only(schema, "fractal template schema should contain only common options");
    }
    {
        cubey::projects::pbr_furnace::PbrFurnaceConfig config;
        const auto schema = cubey::projects::pbr_furnace::pbr_furnace_config_schema(config);
        require_common_only(schema,
                            "PBR furnace template schema should contain only common options");
    }
    {
        static_cast<void>(parse<cubey::examples::headless_cube::HeadlessCubeConfig>(
            cubey::examples::headless_cube::parse_headless_cube_config,
            {"--write-config-template", path.string()}));
        std::ifstream input(path);
        nlohmann::json document;
        input >> document;
        require(!document.contains("debug_view") && !document.contains("pbr"),
                "common template output should not include project options");
    }
    std::filesystem::remove(path);
}

void test_material_cubes_facade_owns_debug_and_pbr() {
    const auto config = parse<cubey::examples::material_cubes::MaterialCubesConfig>(
        cubey::examples::material_cubes::parse_material_cubes_config,
        {"--debug-view", "metallic", "--environment", "studio.hdr", "--ibl-intensity", "2.0",
         "--environment-rotation-degrees", "90", "--exposure", "-1.0", "--set",
         "pbr.exposure=1.25"});
    require(config.debug_view == "metallic" && config.pbr.environment_path == "studio.hdr" &&
                config.pbr.ibl_intensity == 2.0F &&
                config.pbr.environment_rotation_degrees == 90.0F && config.pbr.exposure == 1.25F,
            "material facade should parse project-owned debug and PBR controls");

    cubey::examples::material_cubes::MaterialCubesConfig defaults;
    const auto schema = cubey::examples::material_cubes::material_cubes_config_schema(defaults);
    const nlohmann::json document = schema.template_json();
    require(document.contains("debug_view") && document.at("pbr").size() == 4U,
            "material template should expose exactly its live project options");
    require(document.at("pbr").at("ibl_intensity") == 1.0F,
            "material template should preserve the legacy IBL default");
    require_throws(
        [] {
            static_cast<void>(parse<cubey::examples::material_cubes::MaterialCubesConfig>(
                cubey::examples::material_cubes::parse_material_cubes_config,
                {"--planet-view", "night"}));
        },
        "material facade should reject unrelated project flags");
}

void test_material_and_gltf_share_static_ibl_contract() {
    const auto material = parse<cubey::examples::material_cubes::MaterialCubesConfig>(
        cubey::examples::material_cubes::parse_material_cubes_config,
        {"--environment", "studio.hdr", "--ibl-intensity", "2.0",
         "--environment-rotation-degrees", "90", "--exposure", "-1.0"});
    const auto gltf = parse<cubey::projects::gltf_viewer::GltfViewerProjectConfig>(
        cubey::projects::gltf_viewer::parse_gltf_viewer_project_config,
        {"--environment", "studio.hdr", "--ibl-intensity", "2.0",
         "--environment-rotation-degrees", "90", "--exposure", "-1.0",
         "--pbr-environment-source", "static"});
    require(material.pbr.environment_path.has_value() && gltf.pbr.environment_path.has_value() &&
                *material.pbr.environment_path == *gltf.pbr.environment_path &&
                material.pbr.ibl_intensity == gltf.pbr.ibl_intensity &&
                material.pbr.environment_rotation_degrees ==
                    gltf.pbr.environment_rotation_degrees &&
                material.pbr.exposure == gltf.pbr.exposure &&
                material.pbr.exposure_explicit == gltf.pbr.exposure_explicit,
            "material and glTF should compose identical static IBL startup semantics");
}

void test_pbr_furnace_is_common_only() {
    require_throws(
        [] {
            static_cast<void>(parse<cubey::projects::pbr_furnace::PbrFurnaceConfig>(
                cubey::projects::pbr_furnace::parse_pbr_furnace_config,
                {"--environment", "ignored.hdr"}));
        },
        "PBR furnace should reject unconsumed legacy PBR options");
}

} // namespace

int main() {
    try {
        test_common_facade_defaults_and_host_normalization();
        test_configured_app_sets_default_title_and_returns_runner_status();
        test_common_facade_file_precedence_and_scope();
        test_common_facade_templates_cover_all_targets();
        test_material_cubes_facade_owns_debug_and_pbr();
        test_material_and_gltf_share_static_ibl_contract();
        test_pbr_furnace_is_common_only();
    } catch (const std::exception& error) {
        return (std::fprintf(stderr, "config_v2_tests: %s\n", error.what()), 1);
    }
    return 0;
}
