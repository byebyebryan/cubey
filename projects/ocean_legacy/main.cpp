#include "ocean_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "ocean_legacy",
                                  .default_title = "cubey ocean legacy",
                              },
                              cubey::projects::ocean_legacy::run_ocean_legacy);
}
