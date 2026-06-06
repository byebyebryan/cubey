#include <cubey/core/config_options.h>
#include <cubey/core/run_config.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

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

std::filesystem::path temp_config_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

std::filesystem::path source_root_path() {
    return std::filesystem::path(CUBEY_SOURCE_DIR);
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to write test file");
    }
    output << text;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to read test file");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

void require_contains(std::string_view text, std::string_view needle, const char* message) {
    require(contains(text, needle), message);
}

void require_not_contains(std::string_view text, std::string_view needle, const char* message) {
    require(!contains(text, needle), message);
}

const cubey::ConfigOptionDescriptor& require_option(std::string_view path) {
    if (const cubey::ConfigOptionDescriptor* option = cubey::find_run_config_option(path)) {
        return *option;
    }
    throw std::runtime_error("missing config descriptor for " + std::string(path));
}

} // namespace

void test_run_config_parses_png_output_path() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 9> output_flag{'-', '-', 'o', 'u', 't', 'p', 'u', 't', '\0'};
    std::array<char, 29> output_value{'/', 't', 'm', 'p', '/', 'c', 'u', 'b', 'e', 'y',
                                      '-', 'h', 'e', 'a', 'd', 'l', 'e', 's', 's', '-',
                                      't', 'e', 's', 't', '.', 'p', 'n', 'g', '\0'};
    std::array<char*, 3> argv{program.data(), output_flag.data(), output_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.output_path == output_value.data(),
            "run config should preserve PNG output path");
}

void test_run_config_parses_video_capture_defaults() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 11> headless_flag{'-', '-', 'h', 'e', 'a', 'd', 'l', 'e', 's', 's', '\0'};
    std::array<char, 10> capture_flag{'-', '-', 'c', 'a', 'p', 't', 'u', 'r', 'e', '\0'};
    std::array<char, 6> capture_value{'v', 'i', 'd', 'e', 'o', '\0'};
    std::array<char*, 4> argv{program.data(), headless_flag.data(), capture_flag.data(),
                              capture_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.capture_mode == cubey::CaptureMode::Video,
            "video capture flag should select video capture mode");
    require(config.fps == 60, "video capture should default to 60 fps");
    require(config.frames == 300, "video capture should default to 300 frames");
    require(config.output_path == "cubey-output.mp4",
            "video capture should default to an mp4 output path");
}

void test_run_config_preserves_explicit_video_capture_timing_and_output() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 11> headless_flag{'-', '-', 'h', 'e', 'a', 'd', 'l', 'e', 's', 's', '\0'};
    std::array<char, 10> capture_flag{'-', '-', 'c', 'a', 'p', 't', 'u', 'r', 'e', '\0'};
    std::array<char, 6> capture_value{'v', 'i', 'd', 'e', 'o', '\0'};
    std::array<char, 9> frames_flag{'-', '-', 'f', 'r', 'a', 'm', 'e', 's', '\0'};
    std::array<char, 3> frames_value{'4', '2', '\0'};
    std::array<char, 6> fps_flag{'-', '-', 'f', 'p', 's', '\0'};
    std::array<char, 3> fps_value{'2', '4', '\0'};
    std::array<char, 9> output_flag{'-', '-', 'o', 'u', 't', 'p', 'u', 't', '\0'};
    std::array<char, 21> output_value{'/', 't', 'm', 'p', '/', 'c', 'u', 'b', 'e', 'y', '-',
                                      'v', 'i', 'd', 'e', 'o', '.', 'm', 'p', '4', '\0'};
    std::array<char*, 10> argv{program.data(),       headless_flag.data(), capture_flag.data(),
                               capture_value.data(), frames_flag.data(),   frames_value.data(),
                               fps_flag.data(),      fps_value.data(),     output_flag.data(),
                               output_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.capture_mode == cubey::CaptureMode::Video,
            "video capture flag should select video capture mode");
    require(config.frames == 42, "video capture should preserve explicit frame count");
    require(config.fps == 24, "video capture should preserve explicit fps");
    require(config.output_path == output_value.data(),
            "video capture should preserve explicit output path");
}

