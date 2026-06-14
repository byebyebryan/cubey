#include "clouds_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "clouds_legacy",
                                  .default_title = "cubey clouds legacy",
                              },
                              cubey::projects::clouds::run_clouds);
}
