#include "shadow_cube_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "shadow_cube", .default_title = "cubey shadow_cube"},
        cubey::examples::shadow_cube::parse_shadow_cube_config,
        cubey::examples::shadow_cube::run_shadow_cube);
}
