#include <cubey/host/configured_app.h>

#include "../sim/water_2d/water_2d_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv,
        {
            .app_name = "water_2d",
            .default_title = "cubey water 2D",
        },
        cubey::projects::fluid::water_2d::parse_water_2d_project_config,
        cubey::projects::fluid::water_2d::run_water_2d);
}
