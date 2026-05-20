#include <cubey/core/run_config.h>

#include <array>
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

void test_run_config_parses_input_path() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 8> input_flag{'-', '-', 'i', 'n', 'p', 'u', 't', '\0'};
    std::array<char, 25> input_value{'a', 's', 's', 'e', 't', 's', '/', 'D', 'a',
                                     'm', 'a', 'g', 'e', 'd', 'H', 'e', 'l', 'm',
                                     'e', 't', '.', 'g', 'l', 'b', '\0'};
    std::array<char*, 3> argv{program.data(), input_flag.data(), input_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.input_path == input_value.data(), "run config should preserve input path");
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
    std::array<char*, 9> argv{
        program.data(),        environment_flag.data(), environment_value.data(),
        intensity_flag.data(), intensity_value.data(),  rotation_flag.data(),
        rotation_value.data(), exposure_flag.data(),    exposure_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.environment_path == environment_value.data(),
            "run config should preserve HDR environment path");
    require(config.ibl_intensity == 1.25F, "run config should parse IBL intensity");
    require(config.environment_rotation_degrees == 45.0F,
            "run config should parse environment rotation");
    require(config.exposure == -0.5F, "run config should parse exposure");
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
    require(config.animation_index == 2, "run config should parse animation index");
    require(config.animation_speed == 0.5F, "run config should parse animation speed");
    require(config.animation_paused, "run config should parse animation pause flag");
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

void test_run_config_parses_frame_stats_flag() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 20> stats_flag{'-', '-', 'p', 'r', 'i', 'n', 't', '-', 'f', 'r',
                                    'a', 'm', 'e', '-', 's', 't', 'a', 't', 's', '\0'};
    std::array<char*, 2> argv{program.data(), stats_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.print_frame_stats, "run config should parse frame stats logging flag");
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
    require(config.grid_width == 1024, "run config should parse grid width");
    require(config.grid_height == 768, "run config should parse grid height");
    require(config.grid_depth == 96, "run config should parse grid depth");
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
    std::array<char*, 11> argv{program.data(), width_flag.data(),    width_value.data(),
                               height_flag.data(), height_value.data(), depth_flag.data(),
                               depth_value.data(), steps_flag.data(),   steps_value.data(),
                               interval_flag.data(), interval_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.shadow_grid_width == 96, "run config should parse shadow grid width");
    require(config.shadow_grid_height == 80, "run config should parse shadow grid height");
    require(config.shadow_grid_depth == 64, "run config should parse shadow grid depth");
    require(config.shadow_steps == 48, "run config should parse shadow steps");
    require(config.shadow_update_interval == 2,
            "run config should parse shadow update interval");
}

void test_run_config_parses_injector_count() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 12> injectors_flag{'-', '-', 'i', 'n', 'j', 'e',
                                        'c', 't', 'o', 'r', 's', '\0'};
    std::array<char, 2> injectors_value{'8', '\0'};
    std::array<char*, 3> argv{program.data(), injectors_flag.data(), injectors_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.injectors == 8, "run config should parse procedural injector count");
}

void test_run_config_parses_injector_orbit_controls() {
    std::string program = "cubey";
    std::string radius_flag = "--injector-orbit-radius";
    std::string radius_value = "0.24";
    std::string radius_spread_flag = "--injector-orbit-radius-spread";
    std::string radius_spread_value = "0.18";
    std::string speed_flag = "--injector-orbit-angular-speed";
    std::string speed_value = "0.25";
    std::string speed_spread_flag = "--injector-orbit-angular-speed-spread";
    std::string speed_spread_value = "1.5";
    std::string phase_spread_flag = "--injector-orbit-phase-spread";
    std::string phase_spread_value = "0.75";
    std::string inclination_flag = "--injector-orbit-inclination-degrees";
    std::string inclination_value = "12.5";
    std::string inclination_spread_flag = "--injector-orbit-inclination-spread-degrees";
    std::string inclination_spread_value = "45.0";
    std::array<char*, 15> argv{program.data(),
                               radius_flag.data(),
                               radius_value.data(),
                               radius_spread_flag.data(),
                               radius_spread_value.data(),
                               speed_flag.data(),
                               speed_value.data(),
                               speed_spread_flag.data(),
                               speed_spread_value.data(),
                               phase_spread_flag.data(),
                               phase_spread_value.data(),
                               inclination_flag.data(),
                               inclination_value.data(),
                               inclination_spread_flag.data(),
                               inclination_spread_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.injector_orbit_radius == 0.24F,
            "run config should parse procedural injector orbit radius");
    require(config.injector_orbit_radius_spread == 0.18F,
            "run config should parse procedural injector orbit radius spread");
    require(config.injector_orbit_angular_speed == 0.25F,
            "run config should parse procedural injector orbit angular speed");
    require(config.injector_orbit_angular_speed_spread == 1.5F,
            "run config should parse procedural injector orbit angular speed spread");
    require(config.injector_orbit_phase_spread == 0.75F,
            "run config should parse procedural injector orbit phase spread");
    require(config.injector_orbit_inclination_degrees == 12.5F,
            "run config should parse procedural injector orbit inclination");
    require(config.injector_orbit_inclination_spread_degrees == 45.0F,
            "run config should parse procedural injector orbit inclination spread");
}

