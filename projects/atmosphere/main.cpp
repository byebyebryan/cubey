#include "atmosphere_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "atmosphere",
                                  .default_title = "cubey atmosphere",
                              },
                              cubey::projects::atmosphere::run_atmosphere);
}
