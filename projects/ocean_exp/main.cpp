#include "ocean_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "ocean_exp",
                                  .default_title = "cubey ocean exp",
                              },
                              cubey::projects::ocean_exp::run_ocean_exp);
}
