#include "cloud_ref_app.h"

#include <cubey/host/configured_app.h>

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "cloud_ref", .default_title = "cubey cloud ref"},
        cubey::projects::cloud_ref::parse_cloud_ref_project_config,
        cubey::projects::cloud_ref::run_cloud_ref);
}
