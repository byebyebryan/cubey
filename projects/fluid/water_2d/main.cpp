#include <cubey/core/run_config.h>

#include "../sim/water_2d/water_2d_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "water_2d",
                                  .default_title = "cubey water 2D",
                              },
                              cubey::projects::fluid::water_2d::run_water_2d);
}
