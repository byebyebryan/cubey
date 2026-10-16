#include "planet_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "planet", .default_title = "cubey planet"},
        cubey::projects::planet::parse_planet_config, cubey::projects::planet::run_planet);
}
