#include "terrain_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "terrain",
                                  .default_title = "cubey terrain",
                              },
                              cubey::projects::terrain::run_terrain);
}
