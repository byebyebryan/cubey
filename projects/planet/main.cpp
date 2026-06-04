#include "planet_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "planet",
                                  .default_title = "cubey planet",
                              },
                              cubey::projects::planet::run_planet);
}