void test_run_config_parses_injector_movement_controls() {
    {
        std::string program = "cubey";
        std::string movement_flag = "--injector-movement";
        std::string movement_value = "circle";
        std::string height_flag = "--injector-circle-height";
        std::string height_value = "0.65";
        std::array<char*, 5> argv{program.data(), movement_flag.data(), movement_value.data(),
                                  height_flag.data(), height_value.data()};

        const cubey::RunConfig config =
            cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
        require(config.injector_movement == "circle",
                "run config should parse procedural injector movement");
        require(config.injector_circle_height == 0.65F,
                "run config should parse procedural injector circle height");
    }
    {
        std::string program = "cubey";
        std::string height_flag = "--injector-circle-height";
        std::string height_value = "1.25";
        std::array<char*, 3> argv{program.data(), height_flag.data(), height_value.data()};
        require_throws(
            [&argv]() { cubey::parse_run_config(static_cast<int>(argv.size()), argv.data()); },
            "run config should reject injector circle heights outside the simulation volume");
    }
}

void test_run_config_parses_injector_force_controls() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 17> force_flag{'-', '-', 'i', 'n', 'j', 'e', 'c', 't', 'o',
                                    'r', '-', 'f', 'o', 'r', 'c', 'e', '\0'};
    std::array<char, 4> force_value{'7', '.', '5', '\0'};
    std::array<char, 22> propulsion_flag{'-', '-', 'i', 'n', 'j', 'e', 'c', 't', 'o', 'r', '-',
                                         'p', 'r', 'o', 'p', 'u', 'l', 's', 'i', 'o', 'n', '\0'};
    std::array<char, 4> propulsion_value{'1', '.', '6', '\0'};
    std::array<char*, 5> argv{program.data(), force_flag.data(), force_value.data(),
                              propulsion_flag.data(), propulsion_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.injector_force == 7.5F, "run config should parse procedural injector force");
    require(config.injector_propulsion == 1.6F,
            "run config should parse procedural injector propulsion");
}

void test_run_config_parses_fluid_density_and_buoyancy_controls() {
    std::string program = "cubey";
    std::string density_flag = "--fluid-density-injection";
    std::string density_value = "7.25";
    std::string buoyancy_flag = "--fluid-buoyancy";
    std::string buoyancy_value = "1.75";
    std::array<char*, 5> argv{program.data(), density_flag.data(), density_value.data(),
                              buoyancy_flag.data(), buoyancy_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.fluid_density_injection == 7.25F,
            "run config should parse fluid density injection");
    require(config.fluid_buoyancy == 1.75F, "run config should parse fluid buoyancy");
}

