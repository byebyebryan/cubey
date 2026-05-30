#pragma once

#include "procedural_terrain_config.h"

namespace cubey::projects::procedural_terrain {

struct TerrainUiContext {
    TerrainConfig& active_config;
    TerrainConfig& edit_config;
    bool& water_visible;
    bool& rebuild_requested;
    bool& reset_camera_requested;
};

void draw_terrain_ui(TerrainUiContext ui);

} // namespace cubey::projects::procedural_terrain
