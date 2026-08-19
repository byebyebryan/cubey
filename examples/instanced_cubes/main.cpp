#include "instanced_cubes_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "instanced_cubes", .default_title = "cubey instanced_cubes"},
        cubey::examples::instanced_cubes::parse_instanced_cubes_config,
        cubey::examples::instanced_cubes::run_instanced_cubes);
}
