#include <cubey/core/run_config.h>

#include "particle_cubes_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "particle_cubes",
                                  .default_title = "cubey particle_cubes",
                              },
                              cubey::examples::particle_cubes::run_particle_cubes);
}
