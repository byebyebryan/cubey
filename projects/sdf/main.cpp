#include "sdf_config.h"
int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "sdf", .default_title = "cubey sdf"},
        cubey::projects::sdf::parse_sdf_config, cubey::projects::sdf::run_sdf);
}
