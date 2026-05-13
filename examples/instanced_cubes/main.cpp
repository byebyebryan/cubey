#include <cubey/core/run_config.h>

#include "instanced_cubes_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "instanced_cubes",
                                  .default_title = "cubey instanced_cubes",
                              },
                              cubey::examples::instanced_cubes::run_instanced_cubes);
}
