#include "terrain_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "terrain", .default_title = "cubey terrain"},
        cubey::projects::terrain::parse_terrain_project_config,
        cubey::projects::terrain::run_terrain);
}
