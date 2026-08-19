#include "fractal_2d_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "fractal_2d", .default_title = "cubey fractal_2d"},
        cubey::projects::fractal_2d::parse_fractal_2d_config,
        cubey::projects::fractal_2d::run_fractal_2d);
}
