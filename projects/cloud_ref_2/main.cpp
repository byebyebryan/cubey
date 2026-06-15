#include "cloud_ref_2_app.h"

#include <cubey/core/run_config.h>

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "cloud_ref_2",
                                  .default_title = "cubey cloud ref 2",
                              },
                              cubey::projects::cloud_ref_2::run_cloud_ref_2);
}
