#include "../sim/pyro_3d/pyro_3d_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    namespace pyro = cubey::projects::fluid::pyro_3d;
    return cubey::host::run_configured_app(
        argc, argv,
        {
            .app_name = "fire_3d",
            .default_title = "cubey fire 3D",
        },
        [](int parse_argc, char** parse_argv, cubey::config::ParseResult* result) {
            return pyro::parse_pyro_3d_project_config(parse_argc, parse_argv,
                                                      pyro::Pyro3DMode::Fire, result);
        },
        [](const pyro::Pyro3DProjectConfig& config) {
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