void test_run_config_rejects_invalid_capture_options() {
    {
        std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
        std::array<char, 10> capture_flag{'-', '-', 'c', 'a', 'p', 't', 'u', 'r', 'e', '\0'};
        std::array<char, 4> capture_value{'g', 'i', 'f', '\0'};
        std::array<char*, 3> argv{program.data(), capture_flag.data(), capture_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject unknown capture modes");
    }
    {
        std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
        std::array<char, 11> headless_flag{'-', '-', 'h', 'e', 'a', 'd', 'l', 'e', 's', 's', '\0'};
        std::array<char, 6> fps_flag{'-', '-', 'f', 'p', 's', '\0'};
        std::array<char, 2> fps_value{'0', '\0'};
        std::array<char*, 4> argv{program.data(), headless_flag.data(), fps_flag.data(),
                                  fps_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject zero fps");
    }
    {
        std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
        std::array<char, 10> capture_flag{'-', '-', 'c', 'a', 'p', 't', 'u', 'r', 'e', '\0'};
        std::array<char, 6> capture_value{'v', 'i', 'd', 'e', 'o', '\0'};
        std::array<char*, 3> argv{program.data(), capture_flag.data(), capture_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject video capture without headless mode");
    }
}

void test_run_config_descriptors_have_help_text() {
    bool saw_ocean = false;
    bool saw_atmosphere = false;
    bool saw_profile = false;
    bool saw_smoke = false;
    bool saw_water3d = false;
    for (const cubey::ConfigOptionDescriptor& option : cubey::run_config_option_descriptors()) {
        require(!option.path.empty(), "config descriptor path should not be empty");
        require(!option.label.empty(), "config descriptor label should not be empty");
        require(!option.group_path.empty(), "config descriptor group should not be empty");
        require(!option.help.empty(), "config descriptor help should not be empty");
        if (option.path == "ocean.map_size") {
            saw_ocean = true;
        }
        if (option.path == "atmosphere.time_of_day_mode") {
            saw_atmosphere = true;
        }
        if (option.path == "profile.output") {
            saw_profile = true;
        }
        if (option.path == "smoke.pressure_solver") {
            saw_smoke = true;
        }
        if (option.path == "water3d.p2g_mode") {
            saw_water3d = true;
        }
    }
    require(saw_ocean, "config descriptors should include active ocean controls");
    require(saw_atmosphere, "config descriptors should include atmosphere controls");
    require(saw_profile, "config descriptors should include profiling controls");
    require(saw_smoke, "config descriptors should include smoke controls");
    require(saw_water3d, "config descriptors should include water 3D controls");
}

void test_run_config_descriptor_cli_names_are_unique() {
    const std::span<const cubey::ConfigOptionDescriptor> options =
        cubey::run_config_option_descriptors();
    for (std::size_t i = 0; i < options.size(); ++i) {
        const cubey::ConfigOptionDescriptor& option = options[i];
        require(!option.cli_name.empty(), "config descriptor CLI name should not be empty");
        require(cubey::find_run_config_option_by_cli_name(option.cli_name) == &option,
                "config descriptor positive CLI lookup should resolve the option");
        if (!option.negative_cli_name.empty()) {
            require(option.type == cubey::ConfigOptionType::Bool,
                    "only bool config descriptors should expose negative CLI names");
            require(cubey::find_run_config_option_by_cli_name(option.negative_cli_name) == &option,
                    "config descriptor negative CLI lookup should resolve the option");
            require(option.negative_cli_name != option.cli_name,
                    "config descriptor positive and negative CLI names should differ");
        }

        for (std::size_t j = i + 1U; j < options.size(); ++j) {
            const cubey::ConfigOptionDescriptor& other = options[j];
            require(option.cli_name != other.cli_name,
                    "config descriptor positive CLI names should be unique");
            if (!option.negative_cli_name.empty()) {
                require(option.negative_cli_name != other.cli_name,
                        "config descriptor negative CLI name should not duplicate a positive name");
                require(option.negative_cli_name != other.negative_cli_name,
                        "config descriptor negative CLI names should be unique");
            }
            if (!other.negative_cli_name.empty()) {
                require(option.cli_name != other.negative_cli_name,
                        "config descriptor positive CLI name should not duplicate a negative name");
            }
        }
    }
}

void test_run_config_promoted_flags_are_not_explicit_parser_branches() {
    const std::string source = read_text_file(source_root_path() / "src/cubey/core/run_config.cpp");

    require_contains(source, "find_run_config_option_by_cli_name(arg)",
                     "run config parser should route ordinary CLI flags through descriptors");

    constexpr std::array promoted_flags{
        "--grid-width",         "--profile-output",        "--profile-diagnostic-interval",
        "--smoke-injectors",    "--smoke-pressure-solver", "--pyro-sources",
        "--pyro-source-radius", "--shadow-grid-width",     "--water2d-transfer",
        "--water2d-hose",       "--water3d-transfer",      "--water3d-p2g-mode",
        "--water3d-whitewater", "--ocean-field-precision", "--planet-max-lod-level",
        "--planet-lod-hysteresis", "--planet-time-hours", "--planet-camera-mode",
    };
    for (std::string_view flag : promoted_flags) {
        const std::string explicit_branch = "arg == \"" + std::string(flag) + "\"";
        require_not_contains(source, explicit_branch,
                             "promoted CLI flags should not regain explicit parser branches");
    }
}

void test_run_config_descriptors_cover_project_control_paths() {
    constexpr std::array project_control_paths{
        "grid.width",
        "grid.height",
        "grid.depth",
        "profile.output",
        "profile.warmup_frames",
        "profile.diagnostics",
        "profile.diagnostic_interval",
        "pbr.environment_source",
        "pbr.ibl_intensity",
        "pbr.exposure",
        "ocean.map_size",
        "ocean.cascade",
        "ocean.spectral_domains",
        "ocean.terrain_fields",
        "planet.radius_m",
        "planet.atmosphere_height_m",
        "planet.camera_altitude_m",
        "planet.patches_per_face",
        "planet.patch_resolution",
        "planet.max_lod_level",
        "planet.lod_target_edge_px",
        "planet.lod_hysteresis",
        "planet.wire_overlay",
        "planet.skirts_enabled",
        "planet.skirt_depth_scale",
        "planet.terrain_enabled",
        "planet.terrain_height_scale_m",
        "planet.terrain_noise_scale",
        "planet.terrain_seed",
        "planet.day_of_year",
        "planet.time_hours",
        "planet.time_speed_hours_per_second",
        "planet.time_paused",
        "planet.camera_mode",
        "terrain.cell_size",
        "terrain.sea_level",
        "terrain.water_surface",
        "atmosphere.time_of_day_mode",
        "atmosphere.sun_elevation_degrees",
        "atmosphere.time_speed_hours_per_second",
        "atmosphere.auto_exposure",
        "atmosphere.moon",
        "smoke.injectors",
        "smoke.pressure_iterations",
        "smoke.pressure_solver",
        "smoke.injector_force",
        "smoke.injector_orbit_radius",
        "smoke.vorticity",
        "pyro.shadow_grid.width",
        "pyro.shadow_steps",
        "pyro.sources",
        "pyro.source_height",
        "pyro.source_radius",
        "pyro.source_force",
        "pyro.buoyancy",
        "pyro.ignition_temperature",
        "pyro.explosion_boost",
        "water2d.transfer",
        "water2d.transfer_limit",
        "water2d.hose",
        "water2d.drain",
        "water2d.wave",
        "water3d.transfer",
        "water3d.transfer_limit",
        "water3d.p2g_mode",
        "water3d.hose",
        "water3d.drain",
        "water3d.rain",
        "water3d.wave",
        "water3d.whitewater",
    };

    for (std::string_view path : project_control_paths) {
        const cubey::ConfigOptionDescriptor& option = require_option(path);
        require(!option.cli_name.empty(), "project control descriptors should expose a CLI name");
        require(!option.group_path.empty(), "project control descriptors should expose a UI group");
    }
}

void test_run_config_toggle_descriptors_have_negative_aliases() {
    constexpr std::array toggles{
        "validation",
        "profile.diagnostics",
        "ocean.spectral_domains",
        "ocean.terrain_fields",
        "terrain.water_surface",
        "atmosphere.auto_exposure",
        "atmosphere.moon",
        "planet.wire_overlay",
        "planet.skirts_enabled",
        "planet.terrain_enabled",
        "water2d.hose",
        "water2d.drain",
        "water2d.wave",
        "water3d.hose",
        "water3d.drain",
        "water3d.rain",
        "water3d.wave",
        "water3d.whitewater",
    };

    for (std::string_view path : toggles) {
        const cubey::ConfigOptionDescriptor& option = require_option(path);
        require(option.type == cubey::ConfigOptionType::Bool,
                "toggle descriptors should be bool options");
        require(!option.negative_cli_name.empty(),
                "project toggles should expose a negative CLI alias");
        require(cubey::find_run_config_option_by_cli_name(option.negative_cli_name) == &option,
                "negative CLI alias should resolve to the toggle descriptor");
    }
}

void test_run_config_loads_json_config_file() {
    const std::filesystem::path path = temp_config_path("cubey-run-config-load-test.json");
    write_text_file(path,
                    R"({
  "width": 640,
  "height": 360,
  "headless": true,
  "output": "/tmp/cubey-config.png",
  "grid": {
    "width": 512,
    "height": 256,
    "depth": 64
  },
  "profile": {
    "output": "smoke-profile",
    "diagnostics": true,
    "diagnostic_interval": 4
  },
  "pbr": {
    "environment_source": "atmosphere",
    "exposure": -0.75
  },
  "ocean": {
    "map_size": 512,
    "cascade": "4",
    "spectral_domains": false,
    "terrain_fields": true
  },
  "terrain": {
    "seed": 12345,
    "water_surface": false
  },
  "smoke": {
    "pressure_solver": "red-black-gauss-seidel",
    "dye_decay": 0.98,
    "injector_radius": 0.05
  },
  "pyro": {
    "source_height": 0.15,
    "source_radius": 0.04,
    "explosion_interval_seconds": 2.0,
    "explosion_duration_seconds": 0.25
  },
  "water2d": {
    "transfer": "pic/flip",
    "hose": true,
    "drain": false
  },
  "water3d": {
    "transfer": "picflip",
    "p2g_mode": "active-faces",
    "whitewater": true
  },
  "atmosphere": {
    "time_of_day_mode": "solar",
    "time_hours": 18.5,
    "moon": false
  }
})");

    std::string program = "cubey";
    std::string config_flag = "--config";
    std::string config_path = path.string();
    std::array<char*, 3> argv{program.data(), config_flag.data(), config_path.data()};
    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());

    require(config.width == 640 && config.height == 360,
            "config file should set window dimensions");
    require(config.headless, "config file should set boolean options");
    require(config.output_path == "/tmp/cubey-config.png",
            "config file should preserve explicit output path");
    require(config.grid.width == 512 && config.grid.height == 256 && config.grid.depth == 64,
            "config file should set shared grid dimensions");
    require(config.profile_output_prefix ==
                    std::filesystem::path("outputs") / "profiles" / "smoke-profile" &&
                config.profile_diagnostics && config.profile_diagnostic_interval == 4,
            "config file should set profiling controls");
    require(config.pbr.environment_source == "atmosphere",
            "config file should set nested PBR options");
    require(config.pbr.exposure == -0.75F && config.pbr.exposure_explicit,
            "config file should set explicit exposure");
    require(config.ocean.map_size == 512 && config.ocean.cascade == 4,
            "config file should set ocean controls");
    require(config.ocean.spectral_domains == 0 && config.ocean.terrain_fields == 1,
            "config file should set ocean tri-state booleans");
    require(config.terrain.seed_set && config.terrain.seed == 12345U,
            "config file should mark terrain seed as explicit");
    require(config.terrain.water_surface == 0, "config file should set terrain booleans");
    require(config.smoke.pressure_solver == "red-black-gauss-seidel" &&
                config.smoke.dye_decay == 0.98F && config.smoke.injector_radius == 0.05F,
            "config file should set smoke controls");
    require(config.pyro.source_height == 0.15F && config.pyro.source_radius == 0.04F &&
                config.pyro.explosion_interval_seconds == 2.0F &&
                config.pyro.explosion_duration_seconds == 0.25F,
            "config file should set pyro controls");
    require(config.water2d.transfer_mode == "pic/flip" && config.water2d.hose == 1 &&
                config.water2d.drain == 0,
            "config file should set water 2D controls");
    require(config.water3d.transfer_mode == "picflip" &&
                config.water3d.p2g_mode == "active-faces" && config.water3d.whitewater == 1,
            "config file should set water 3D controls");
    require(config.atmosphere.time_of_day_mode == "solar" &&
                config.atmosphere.time_hours == 18.5F && config.atmosphere.moon == 0,
            "config file should set atmosphere controls");
}

void test_run_config_cli_and_set_override_config_file() {
    const std::filesystem::path path = temp_config_path("cubey-run-config-override-test.json");
    write_text_file(path,
                    R"({
  "width": 640,
  "height": 360,
  "pbr": {
    "environment_source": "static"
  },
  "ocean": {
    "terrain_fields": false
  },
  "water3d": {
    "whitewater": false
  }
})");

    std::string program = "cubey";
    std::string config_flag = "--config";
    std::string config_path = path.string();
    std::string width_flag = "--width";
    std::string width_value = "800";
    std::string set_flag = "--set";
    std::string set_height = "height=720";
    std::string set_env = "pbr.environment_source=atmosphere";
    std::string set_terrain = "ocean.terrain_fields=true";
    std::string set_whitewater = "water3d.whitewater=true";
    std::array<char*, 13> argv{program.data(),       width_flag.data(),  width_value.data(),
                               config_flag.data(),   config_path.data(), set_flag.data(),
                               set_height.data(),    set_flag.data(),    set_env.data(),
                               set_flag.data(),      set_terrain.data(), set_flag.data(),
                               set_whitewater.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.width == 800, "named CLI flags should override config files");
    require(config.height == 720, "--set should override config files");
    require(config.pbr.environment_source == "atmosphere",
            "--set should override nested config values");
    require(config.ocean.terrain_fields == 1, "--set should parse bool overrides");
    require(config.water3d.whitewater == 1,
            "--set should override descriptor-backed water controls");
}

