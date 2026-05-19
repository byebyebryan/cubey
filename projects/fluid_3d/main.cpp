#include "fluid_3d_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "fluid_3d",
                                  .default_title = "cubey fluid 3D",
                              },
                              [](const cubey::RunConfig& config) {
                                  return cubey::projects::fluid_3d::run_fluid_3d(config);
                              });
}
