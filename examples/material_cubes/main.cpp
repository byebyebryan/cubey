#include <cubey/core/run_config.h>

#include "material_cubes_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "material_cubes",
                                  .default_title = "cubey material_cubes",
                              },
                              cubey::examples::material_cubes::run_material_cubes);
}
