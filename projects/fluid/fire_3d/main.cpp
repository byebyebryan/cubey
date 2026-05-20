#include "../sim/pyro_3d/pyro_3d_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    namespace pyro = cubey::projects::fluid::pyro_3d;
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "fire_3d",
                                  .default_title = "cubey fire 3D",
                              },
                              [](const cubey::RunConfig& config) {
                                  return pyro::run_pyro_3d(
                                      config,
                                      {
                                          .mode = pyro::Pyro3DMode::Fire,
                                          .app_name = "fire_3d",
                                          .ready_status = "rendering 3D fire project",
                                          .ui_title = "Fire 3D",
                                      });
                              });
}
