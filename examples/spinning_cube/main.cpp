#include "spinning_cube_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "spinning_cube", .default_title = "cubey spinning_cube"},
        cubey::examples::spinning_cube::parse_spinning_cube_config,
        cubey::examples::spinning_cube::run_spinning_cube);
}
