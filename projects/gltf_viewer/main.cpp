#include "gltf_viewer_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "gltf_viewer", .default_title = "cubey glTF viewer"},
        cubey::projects::gltf_viewer::parse_gltf_viewer_project_config,
        cubey::projects::gltf_viewer::run_gltf_viewer);
}
