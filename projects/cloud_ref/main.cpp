#include "cloud_ref_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "cloud_ref",
                                  .default_title = "cubey cloud ref",
                              },
                              cubey::projects::cloud_ref::run_cloud_ref);
}
