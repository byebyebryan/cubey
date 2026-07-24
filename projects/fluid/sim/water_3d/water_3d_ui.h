#pragma once

#include "water_3d_config.h"

#include <cubey/host/performance_ui.h>
#include <cubey/render/terrain_backdrop_presentation.h>

namespace cubey {
struct AtmosphereEnvironmentRunState;
struct CloudEnvironmentConfig;
} // namespace cubey

namespace cubey::projects::fluid::water_3d {

class Water3DGpuResources;

struct Water3DTerrainUiState {
    bool& visible;
    float& foreground_height_m;
    float minimum_foreground_height_m;
    cubey::render::TerrainBackdropMaterialMode& material;
    bool& shadows;
};

struct Water3DUiContext {
    const char* title = nullptr;
    Water3DConfig& config;
    cubey::AtmosphereEnvironmentRunState* atmosphere = nullptr;
    cubey::CloudEnvironmentConfig* clouds = nullptr;
    Water3DTerrainUiState* terrain = nullptr;
    Water3DRuntimeState& runtime_state;
    Water3DGpuResources& resources;
    cubey::host::PerformanceUiContext performance;
    Water3DRenderView& render_view;
    bool& paused;
    bool& reset_requested;
};

struct Water3DUiResult {
    bool atmosphere_changed = false;
    bool clouds_changed = false;
};

[[nodiscard]] Water3DUiResult draw_water_3d_ui(Water3DUiContext ui);

} // namespace cubey::projects::fluid::water_3d
