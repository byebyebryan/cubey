#include "cloud_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "cloud",
                                  .default_title = "cubey cloud",
                              },
                              cubey::projects::cloud::run_cloud);
}
