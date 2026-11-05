#include "raytracing_config.h"
int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "raytracing", .default_title = "cubey ray tracing"},
        cubey::projects::raytracing::parse_config, cubey::projects::raytracing::run);
}
