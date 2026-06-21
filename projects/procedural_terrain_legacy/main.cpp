#include "procedural_terrain_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "procedural_terrain_legacy",
                                  .default_title = "cubey procedural terrain legacy",
                              },
                              cubey::projects::procedural_terrain::run_procedural_terrain);
}
