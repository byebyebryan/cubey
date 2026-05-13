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
