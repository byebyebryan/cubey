#include "terrain_lab_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "terrain_lab_legacy",
                                  .default_title = "cubey terrain lab legacy",
                              },
                              cubey::projects::terrain_lab::run_terrain_lab);
}