void test_run_config_descriptor_cli_and_set_precedence() {
    std::string program = "cubey";
    std::string terrain_flag = "--ocean-terrain-fields";
    std::string auto_exposure_flag = "--no-auto-exposure";
    std::string set_flag = "--set";
    std::string set_terrain = "ocean.terrain_fields=false";
    std::string set_auto_exposure = "atmosphere.auto_exposure=true";
    std::string water_wave_flag = "--water3d-wave";
    std::string set_water_wave = "water3d.wave=false";
    std::array<char*, 10> argv{program.data(),           terrain_flag.data(),       set_flag.data(),
                               set_terrain.data(),       auto_exposure_flag.data(), set_flag.data(),
                               set_auto_exposure.data(), water_wave_flag.data(),    set_flag.data(),
                               set_water_wave.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.ocean.terrain_fields == 0,
            "--set should override descriptor-backed positive bool CLI flags");
    require(config.atmosphere.auto_exposure == 1,
            "--set should override descriptor-backed negative bool CLI flags");
    require(config.water3d.wave == 0,
            "--set should override newly descriptor-backed project bool CLI flags");
}

void test_run_config_rejects_invalid_json_config_file() {
    {
        const std::filesystem::path path = temp_config_path("cubey-run-config-unknown-test.json");
        write_text_file(path, R"({"ocean":{"unknown":1}})");
        std::string program = "cubey";
        std::string config_flag = "--config";
        std::string config_path = path.string();
        std::array<char*, 3> argv{program.data(), config_flag.data(), config_path.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "config file should reject unknown options");
    }
    {
        const std::filesystem::path path = temp_config_path("cubey-run-config-type-test.json");
        write_text_file(path, R"({"width":"640"})");
        std::string program = "cubey";
        std::string config_flag = "--config";
        std::string config_path = path.string();
        std::array<char*, 3> argv{program.data(), config_flag.data(), config_path.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "config file should reject wrong JSON types");
    }
}

void test_run_config_writes_json_template() {
    const std::filesystem::path path = temp_config_path("cubey-run-config-template-test.json");
    std::string program = "cubey";
    std::string template_flag = "--write-config-template";
    std::string template_path = path.string();
    std::array<char*, 3> argv{program.data(), template_flag.data(), template_path.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.write_config_template_path == path,
            "run config should preserve template output path");

    const std::string text = read_text_file(path);
    require(text.find("\"width\"") != std::string::npos,
            "config template should include common options");
    require(text.find("\"atmosphere\"") != std::string::npos,
            "config template should include nested atmosphere options");
    require(text.find("\"ocean\"") != std::string::npos,
            "config template should include nested ocean options");
    require(text.find("\"profile\"") != std::string::npos,
            "config template should include profiling options");
    require(text.find("\"grid\"") != std::string::npos,
            "config template should include grid options");
    require(text.find("\"smoke\"") != std::string::npos,
            "config template should include smoke options");
    require(text.find("\"pyro\"") != std::string::npos,
            "config template should include pyro options");
    require(text.find("\"water2d\"") != std::string::npos,
            "config template should include water 2D options");
    require(text.find("\"water3d\"") != std::string::npos,
            "config template should include water 3D options");
}

void test_run_config_parses_input_path() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 8> input_flag{'-', '-', 'i', 'n', 'p', 'u', 't', '\0'};
    std::array<char, 25> input_value{'a', 's', 's', 'e', 't', 's', '/', 'D', 'a',
                                     'm', 'a', 'g', 'e', 'd', 'H', 'e', 'l', 'm',
                                     'e', 't', '.', 'g', 'l', 'b', '\0'};
    std::array<char*, 3> argv{program.data(), input_flag.data(), input_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.gltf.input_path == input_value.data(), "run config should preserve input path");
}

void test_run_config_parses_pbr_environment_options() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 14> environment_flag{'-', '-', 'e', 'n', 'v', 'i', 'r',
                                          'o', 'n', 'm', 'e', 'n', 't', '\0'};
    std::array<char, 6> environment_value{'a', '.', 'h', 'd', 'r', '\0'};
    std::array<char, 16> intensity_flag{'-', '-', 'i', 'b', 'l', '-', 'i', 'n',
                                        't', 'e', 'n', 's', 'i', 't', 'y', '\0'};
    std::array<char, 5> intensity_value{'1', '.', '2', '5', '\0'};
    std::array<char, 31> rotation_flag{'-', '-', 'e', 'n', 'v', 'i', 'r', 'o', 'n', 'm', 'e',
                                       'n', 't', '-', 'r', 'o', 't', 'a', 't', 'i', 'o', 'n',
                                       '-', 'd', 'e', 'g', 'r', 'e', 'e', 's', '\0'};
    std::array<char, 6> rotation_value{'4', '5', '.', '0', '0', '\0'};
    std::array<char, 11> exposure_flag{'-', '-', 'e', 'x', 'p', 'o', 's', 'u', 'r', 'e', '\0'};
    std::array<char, 5> exposure_value{'-', '0', '.', '5', '\0'};
    std::string source_flag = "--pbr-environment-source";
    std::string source_value = "atmosphere";
    std::array<char*, 11> argv{
        program.data(),        environment_flag.data(), environment_value.data(),
        intensity_flag.data(), intensity_value.data(),  rotation_flag.data(),
        rotation_value.data(), exposure_flag.data(),    exposure_value.data(),
        source_flag.data(),    source_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.pbr.environment_path == environment_value.data(),
            "run config should preserve HDR environment path");
    require(config.pbr.ibl_intensity == 1.25F, "run config should parse IBL intensity");
    require(config.pbr.environment_rotation_degrees == 45.0F,
            "run config should parse environment rotation");
    require(config.pbr.exposure == -0.5F, "run config should parse exposure");
    require(config.pbr.exposure_explicit, "run config should track explicit exposure");
    require(config.pbr.environment_source == source_value,
            "run config should parse PBR environment source");
}

void test_run_config_rejects_invalid_pbr_options() {
    std::string program = "cubey";
    std::string source_flag = "--pbr-environment-source";
    std::string source_value = "neon";
    std::array<char*, 3> argv{program.data(), source_flag.data(), source_value.data()};
    require_throws(
        [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
        "run config should reject unknown PBR environment sources");
}

void test_run_config_parses_animation_options() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 18> index_flag{'-', '-', 'a', 'n', 'i', 'm', 'a', 't', 'i',
                                    'o', 'n', '-', 'i', 'n', 'd', 'e', 'x', '\0'};
    std::array<char, 2> index_value{'2', '\0'};
    std::array<char, 18> speed_flag{'-', '-', 'a', 'n', 'i', 'm', 'a', 't', 'i',
                                    'o', 'n', '-', 's', 'p', 'e', 'e', 'd', '\0'};
    std::array<char, 4> speed_value{'0', '.', '5', '\0'};
    std::array<char, 18> pause_flag{'-', '-', 'p', 'a', 'u', 's', 'e', '-', 'a',
                                    'n', 'i', 'm', 'a', 't', 'i', 'o', 'n', '\0'};
    std::array<char*, 6> argv{program.data(),    index_flag.data(),  index_value.data(),
                              speed_flag.data(), speed_value.data(), pause_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.gltf.animation_index == 2, "run config should parse animation index");
    require(config.gltf.animation_speed == 0.5F, "run config should parse animation speed");
    require(config.gltf.animation_paused, "run config should parse animation pause flag");
}

