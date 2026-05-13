#include <cubey/core/run_config.h>

#include "fluid_2d_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "fluid_2d",
                                  .default_title = "cubey fluid 2d",
                              },
                              cubey::projects::fluid_2d::run_fluid_2d);
}
