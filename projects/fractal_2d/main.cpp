#include <cubey/core/run_config.h>

#include "fractal_2d_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "fractal_2d",
                                  .default_title = "cubey fractal_2d",
                              },
                              cubey::projects::fractal_2d::run_fractal_2d);
}
