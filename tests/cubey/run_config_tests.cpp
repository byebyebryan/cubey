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

void test_run_config_parses_input_path() {
    std::array<char, 6> program{'c', 'u', 'b', 'e', 'y', '\0'};
    std::array<char, 8> input_flag{'-', '-', 'i', 'n', 'p', 'u', 't', '\0'};
    std::array<char, 25> input_value{'a', 's', 's', 'e', 't', 's', '/', 'D',
                                     'a', 'm', 'a', 'g', 'e', 'd', 'H', 'e',
                                     'l', 'm', 'e', 't', '.', 'g', 'l', 'b',
                                     '\0'};
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
    std::array<char, 31> rotation_flag{
        '-', '-', 'e', 'n', 'v', 'i', 'r', 'o', 'n', 'm', 'e', 'n', 't', '-',
        'r', 'o', 't', 'a', 't', 'i', 'o', 'n', '-', 'd', 'e', 'g', 'r', 'e',
        'e', 's', '\0'};
    std::array<char, 6> rotation_value{'4', '5', '.', '0', '0', '\0'};
    std::array<char, 11> exposure_flag{'-', '-', 'e', 'x', 'p', 'o', 's', 'u', 'r', 'e',
                                       '\0'};
    std::array<char, 5> exposure_value{'-', '0', '.', '5', '\0'};
    std::array<char*, 9> argv{
        program.data(),      environment_flag.data(), environment_value.data(),
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
    std::array<char*, 6> argv{program.data(), index_flag.data(), index_value.data(),
                              speed_flag.data(), speed_value.data(), pause_flag.data()};

    const cubey::RunConfig config =
        cubey::parse_run_config(static_cast<int>(argv.size()), argv.data());
    require(config.animation_index == 2, "run config should parse animation index");
    require(config.animation_speed == 0.5F, "run config should parse animation speed");
    require(config.animation_paused, "run config should parse animation pause flag");
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
