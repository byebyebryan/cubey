#include <cubey/core/run_config.h>

#include "spinning_cube_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "spinning_cube",
                                  .default_title = "cubey spinning_cube",
                              },
                              cubey::examples::spinning_cube::run_spinning_cube);
}
