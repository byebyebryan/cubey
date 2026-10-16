#include <cubey/host/configured_app.h>
#include "terrain_water_config.h"
#include "fluid_25d_app.h"

#include <cstdio>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::puts("Terrain Water — terrain-water simulation and rendering\n"
                  "Managed live/replay: python3 projects/terrain_water/run_terrain_rain_demo.py "
                  "[replay] --study PREPARED_DIR [--recording FILE]\n"
                  "Native attach: --fluid25d-backend external --fluid25d-external-session DIR\n"
                  "Native replay: --fluid25d-backend recording --fluid25d-recording FILE\n"
                  "Diagnostic alternative: --fluid25d-backend builtin\n"
                  "Established --fluid25d-* options remain compatible. No implicit producer, "
                  "downloads, solver launch or fallback. See projects/terrain_water/README.md.");
        return 0;
    }
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "terrain_water", .default_title = "cubey Terrain Water"},
        cubey::projects::terrain_water::parse_terrain_water_project_config,
        [](const cubey::projects::terrain_water::TerrainWaterProjectConfig& config) {
            if (cubey::projects::fluid::fluid_25d::fluid_25d_selected_backend(config) == "builtin")
                std::fputs("terrain_water: explicitly selected builtin diagnostic backend; "
                           "not equivalent to qualified SynxFlow terrain results.\n", stderr);
            return cubey::projects::fluid::fluid_25d::run_fluid_25d(config);
        });
}
