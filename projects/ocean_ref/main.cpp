#include "ocean_ref_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "ocean_ref",
                                  .default_title = "cubey ocean ref",
                              },
                              cubey::projects::ocean_ref::run_ocean_ref);
}
