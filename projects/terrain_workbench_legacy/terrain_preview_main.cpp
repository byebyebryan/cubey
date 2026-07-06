#include "terrain_preview_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                                  {
                                      .app_name = "terrain_workbench_preview_legacy",
                                      .default_title = "cubey terrain workbench preview",
                                  },
                              cubey::projects::terrain::run_terrain_preview);
}
