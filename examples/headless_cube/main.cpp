#include <cubey/core/run_config.h>

#include "headless_cube_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "headless_cube",
                                  .default_title = "cubey headless_cube",
                              },
                              cubey::examples::headless_cube::run_headless_cube);
}
