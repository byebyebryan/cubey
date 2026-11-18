#include "pbr_furnace_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv,
        {.app_name = "pbr_furnace", .default_title = "cubey PBR furnace",
         .temporal_bypass_reason = "conformance-diagnostic"},
        cubey::projects::pbr_furnace::parse_pbr_furnace_config,
        cubey::projects::pbr_furnace::run_pbr_furnace);
}
