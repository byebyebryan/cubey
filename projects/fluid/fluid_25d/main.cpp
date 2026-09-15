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
        cubey::projects::fluid::fluid_25d::run_fluid_25d);
}
