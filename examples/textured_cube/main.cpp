#include <cubey/core/run_config.h>

#include "textured_cube_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "textured_cube",
                                  .default_title = "cubey textured_cube",
                              },
                              cubey::examples::textured_cube::run_textured_cube);
}
