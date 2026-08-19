#include "../sim/water_3d/water_3d_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv,
        {
            .app_name = "water_3d",
            .default_title = "cubey water 3D",
        },
        cubey::projects::fluid::water_3d::parse_water_3d_project_config,
        [](const cubey::projects::fluid::water_3d::Water3DProjectConfig& config) {
            return cubey::projects::fluid::water_3d::run_water_3d(config);
        });
}
