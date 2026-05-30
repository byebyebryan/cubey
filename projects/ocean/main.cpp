#include "ocean_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "ocean",
                                  .default_title = "cubey ocean",
                              },
                              cubey::projects::ocean::run_ocean);
}