void test_run_config_parses_pbr_debug_view_name() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 13> debug_flag{'-', '-', 'd', 'e', 'b', 'u', 'g',
                                    '-', 'v', 'i', 'e', 'w', '\0'};
    std::array<char, 15> debug_value{'g', 'e', 'o', 'm', 'e', 't', 'r', 'i',
                                     'c', '-', 'n', 'o', 'r', 'm', '\0'};
    std::array<char*, 3> argv{program.data(), debug_flag.data(), debug_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.debug_view == debug_value.data(),
            "run config should preserve renderer debug view name");
}

void test_run_config_parses_atmosphere_options() {
    std::string program = "cubey";
    std::string preset_flag = "--atmosphere-preset";
    std::string preset_value = "sunset";
    std::string elevation_flag = "--sun-elevation";
    std::string elevation_value = "4.5";
    std::string azimuth_flag = "--sun-azimuth";
    std::string azimuth_value = "-28.0";
    std::string altitude_flag = "--camera-altitude-km";
    std::string altitude_value = "2.25";
    std::string mie_flag = "--mie-scale";
    std::string mie_value = "1.75";
    std::string mode_flag = "--time-of-day-mode";
    std::string mode_value = "manual";
    std::string time_flag = "--time-hours";
    std::string time_value = "18.5";
    std::string day_flag = "--day-of-year";
    std::string day_value = "172";
    std::string latitude_flag = "--latitude-degrees";
    std::string latitude_value = "42.5";
    std::string offset_flag = "--sun-azimuth-offset";
    std::string offset_value = "12.5";
    std::string speed_flag = "--time-speed-hours-per-second";
    std::string speed_value = "1.25";
    std::string pause_flag = "--pause-time";
    std::string auto_exposure_flag = "--no-auto-exposure";
    std::string exposure_bias_flag = "--exposure-bias";
    std::string exposure_bias_value = "0.75";
    std::string twilight_flag = "--twilight-strength";
    std::string twilight_value = "1.5";
    std::string twilight_warmth_flag = "--twilight-horizon-warmth";
    std::string twilight_warmth_value = "0.8";
    std::string star_intensity_flag = "--star-intensity";
    std::string star_intensity_value = "1.7";
    std::string star_density_flag = "--star-density";
    std::string star_density_value = "0.4";
    std::string moon_intensity_flag = "--moon-intensity";
    std::string moon_intensity_value = "1.2";
    std::string moonlight_intensity_flag = "--moonlight-intensity";
    std::string moonlight_intensity_value = "1.4";
    std::string moon_phase_offset_flag = "--moon-phase-offset-days";
    std::string moon_phase_offset_value = "7.5";
    std::string moon_size_scale_flag = "--moon-size-scale";
    std::string moon_size_scale_value = "1.8";
    std::string no_moon_flag = "--no-moon";
    std::array<char*, 44> argv{program.data(),
                               preset_flag.data(),
                               preset_value.data(),
                               elevation_flag.data(),
                               elevation_value.data(),
                               azimuth_flag.data(),
                               azimuth_value.data(),
                               altitude_flag.data(),
                               altitude_value.data(),
                               mie_flag.data(),
                               mie_value.data(),
                               mode_flag.data(),
                               mode_value.data(),
                               time_flag.data(),
                               time_value.data(),
                               day_flag.data(),
                               day_value.data(),
                               latitude_flag.data(),
                               latitude_value.data(),
                               offset_flag.data(),
                               offset_value.data(),
                               speed_flag.data(),
                               speed_value.data(),
                               pause_flag.data(),
                               auto_exposure_flag.data(),
                               exposure_bias_flag.data(),
                               exposure_bias_value.data(),
                               twilight_flag.data(),
                               twilight_value.data(),
                               twilight_warmth_flag.data(),
                               twilight_warmth_value.data(),
                               star_intensity_flag.data(),
                               star_intensity_value.data(),
                               star_density_flag.data(),
                               star_density_value.data(),
                               moon_intensity_flag.data(),
                               moon_intensity_value.data(),
                               moonlight_intensity_flag.data(),
                               moonlight_intensity_value.data(),
                               moon_phase_offset_flag.data(),
                               moon_phase_offset_value.data(),
                               moon_size_scale_flag.data(),
                               moon_size_scale_value.data(),
                               no_moon_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.atmosphere.preset == "sunset", "run config should parse atmosphere preset");
    require(config.atmosphere.sun_elevation_degrees == 4.5F,
            "run config should parse sun elevation");
    require(config.atmosphere.sun_azimuth_degrees == -28.0F, "run config should parse sun azimuth");
    require(config.atmosphere.camera_altitude_km == 2.25F,
            "run config should parse camera altitude");
    require(config.atmosphere.mie_scale == 1.75F, "run config should parse Mie scale");
    require(config.atmosphere.time_of_day_mode == "manual",
            "run config should parse atmosphere time mode");
    require(config.atmosphere.time_hours == 18.5F, "run config should parse time hours");
    require(config.atmosphere.day_of_year == 172.0F, "run config should parse day of year");
    require(config.atmosphere.latitude_degrees == 42.5F, "run config should parse latitude");
    require(config.atmosphere.sun_azimuth_offset_degrees == 12.5F,
            "run config should parse sun azimuth offset");
    require(config.atmosphere.time_speed_hours_per_second == 1.25F,
            "run config should parse time speed");
    require(config.atmosphere.time_paused == 1, "run config should parse time pause flag");
    require(config.atmosphere.auto_exposure == 0,
            "run config should parse auto exposure disable flag");
    require(config.atmosphere.exposure_bias == 0.75F, "run config should parse exposure bias");
    require(config.atmosphere.twilight_strength == 1.5F,
            "run config should parse twilight strength");
    require(config.atmosphere.twilight_horizon_warmth == 0.8F,
            "run config should parse twilight horizon warmth");
    require(config.atmosphere.star_intensity == 1.7F, "run config should parse star intensity");
    require(config.atmosphere.star_density == 0.4F, "run config should parse star density");
    require(config.atmosphere.moon_intensity == 1.2F, "run config should parse moon intensity");
    require(config.atmosphere.moonlight_intensity == 1.4F,
            "run config should parse moonlight intensity");
    require(config.atmosphere.moon_phase_offset_days == 7.5F,
            "run config should parse moon phase offset");
    require(config.atmosphere.moon_size_scale == 1.8F, "run config should parse moon size scale");
    require(config.atmosphere.moon == 0, "run config should parse moon disable flag");
}

