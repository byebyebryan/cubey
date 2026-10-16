#pragma once
#include "../fluid/fluid_25d/fluid_25d_project_config.h"

namespace cubey::projects::terrain_water {
// Keep the established field/CLI ABI and capability validation. Project
// identity is separate from backend identity; a failed external source never
// silently becomes a different hydraulic experiment.
using TerrainWaterProjectConfig = fluid::fluid_25d::Fluid25DProjectConfig;

[[nodiscard]] inline TerrainWaterProjectConfig
parse_terrain_water_project_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    config::ParseResult parsed;
    auto config = fluid::fluid_25d::parse_fluid_25d_project_config(argc, argv, &parsed);
    if (!parsed.write_config_template_path &&
        fluid::fluid_25d::fluid_25d_selected_backend(config) == "builtin" &&
        config.backend != "builtin") {
        throw std::runtime_error(
            "Terrain Water needs an explicit producer: use projects/terrain_water/"
            "run_terrain_rain_demo.py --study PREPARED_DIR for managed live simulation, "
            "or supply --fluid25d-recording / --fluid25d-external-session. "
            "Use fluid_25d for standalone shallow water; --fluid25d-backend builtin "
            "here explicitly selects diagnostic-only computation, not equivalent terrain flow.");
    }
    if (result)
        *result = std::move(parsed);
    return config;
}
} // namespace cubey::projects::terrain_water
