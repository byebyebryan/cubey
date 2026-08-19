#include "atmosphere_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "atmosphere", .default_title = "cubey atmosphere"},
        cubey::projects::atmosphere::parse_atmosphere_project_config,
        cubey::projects::atmosphere::run_atmosphere);
}
