#include <cubey/host/configured_app.h>

#include "smoke_2d_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv,
        {
            .app_name = "smoke_2d",
            .default_title = "cubey smoke 2D",
        },
        cubey::projects::fluid::smoke_2d::parse_smoke_2d_project_config,
        cubey::projects::fluid::smoke_2d::run_smoke_2d);
}
