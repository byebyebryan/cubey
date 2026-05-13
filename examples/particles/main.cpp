#include <cubey/core/run_config.h>

#include "particles_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "particles",
                                  .default_title = "cubey particles",
                              },
                              cubey::examples::particles::run_particles);
}
