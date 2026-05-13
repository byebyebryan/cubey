#include "gltf_viewer_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(
        argc, argv,
        {
            .app_name = "gltf_viewer",
            .default_title = "cubey glTF viewer",
        },
        cubey::projects::gltf_viewer::run_gltf_viewer);
}
