#include "pbr_furnace_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(
        argc, argv,
        {
            .app_name = "pbr_furnace",
            .default_title = "cubey PBR furnace",
        },
        cubey::projects::pbr_furnace::run_pbr_furnace);
}
