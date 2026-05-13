#include <cubey/core/run_config.h>

#include "shadow_cube_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "shadow_cube",
                                  .default_title = "cubey shadow_cube",
                              },
                              cubey::examples::shadow_cube::run_shadow_cube);
}
