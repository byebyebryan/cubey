#include <cubey/core/run_config.h>

#include "smoke_2d_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "smoke_2d",
                                  .default_title = "cubey smoke 2D",
                              },
                              cubey::projects::fluid::smoke_2d::run_smoke_2d);
}
