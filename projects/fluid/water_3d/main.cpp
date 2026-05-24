#include "../sim/water_3d/water_3d_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "water_3d",
                                  .default_title = "cubey water 3D",
                              },
                              [](const cubey::RunConfig& config) {
                                  return cubey::projects::fluid::water_3d::run_water_3d(config);
                              });
}
