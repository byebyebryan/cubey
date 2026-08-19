#include "material_cubes_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "material_cubes", .default_title = "cubey material_cubes"},
        cubey::examples::material_cubes::parse_material_cubes_config,
        cubey::examples::material_cubes::run_material_cubes);
}