void test_run_config_rejects_invalid_atmosphere_options() {
    {
        std::string program = "cubey";
        std::string elevation_flag = "--sun-elevation";
        std::string elevation_value = "91";
        std::array<char*, 3> argv{program.data(), elevation_flag.data(), elevation_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid sun elevation");
    }
    {
        std::string program = "cubey";
        std::string azimuth_flag = "--sun-azimuth";
        std::string azimuth_value = "361";
        std::array<char*, 3> argv{program.data(), azimuth_flag.data(), azimuth_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid sun azimuth");
    }
    {
        std::string program = "cubey";
        std::string altitude_flag = "--camera-altitude-km";
        std::string altitude_value = "-1";
        std::array<char*, 3> argv{program.data(), altitude_flag.data(), altitude_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject negative camera altitude");
    }
    {
        std::string program = "cubey";
        std::string mie_flag = "--mie-scale";
        std::string mie_value = "-0.5";
        std::array<char*, 3> argv{program.data(), mie_flag.data(), mie_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject negative Mie scale");
    }
    {
        std::string program = "cubey";
        std::string mode_flag = "--time-of-day-mode";
        std::string mode_value = "civil";
        std::array<char*, 3> argv{program.data(), mode_flag.data(), mode_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject unknown time-of-day mode");
    }
    {
        std::string program = "cubey";
        std::string mode_flag = "--time-of-day-mode";
        std::string mode_value = "solar";
        std::string elevation_flag = "--sun-elevation";
        std::string elevation_value = "4";
        std::array<char*, 5> argv{program.data(), mode_flag.data(), mode_value.data(),
                                  elevation_flag.data(), elevation_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject solar mode with manual sun controls");
    }
    {
        std::string program = "cubey";
        std::string time_flag = "--time-hours";
        std::string time_value = "24.5";
        std::array<char*, 3> argv{program.data(), time_flag.data(), time_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject out-of-range time hours");
    }
    {
        std::string program = "cubey";
        std::string latitude_flag = "--latitude-degrees";
        std::string latitude_value = "-91";
        std::array<char*, 3> argv{program.data(), latitude_flag.data(), latitude_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid latitude");
    }
    {
        std::string program = "cubey";
        std::string bias_flag = "--exposure-bias";
        std::string bias_value = "4.5";
        std::array<char*, 3> argv{program.data(), bias_flag.data(), bias_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid exposure bias");
    }
    {
        std::string program = "cubey";
        std::string star_density_flag = "--star-density";
        std::string star_density_value = "1.5";
        std::array<char*, 3> argv{program.data(), star_density_flag.data(),
                                  star_density_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid star density");
    }
    {
        std::string program = "cubey";
        std::string twilight_warmth_flag = "--twilight-horizon-warmth";
        std::string twilight_warmth_value = "2.5";
        std::array<char*, 3> argv{program.data(), twilight_warmth_flag.data(),
                                  twilight_warmth_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid twilight horizon warmth");
    }
    {
        std::string program = "cubey";
        std::string moon_phase_offset_flag = "--moon-phase-offset-days";
        std::string moon_phase_offset_value = "30";
        std::array<char*, 3> argv{program.data(), moon_phase_offset_flag.data(),
                                  moon_phase_offset_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid moon phase offset");
    }
    {
        std::string program = "cubey";
        std::string moon_size_scale_flag = "--moon-size-scale";
        std::string moon_size_scale_value = "0";
        std::array<char*, 3> argv{program.data(), moon_size_scale_flag.data(),
                                  moon_size_scale_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject invalid moon size scale");
    }
}

void test_run_config_parses_frame_stats_flag() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 20> stats_flag{'-', '-', 'p', 'r', 'i', 'n', 't', '-', 'f', 'r',
                                    'a', 'm', 'e', '-', 's', 't', 'a', 't', 's', '\0'};
    std::array<char*, 2> argv{program.data(), stats_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.print_frame_stats, "run config should parse frame stats logging flag");
}

void test_run_config_parses_profile_options() {
    std::string program = "cubey";
    std::string output_flag = "--profile-output";
    std::string output_value = "water3d-64";
    std::string warmup_flag = "--profile-warmup-frames";
    std::string warmup_value = "60";
    std::string diagnostics_flag = "--profile-diagnostics";
    std::string interval_flag = "--profile-diagnostic-interval";
    std::string interval_value = "4";
    std::array<char*, 8> argv{program.data(),       output_flag.data(),   output_value.data(),
                              warmup_flag.data(),   warmup_value.data(),  diagnostics_flag.data(),
                              interval_flag.data(), interval_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.profile_output_prefix ==
                std::filesystem::path("outputs") / "profiles" / output_value,
            "run config should place simple profile prefixes under outputs/profiles");
    require(config.profile_warmup_frames == 60,
            "run config should parse profile warmup frame count");
    require(config.profile_diagnostics, "run config should parse profile diagnostics flag");
    require(config.profile_diagnostic_interval == 4,
            "run config should parse profile diagnostic interval");
}

void test_run_config_rejects_invalid_profile_diagnostics_options() {
    {
        std::string program = "cubey";
        std::string diagnostics_flag = "--profile-diagnostics";
        std::array<char*, 2> argv{program.data(), diagnostics_flag.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject profile diagnostics without profile output");
    }
    {
        std::string program = "cubey";
        std::string output_flag = "--profile-output";
        std::string output_value = "water3d";
        std::string interval_flag = "--profile-diagnostic-interval";
        std::string interval_value = "0";
        std::array<char*, 5> argv{program.data(), output_flag.data(), output_value.data(),
                                  interval_flag.data(), interval_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject zero profile diagnostic interval");
    }
}

void test_run_config_parses_grid_dimensions() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 13> grid_width_flag{'-', '-', 'g', 'r', 'i', 'd', '-',
                                         'w', 'i', 'd', 't', 'h', '\0'};
    std::array<char, 5> grid_width_value{'1', '0', '2', '4', '\0'};
    std::array<char, 14> grid_height_flag{'-', '-', 'g', 'r', 'i', 'd', '-',
                                          'h', 'e', 'i', 'g', 'h', 't', '\0'};
    std::array<char, 4> grid_height_value{'7', '6', '8', '\0'};
    std::array<char, 13> grid_depth_flag{'-', '-', 'g', 'r', 'i', 'd', '-',
                                         'd', 'e', 'p', 't', 'h', '\0'};
    std::array<char, 3> grid_depth_value{'9', '6', '\0'};
    std::array<char*, 7> argv{program.data(),           grid_width_flag.data(),
                              grid_width_value.data(),  grid_height_flag.data(),
                              grid_height_value.data(), grid_depth_flag.data(),
                              grid_depth_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.grid.width == 1024, "run config should parse grid width");
    require(config.grid.height == 768, "run config should parse grid height");
    require(config.grid.depth == 96, "run config should parse grid depth");
}

void test_run_config_parses_water_controls() {
    std::string program = "cubey";
    std::string water2d_transfer_flag = "--water2d-transfer";
    std::string water2d_transfer_value = "pic-flip";
    std::string water2d_limit_flag = "--water2d-transfer-limit";
    std::string water2d_limit_value = "48";
    std::string water2d_hose_flag = "--water2d-hose";
    std::string water2d_drain_flag = "--no-water2d-drain";
    std::string water2d_wave_flag = "--water2d-wave";
    std::string water3d_transfer_flag = "--water3d-transfer";
    std::string water3d_transfer_value = "apic";
    std::string water3d_limit_flag = "--water3d-transfer-limit";
    std::string water3d_limit_value = "96";
    std::string water3d_p2g_flag = "--water3d-p2g-mode";
    std::string water3d_p2g_value = "tiled";
    std::string water3d_hose_flag = "--water3d-hose";
    std::string water3d_drain_flag = "--water3d-drain";
    std::string water3d_rain_flag = "--no-water3d-rain";
    std::string water3d_wave_flag = "--no-water3d-wave";
    std::string water3d_whitewater_flag = "--no-water3d-whitewater";
    std::array<char*, 19> argv{program.data(),
                               water2d_transfer_flag.data(),
                               water2d_transfer_value.data(),
                               water2d_limit_flag.data(),
                               water2d_limit_value.data(),
                               water2d_hose_flag.data(),
                               water2d_drain_flag.data(),
                               water2d_wave_flag.data(),
                               water3d_transfer_flag.data(),
                               water3d_transfer_value.data(),
                               water3d_limit_flag.data(),
                               water3d_limit_value.data(),
                               water3d_p2g_flag.data(),
                               water3d_p2g_value.data(),
                               water3d_hose_flag.data(),
                               water3d_drain_flag.data(),
                               water3d_rain_flag.data(),
                               water3d_wave_flag.data(),
                               water3d_whitewater_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.water2d.transfer_mode == "pic-flip",
            "run config should parse Water 2D transfer mode");
    require(config.water2d.transfer_limit == 48, "run config should parse Water 2D transfer limit");
    require(config.water2d.hose == 1 && config.water2d.drain == 0 && config.water2d.wave == 1,
            "run config should parse Water 2D flow toggles");
    require(config.water3d.transfer_mode == "apic",
            "run config should parse Water 3D transfer mode");
    require(config.water3d.transfer_limit == 96, "run config should parse Water 3D transfer limit");
    require(config.water3d.p2g_mode == "tiled", "run config should parse Water 3D P2G mode");
    require(config.water3d.hose == 1 && config.water3d.drain == 1 && config.water3d.rain == 0,
            "run config should parse Water 3D flow toggles");
    require(config.water3d.wave == 0 && config.water3d.whitewater == 0,
            "run config should parse Water 3D optional-system toggles");
}

void test_run_config_parses_ocean_controls() {
    std::string program = "cubey";
    std::string map_flag = "--ocean-map-size";
    std::string map_value = "256";
    std::string precision_flag = "--ocean-field-precision";
    std::string precision_value = "half";
    std::string cascade_flag = "--ocean-cascade";
    std::string cascade_value = "4";
    std::string terrain_fields_flag = "--ocean-terrain-fields";
    std::string wire_flag = "--ocean-wire-overlay";
    std::string opacity_flag = "--ocean-wire-opacity";
    std::string opacity_value = "0.75";
    std::array<char*, 11> argv{program.data(),         map_flag.data(),
                               map_value.data(),       precision_flag.data(),
                               precision_value.data(), cascade_flag.data(),
                               cascade_value.data(),   terrain_fields_flag.data(),
                               wire_flag.data(),       opacity_flag.data(),
                               opacity_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.ocean.map_size == 256, "run config should parse ocean map size");
    require(config.ocean.field_precision == "half",
            "run config should parse ocean field precision");
    require(config.ocean.cascade == 4, "run config should parse ocean cascade selection");
    require(config.ocean.terrain_fields == 1, "run config should parse ocean terrain field toggle");
    require(config.ocean.wire_overlay, "run config should parse ocean wire overlay");
    require(config.ocean.wire_opacity == 0.75F, "run config should parse ocean wire opacity");

    std::string all_value = "all";
    std::array<char*, 3> all_argv{program.data(), cascade_flag.data(), all_value.data()};
    const cubey::RunConfig all_config =
        cubey::parse_run_config(static_cast<int>(all_argv.size()), all_argv.data());
    require(all_config.ocean.cascade == -1, "run config should parse all ocean cascades");

    const cubey::RunConfig defaults = cubey::parse_run_config(1, all_argv.data());
    require(defaults.ocean.cascade == -1, "run config should default to all ocean cascades");
    require(defaults.ocean.terrain_fields == -1,
            "run config should default ocean terrain fields to project defaults");

    std::string no_terrain_fields_flag = "--no-ocean-terrain-fields";
    std::array<char*, 2> no_terrain_fields_argv{program.data(), no_terrain_fields_flag.data()};
    const cubey::RunConfig no_terrain_fields_config = cubey::parse_run_config(
        static_cast<int>(no_terrain_fields_argv.size()), no_terrain_fields_argv.data());
    require(no_terrain_fields_config.ocean.terrain_fields == 0,
            "run config should parse disabled ocean terrain fields");
}

void test_run_config_parses_terrain_controls() {
    std::string program = "cubey";
    std::string seed_flag = "--terrain-seed";
    std::string seed_value = "12345";
    std::string cell_size_flag = "--terrain-cell-size";
    std::string cell_size_value = "5.5";
    std::string sea_level_flag = "--terrain-sea-level";
    std::string sea_level_value = "-3.25";
    std::string land_extent_flag = "--terrain-land-extent";
    std::string land_extent_value = "0.64";
    std::string coast_noise_flag = "--terrain-coast-noise";
    std::string coast_noise_value = "0.31";
    std::string relief_flag = "--terrain-relief";
    std::string relief_value = "1.35";
    std::string ridges_flag = "--terrain-ridges";
    std::string ridges_value = "0.85";
    std::string valleys_flag = "--terrain-valleys";
    std::string valleys_value = "1.15";
    std::string water_surface_flag = "--no-terrain-water-surface";
    std::array<char*, 18> argv{
        program.data(),          seed_flag.data(),         seed_value.data(),
        cell_size_flag.data(),   cell_size_value.data(),   sea_level_flag.data(),
        sea_level_value.data(),  land_extent_flag.data(),  land_extent_value.data(),
        coast_noise_flag.data(), coast_noise_value.data(), relief_flag.data(),
        relief_value.data(),     ridges_flag.data(),       ridges_value.data(),
        valleys_flag.data(),     valleys_value.data(),     water_surface_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.terrain.seed_set, "run config should mark terrain seed as set");
    require(config.terrain.seed == 12345U, "run config should parse terrain seed");
    require(config.terrain.cell_size == 5.5F, "run config should parse terrain cell size");
    require(config.terrain.sea_level == -3.25F, "run config should parse terrain sea level");
    require(config.terrain.land_extent == 0.64F, "run config should parse terrain land extent");
    require(config.terrain.coast_noise == 0.31F, "run config should parse terrain coast noise");
    require(config.terrain.relief == 1.35F, "run config should parse terrain relief");
    require(config.terrain.ridges == 0.85F, "run config should parse terrain ridges");
    require(config.terrain.valleys == 1.15F, "run config should parse terrain valleys");
    require(config.terrain.water_surface == 0,
            "run config should parse disabled terrain water surface");

    std::string enabled_flag = "--terrain-water-surface";
    std::array<char*, 2> enabled_argv{program.data(), enabled_flag.data()};
    const cubey::RunConfig enabled_config =
        cubey::parse_run_config(static_cast<int>(enabled_argv.size()), enabled_argv.data());
    require(enabled_config.terrain.water_surface == 1,
            "run config should parse enabled terrain water surface");
}

void test_run_config_rejects_invalid_ocean_controls() {
    std::string program = "cubey";
    std::string cascade_flag = "--ocean-cascade";
    std::string cascade_value = "5";
    std::array<char*, 3> argv{program.data(), cascade_flag.data(), cascade_value.data()};
    require_throws(
        [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
        "run config should reject unsupported ocean cascade selection");
}

void test_run_config_parses_planet_controls() {
    std::string program = "cubey";
    std::string radius_flag = "--planet-radius-m";
    std::string radius_value = "600000";
    std::string atmosphere_flag = "--planet-atmosphere-height-m";
    std::string atmosphere_value = "70000";
    std::string altitude_flag = "--planet-camera-altitude-m";
    std::string altitude_value = "240000";
    std::string patches_flag = "--planet-patches-per-face";
    std::string patches_value = "4";
    std::string patch_resolution_flag = "--planet-patch-resolution";
    std::string patch_resolution_value = "128";
    std::string max_lod_flag = "--planet-max-lod-level";
    std::string max_lod_value = "9";
    std::string lod_target_flag = "--planet-lod-target-edge-px";
    std::string lod_target_value = "9.5";
    std::string lod_hysteresis_flag = "--planet-lod-hysteresis";
    std::string lod_hysteresis_value = "0.25";
    std::string wire_flag = "--planet-wire-overlay";
    std::string skirts_flag = "--no-planet-skirts";
    std::string skirt_depth_flag = "--planet-skirt-depth-scale";
    std::string skirt_depth_value = "0.45";
    std::string terrain_flag = "--no-planet-terrain";
    std::string terrain_height_flag = "--planet-terrain-height-scale-m";
    std::string terrain_height_value = "9000";
    std::string terrain_noise_flag = "--planet-terrain-noise-scale";
    std::string terrain_noise_value = "4.25";
    std::string terrain_seed_flag = "--planet-terrain-seed";
    std::string terrain_seed_value = "42";
    std::string day_flag = "--planet-day-of-year";
    std::string day_value = "81";
    std::string time_flag = "--planet-time-hours";
    std::string time_value = "15.25";
    std::string speed_flag = "--planet-time-speed-hours-per-second";
    std::string speed_value = "0.75";
    std::string pause_flag = "--planet-pause-time";
    std::string camera_mode_flag = "--planet-camera-mode";
    std::string camera_mode_value = "surface";
    std::array<char*, 37> argv{program.data(),
                               radius_flag.data(),
                               radius_value.data(),
                               atmosphere_flag.data(),
                               atmosphere_value.data(),
                               altitude_flag.data(),
                               altitude_value.data(),
                               patches_flag.data(),
                               patches_value.data(),
                               patch_resolution_flag.data(),
                               patch_resolution_value.data(),
                               max_lod_flag.data(),
                               max_lod_value.data(),
                               lod_target_flag.data(),
                               lod_target_value.data(),
                               lod_hysteresis_flag.data(),
                               lod_hysteresis_value.data(),
                               wire_flag.data(),
                               skirts_flag.data(),
                               skirt_depth_flag.data(),
                               skirt_depth_value.data(),
                               terrain_flag.data(),
                               terrain_height_flag.data(),
                               terrain_height_value.data(),
                               terrain_noise_flag.data(),
                               terrain_noise_value.data(),
                               terrain_seed_flag.data(),
                               terrain_seed_value.data(),
                               day_flag.data(),
                               day_value.data(),
                               time_flag.data(),
                               time_value.data(),
                               speed_flag.data(),
                               speed_value.data(),
                               pause_flag.data(),
                               camera_mode_flag.data(),
                               camera_mode_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.planet.radius_m == 600000.0F, "run config should parse planet radius");
    require(config.planet.atmosphere_height_m == 70000.0F,
            "run config should parse planet atmosphere height");
    require(config.planet.camera_altitude_m == 240000.0F,
            "run config should parse planet camera altitude");
    require(config.planet.patches_per_face == 4U,
            "run config should parse planet patches per face");
    require(config.planet.patch_resolution == 128U,
            "run config should parse planet patch resolution");
    require(config.planet.max_lod_level_set && config.planet.max_lod_level == 9U,
            "run config should parse planet max LOD level");
    require(config.planet.lod_target_edge_px == 9.5F, "run config should parse planet LOD target");
    require(config.planet.lod_hysteresis == 0.25F,
            "run config should parse planet LOD hysteresis");
    require(config.planet.wire_overlay == 1, "run config should parse planet wire overlay");
    require(config.planet.skirts_enabled == 0, "run config should parse planet skirts toggle");
    require(config.planet.skirt_depth_scale == 0.45F,
            "run config should parse planet skirt depth scale");
    require(config.planet.terrain_enabled == 0, "run config should parse planet terrain toggle");
    require(config.planet.terrain_height_scale_m == 9000.0F,
            "run config should parse planet terrain height scale");
    require(config.planet.terrain_noise_scale == 4.25F,
            "run config should parse planet terrain noise scale");
    require(config.planet.terrain_seed_set && config.planet.terrain_seed == 42U,
            "run config should parse planet terrain seed");
    require(config.planet.day_of_year == 81.0F, "run config should parse planet day of year");
    require(config.planet.time_hours == 15.25F, "run config should parse planet time hours");
    require(config.planet.time_speed_hours_per_second == 0.75F,
            "run config should parse planet time speed");
    require(config.planet.time_paused == 1, "run config should parse planet time pause");
    require(config.planet.camera_mode == "surface", "run config should parse planet camera mode");
}

void test_run_config_rejects_invalid_planet_controls() {
    std::string program = "cubey";
    std::string radius_flag = "--planet-radius-m";
    std::string radius_value = "0";
    std::array<char*, 3> radius_argv{program.data(), radius_flag.data(), radius_value.data()};
    require_throws(
        [&radius_argv]() {
            cubey::parse_run_config(static_cast<int>(radius_argv.size()), radius_argv.data());
        },
        "run config should reject nonpositive planet radius");

    std::string altitude_flag = "--planet-camera-altitude-m";
    std::string altitude_value = "-1";
    std::array<char*, 3> altitude_argv{program.data(), altitude_flag.data(), altitude_value.data()};
    require_throws(
        [&altitude_argv]() {
            cubey::parse_run_config(static_cast<int>(altitude_argv.size()), altitude_argv.data());
        },
        "run config should reject negative planet camera altitude");

    std::string max_lod_flag = "--planet-max-lod-level";
    std::string max_lod_value = "10";
    std::array<char*, 3> max_lod_argv{program.data(), max_lod_flag.data(), max_lod_value.data()};
    require_throws(
        [&max_lod_argv]() {
            cubey::parse_run_config(static_cast<int>(max_lod_argv.size()), max_lod_argv.data());
        },
        "run config should reject planet max LOD above live cap");

    std::string patch_resolution_flag = "--planet-patch-resolution";
    std::string patch_resolution_value = "129";
    std::array<char*, 3> patch_resolution_argv{program.data(), patch_resolution_flag.data(),
                                               patch_resolution_value.data()};
    require_throws(
        [&patch_resolution_argv]() {
            cubey::parse_run_config(static_cast<int>(patch_resolution_argv.size()),
                                    patch_resolution_argv.data());
        },
        "run config should reject oversized planet patch resolution");

    std::string lod_hysteresis_flag = "--planet-lod-hysteresis";
    std::string lod_hysteresis_value = "1";
    std::array<char*, 3> lod_hysteresis_argv{program.data(), lod_hysteresis_flag.data(),
                                             lod_hysteresis_value.data()};
    require_throws(
        [&lod_hysteresis_argv]() {
            cubey::parse_run_config(static_cast<int>(lod_hysteresis_argv.size()),
                                    lod_hysteresis_argv.data());
        },
        "run config should reject invalid planet LOD hysteresis");

    std::string camera_mode_flag = "--planet-camera-mode";
    std::string camera_mode_value = "sideways";
    std::array<char*, 3> camera_mode_argv{program.data(), camera_mode_flag.data(),
                                          camera_mode_value.data()};
    require_throws(
        [&camera_mode_argv]() {
            cubey::parse_run_config(static_cast<int>(camera_mode_argv.size()),
                                    camera_mode_argv.data());
        },
        "run config should reject invalid planet camera mode");
}

void test_run_config_parses_shadow_volume_controls() {
    std::string program = "cubey";
    std::string width_flag = "--shadow-grid-width";
    std::string width_value = "96";
    std::string height_flag = "--shadow-grid-height";
    std::string height_value = "80";
    std::string depth_flag = "--shadow-grid-depth";
    std::string depth_value = "64";
    std::string steps_flag = "--shadow-steps";
    std::string steps_value = "48";
    std::string interval_flag = "--shadow-update-interval";
    std::string interval_value = "2";
    std::array<char*, 11> argv{program.data(),       width_flag.data(),    width_value.data(),
                               height_flag.data(),   height_value.data(),  depth_flag.data(),
                               depth_value.data(),   steps_flag.data(),    steps_value.data(),
                               interval_flag.data(), interval_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.pyro.shadow_grid.width == 96, "run config should parse shadow grid width");
    require(config.pyro.shadow_grid.height == 80, "run config should parse shadow grid height");
    require(config.pyro.shadow_grid.depth == 64, "run config should parse shadow grid depth");
    require(config.pyro.shadow_steps == 48, "run config should parse shadow steps");
    require(config.pyro.shadow_update_interval == 2,
            "run config should parse shadow update interval");
}

void test_run_config_parses_smoke_injector_count() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 18> injectors_flag{'-', '-', 's', 'm', 'o', 'k', 'e', '-', 'i',
                                        'n', 'j', 'e', 'c', 't', 'o', 'r', 's', '\0'};
    std::array<char, 2> injectors_value{'8', '\0'};
    std::array<char*, 3> argv{program.data(), injectors_flag.data(), injectors_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.smoke.injectors == 8, "run config should parse smoke injector count");
}

void test_run_config_parses_smoke_injector_orbit_controls() {
    std::string program = "cubey";
    std::string radius_flag = "--smoke-injector-orbit-radius";
    std::string radius_value = "0.24";
    std::string radius_spread_flag = "--smoke-injector-orbit-radius-spread";
    std::string radius_spread_value = "0.18";
    std::string speed_flag = "--smoke-injector-orbit-angular-speed";
    std::string speed_value = "0.25";
    std::string speed_spread_flag = "--smoke-injector-orbit-angular-speed-spread";
    std::string speed_spread_value = "1.5";
    std::string phase_spread_flag = "--smoke-injector-orbit-phase-spread";
    std::string phase_spread_value = "0.75";
    std::array<char*, 11> argv{program.data(),
                               radius_flag.data(),
                               radius_value.data(),
                               radius_spread_flag.data(),
                               radius_spread_value.data(),
                               speed_flag.data(),
                               speed_value.data(),
                               speed_spread_flag.data(),
                               speed_spread_value.data(),
                               phase_spread_flag.data(),
                               phase_spread_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.smoke.injector_orbit_radius == 0.24F,
            "run config should parse smoke injector orbit radius");
    require(config.smoke.injector_orbit_radius_spread == 0.18F,
            "run config should parse smoke injector orbit radius spread");
    require(config.smoke.injector_orbit_angular_speed == 0.25F,
            "run config should parse smoke injector orbit angular speed");
    require(config.smoke.injector_orbit_angular_speed_spread == 1.5F,
            "run config should parse smoke injector orbit angular speed spread");
    require(config.smoke.injector_orbit_phase_spread == 0.75F,
            "run config should parse smoke injector orbit phase spread");
}

void test_run_config_parses_smoke_injector_force_controls() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 23> force_flag{'-', '-', 's', 'm', 'o', 'k', 'e', '-', 'i', 'n', 'j', 'e',
                                    'c', 't', 'o', 'r', '-', 'f', 'o', 'r', 'c', 'e', '\0'};
    std::array<char, 4> force_value{'7', '.', '5', '\0'};
    std::array<char, 28> propulsion_flag{'-', '-', 's', 'm', 'o', 'k', 'e', '-', 'i', 'n',
                                         'j', 'e', 'c', 't', 'o', 'r', '-', 'p', 'r', 'o',
                                         'p', 'u', 'l', 's', 'i', 'o', 'n', '\0'};
    std::array<char, 4> propulsion_value{'1', '.', '6', '\0'};
    std::array<char*, 5> argv{program.data(), force_flag.data(), force_value.data(),
                              propulsion_flag.data(), propulsion_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.smoke.injector_force == 7.5F, "run config should parse smoke injector force");
    require(config.smoke.injector_propulsion == 1.6F,
            "run config should parse smoke injector propulsion");
}

void test_run_config_parses_smoke_solver_controls() {
    std::string program = "cubey";
    std::string pressure_flag = "--smoke-pressure-iterations";
    std::string pressure_value = "48";
    std::string pressure_solver_flag = "--smoke-pressure-solver";
    std::string pressure_solver_value = "rbgs";
    std::string dye_decay_flag = "--smoke-dye-decay";
    std::string dye_decay_value = "0.985";
    std::string velocity_decay_flag = "--smoke-velocity-decay";
    std::string velocity_decay_value = "0.991";
    std::string radius_flag = "--smoke-injector-radius";
    std::string radius_value = "0.041";
    std::string vorticity_flag = "--smoke-vorticity";
    std::string vorticity_value = "24.0";
    std::array<char*, 13> argv{program.data(),
                               pressure_flag.data(),
                               pressure_value.data(),
                               pressure_solver_flag.data(),
                               pressure_solver_value.data(),
                               dye_decay_flag.data(),
                               dye_decay_value.data(),
                               velocity_decay_flag.data(),
                               velocity_decay_value.data(),
                               radius_flag.data(),
                               radius_value.data(),
                               vorticity_flag.data(),
                               vorticity_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.smoke.pressure_iterations == 48,
            "run config should parse smoke pressure iterations");
    require(config.smoke.pressure_solver == "rbgs",
            "run config should parse smoke pressure solver");
    require(config.smoke.dye_decay == 0.985F, "run config should parse smoke dye decay");
    require(config.smoke.velocity_decay == 0.991F, "run config should parse smoke velocity decay");
    require(config.smoke.injector_radius == 0.041F,
            "run config should parse smoke injector radius");
    require(config.smoke.vorticity == 24.0F, "run config should parse smoke vorticity");
}

void test_run_config_parses_pyro_buoyancy_control() {
    std::string program = "cubey";
    std::string buoyancy_flag = "--pyro-buoyancy";
    std::string buoyancy_value = "1.75";
    std::array<char*, 3> argv{program.data(), buoyancy_flag.data(), buoyancy_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.pyro.buoyancy == 1.75F, "run config should parse pyro buoyancy");
}

void test_run_config_parses_pyro_source_controls() {
    std::string program = "cubey";
    std::string sources_flag = "--pyro-sources";
    std::string sources_value = "6";
    std::string height_flag = "--pyro-source-height";
    std::string height_value = "0.21";
    std::string radius_flag = "--pyro-source-radius";
    std::string radius_value = "0.08";
    std::string force_flag = "--pyro-source-force";
    std::string force_value = "9.5";
    std::string soot_flag = "--pyro-soot";
    std::string soot_value = "7.25";
    std::string temperature_flag = "--pyro-temperature";
    std::string temperature_value = "1.75";
    std::string fuel_flag = "--pyro-fuel";
    std::string fuel_value = "2.5";
    std::string interval_flag = "--explosion-interval";
    std::string interval_value = "2.5";
    std::string duration_flag = "--explosion-duration";
    std::string duration_value = "0.18";
    std::string boost_flag = "--explosion-boost";
    std::string boost_value = "22.0";
    std::array<char*, 21> argv{
        program.data(),           sources_flag.data(),   sources_value.data(),
        height_flag.data(),       height_value.data(),   radius_flag.data(),
        radius_value.data(),      force_flag.data(),     force_value.data(),
        soot_flag.data(),         soot_value.data(),     temperature_flag.data(),
        temperature_value.data(), fuel_flag.data(),      fuel_value.data(),
        interval_flag.data(),     interval_value.data(), duration_flag.data(),
        duration_value.data(),    boost_flag.data(),     boost_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.pyro.sources == 6, "run config should parse pyro source count");
    require(config.pyro.source_height == 0.21F, "run config should parse pyro source height");
    require(config.pyro.source_radius == 0.08F, "run config should parse pyro source radius");
    require(config.pyro.source_force == 9.5F, "run config should parse pyro source force");
    require(config.pyro.soot == 7.25F, "run config should parse pyro soot amount");
    require(config.pyro.temperature == 1.75F, "run config should parse pyro temperature amount");
    require(config.pyro.fuel == 2.5F, "run config should parse pyro fuel amount");
    require(config.pyro.explosion_interval_seconds == 2.5F,
            "run config should parse explosion interval");
    require(config.pyro.explosion_duration_seconds == 0.18F,
            "run config should parse explosion duration");
    require(config.pyro.explosion_boost == 22.0F, "run config should parse explosion boost");
}

void test_run_config_parses_pyro_fire_controls() {
    std::string program = "cubey";
    std::string ignition_flag = "--pyro-ignition-temperature";
    std::string ignition_value = "0.31";
    std::string burn_rate_flag = "--pyro-burn-rate";
    std::string burn_rate_value = "4.5";
    std::string heat_output_flag = "--pyro-heat-output";
    std::string heat_output_value = "3.25";
    std::string soot_yield_flag = "--pyro-soot-yield";
    std::string soot_yield_value = "0.22";
    std::string expansion_flag = "--pyro-expansion";
    std::string expansion_value = "1.8";
    std::string cooling_flag = "--pyro-flame-cooling";
    std::string cooling_value = "2.75";
    std::string shredding_flag = "--pyro-shredding";
    std::string shredding_value = "3.5";
    std::string turbulence_flag = "--pyro-turbulence";
    std::string turbulence_value = "0.85";
    std::array<char*, 17> argv{
        program.data(),           ignition_flag.data(),   ignition_value.data(),
        burn_rate_flag.data(),    burn_rate_value.data(), heat_output_flag.data(),
        heat_output_value.data(), soot_yield_flag.data(), soot_yield_value.data(),
        expansion_flag.data(),    expansion_value.data(), cooling_flag.data(),
        cooling_value.data(),     shredding_flag.data(),  shredding_value.data(),
        turbulence_flag.data(),   turbulence_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.pyro.ignition_temperature == 0.31F, "run config should parse pyro ignition");
    require(config.pyro.burn_rate == 4.5F, "run config should parse pyro burn rate");
    require(config.pyro.heat_output == 3.25F, "run config should parse pyro heat output");
    require(config.pyro.soot_yield == 0.22F, "run config should parse pyro soot yield");
    require(config.pyro.expansion == 1.8F, "run config should parse pyro expansion");
    require(config.pyro.flame_cooling == 2.75F, "run config should parse pyro flame cooling");
    require(config.pyro.shredding == 3.5F, "run config should parse pyro shredding");
    require(config.pyro.turbulence == 0.85F, "run config should parse pyro turbulence");
}

void test_run_config_parses_pyro_obstacle_controls() {
    std::string program = "cubey";
    std::string height_flag = "--pyro-obstacle-height";
    std::string height_value = "0.58";
    std::string radius_flag = "--pyro-obstacle-radius";
    std::string radius_value = "0.18";
    std::array<char*, 5> argv{program.data(), height_flag.data(), height_value.data(),
                              radius_flag.data(), radius_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.pyro.obstacle_height == 0.58F, "run config should parse pyro obstacle height");
    require(config.pyro.obstacle_radius == 0.18F, "run config should parse pyro obstacle radius");
}

void test_run_cli_app_sets_default_title_and_returns_runner_status() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char*, 1> argv{program.data()};
    std::string observed_title;

    const int status = cubey::run_cli_app(static_cast<int>(argv.size()), argv.data(),
                                          {
                                              .app_name = "unit_test",
                                              .default_title = "cubey unit test",
                                          },
                                          [&observed_title](const cubey::RunConfig& config) {
                                              observed_title = config.title;
                                              return 7;
                                          });

    require(status == 7, "CLI app helper should return runner status");
    require(observed_title == "cubey unit test", "CLI app helper should apply the default title");
}