void test_run_config_parses_fluid_3d_source_controls() {
    std::string program = "cubey";
    std::string scenario_flag = "--fluid-scenario";
    std::string scenario_value = "smoke-plume";
    std::string sources_flag = "--fluid-sources";
    std::string sources_value = "6";
    std::string radius_flag = "--fluid-source-radius";
    std::string radius_value = "0.08";
    std::string force_flag = "--fluid-source-force";
    std::string force_value = "9.5";
    std::string smoke_flag = "--fluid-smoke";
    std::string smoke_value = "7.25";
    std::string heat_flag = "--fluid-heat";
    std::string heat_value = "1.75";
    std::string flame_flag = "--fluid-flame";
    std::string flame_value = "2.5";
    std::string interval_flag = "--fluid-explosion-interval";
    std::string interval_value = "2.5";
    std::string duration_flag = "--fluid-explosion-duration";
    std::string duration_value = "0.18";
    std::string boost_flag = "--fluid-explosion-boost";
    std::string boost_value = "22.0";
    std::array<char*, 21> argv{program.data(),      scenario_flag.data(), scenario_value.data(),
                               sources_flag.data(), sources_value.data(),  radius_flag.data(),
                               radius_value.data(), force_flag.data(),    force_value.data(),
                               smoke_flag.data(),   smoke_value.data(),   heat_flag.data(),
                               heat_value.data(),   flame_flag.data(),    flame_value.data(),
                               interval_flag.data(), interval_value.data(), duration_flag.data(),
                               duration_value.data(), boost_flag.data(),  boost_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.fluid_scenario == "smoke-plume", "run config should parse fluid scenario");
    require(config.fluid_sources == 6, "run config should parse fluid source count");
    require(config.fluid_source_radius == 0.08F, "run config should parse fluid source radius");
    require(config.fluid_source_force == 9.5F, "run config should parse fluid source force");
    require(config.fluid_smoke == 7.25F, "run config should parse fluid smoke amount");
    require(config.fluid_heat == 1.75F, "run config should parse fluid heat amount");
    require(config.fluid_flame == 2.5F, "run config should parse fluid flame amount");
    require(config.fluid_explosion_interval_seconds == 2.5F,
            "run config should parse fluid explosion interval");
    require(config.fluid_explosion_duration_seconds == 0.18F,
            "run config should parse fluid explosion duration");
    require(config.fluid_explosion_boost == 22.0F,
            "run config should parse fluid explosion boost");
}

void test_run_config_parses_fluid_3d_fire_controls() {
    std::string program = "cubey";
    std::string ignition_flag = "--fluid-fire-ignition-temperature";
    std::string ignition_value = "0.31";
    std::string burn_rate_flag = "--fluid-fire-burn-rate";
    std::string burn_rate_value = "4.5";
    std::string heat_output_flag = "--fluid-fire-heat-output";
    std::string heat_output_value = "3.25";
    std::string soot_yield_flag = "--fluid-fire-soot-yield";
    std::string soot_yield_value = "0.22";
    std::string expansion_flag = "--fluid-fire-expansion";
    std::string expansion_value = "1.8";
    std::string cooling_flag = "--fluid-fire-flame-cooling";
    std::string cooling_value = "2.75";
    std::string shredding_flag = "--fluid-fire-shredding";
    std::string shredding_value = "3.5";
    std::string turbulence_flag = "--fluid-fire-turbulence";
    std::string turbulence_value = "0.85";
    std::array<char*, 17> argv{program.data(),
                               ignition_flag.data(),
                               ignition_value.data(),
                               burn_rate_flag.data(),
                               burn_rate_value.data(),
                               heat_output_flag.data(),
                               heat_output_value.data(),
                               soot_yield_flag.data(),
                               soot_yield_value.data(),
                               expansion_flag.data(),
                               expansion_value.data(),
                               cooling_flag.data(),
                               cooling_value.data(),
                               shredding_flag.data(),
                               shredding_value.data(),
                               turbulence_flag.data(),
                               turbulence_value.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.fluid_fire_ignition_temperature == 0.31F,
            "run config should parse fluid fire ignition");
    require(config.fluid_fire_burn_rate == 4.5F,
            "run config should parse fluid fire burn rate");
    require(config.fluid_fire_heat_output == 3.25F,
            "run config should parse fluid fire heat output");
    require(config.fluid_fire_soot_yield == 0.22F,
            "run config should parse fluid fire soot yield");
    require(config.fluid_fire_expansion == 1.8F,
            "run config should parse fluid fire expansion");
    require(config.fluid_fire_flame_cooling == 2.75F,
            "run config should parse fluid fire flame cooling");
    require(config.fluid_fire_shredding == 3.5F,
            "run config should parse fluid fire shredding");
    require(config.fluid_fire_turbulence == 0.85F,
            "run config should parse fluid fire turbulence");
}

void test_run_config_parses_obstacle_flag() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 12> obstacles_flag{'-', '-', 'o', 'b', 's', 't',
                                        'a', 'c', 'l', 'e', 's', '\0'};
    std::array<char*, 2> argv{program.data(), obstacles_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.obstacles, "run config should parse obstacle flag");
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
