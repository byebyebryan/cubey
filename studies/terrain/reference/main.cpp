#include "terrain_ref_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "terrain_reference",
                                  .default_title = "cubey terrain reference",
                              },
                              cubey::projects::terrain_ref::run_terrain_ref);
}
