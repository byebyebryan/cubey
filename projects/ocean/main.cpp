#include "ocean_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "ocean", .default_title = "cubey ocean"},
        cubey::projects::ocean::parse_ocean_project_config,
        cubey::projects::ocean::run_ocean);
}
