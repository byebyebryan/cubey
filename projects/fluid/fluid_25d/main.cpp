#include <cubey/host/configured_app.h>

#include "fluid_25d_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv,
        {
            .app_name = "fluid_25d",
            .default_title = "cubey Fluid 2.5D",
        },
        cubey::projects::fluid::fluid_25d::parse_fluid_25d_project_config,
        [](const cubey::projects::fluid::fluid_25d::Fluid25DProjectConfig& config) {
            if (cubey::projects::fluid::fluid_25d::fluid_25d_selected_backend(config) != "builtin")
                std::fputs("fluid_25d: compatibility external/replay entry; prefer terrain_water. "
                           "No backend or field conversion was performed.\n", stderr);
            return cubey::projects::fluid::fluid_25d::run_fluid_25d(config);
        });
}
