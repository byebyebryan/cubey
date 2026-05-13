#include <cubey/core/run_config.h>

#include "fractal_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "fractal",
                                  .default_title = "cubey fractal",
                              },
                              cubey::examples::fractal::run_fractal);
}
