#include "pbr_materials_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "pbr_materials", .default_title = "cubey pbr_materials"},
        cubey::projects::pbr_materials::parse_pbr_materials_config,
        cubey::projects::pbr_materials::run_pbr_materials);
}
