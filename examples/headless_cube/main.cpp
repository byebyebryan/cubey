#include "headless_cube_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "headless_cube", .default_title = "cubey headless_cube"},
        cubey::examples::headless_cube::parse_headless_cube_config,
        cubey::examples::headless_cube::run_headless_cube);
}
