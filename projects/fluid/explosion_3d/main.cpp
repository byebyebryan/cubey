#include "../sim/pyro_3d/pyro_3d_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    namespace pyro = cubey::projects::fluid::pyro_3d;
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "explosion_3d",
                                  .default_title = "cubey explosion 3D",
                              },
                              [](const cubey::RunConfig& config) {
                                  return pyro::run_pyro_3d(
                                      config,
                                      {
                                          .mode = pyro::Pyro3DMode::Explosion,
                                          .app_name = "explosion_3d",
                                          .ready_status = "rendering 3D explosion project",
                                          .ui_title = "Explosion 3D",
                                      });
                              });
}
