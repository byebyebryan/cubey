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
