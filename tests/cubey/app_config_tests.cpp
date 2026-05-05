#include <cubey/app_config.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_app_config_parses_png_output_path() {
    char program[] = "cubey";
    char output_flag[] = "--output";
    char output_value[] = "/tmp/cubey-headless-test.png";
    char* argv[] = {program, output_flag, output_value};

    const cubey::RunConfig config = cubey::parse_run_config(3, argv);
    require(config.output_path == output_value, "run config should preserve PNG output path");
}
