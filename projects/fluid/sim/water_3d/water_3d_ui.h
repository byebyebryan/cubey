#pragma once

#include "water_3d_config.h"

#include <cubey/host/performance_ui.h>

namespace cubey {
struct AtmosphereEnvironmentRunState;
} // namespace cubey

namespace cubey::projects::fluid::water_3d {

class Water3DGpuResources;

struct Water3DUiContext {
    const char* title = nullptr;
    Water3DConfig& config;
    cubey::AtmosphereEnvironmentRunState* atmosphere = nullptr;
    Water3DRuntimeState& runtime_state;
    Water3DGpuResources& resources;
    cubey::host::PerformanceUiContext performance;
    Water3DRenderView& render_view;
    bool& paused;
    bool& reset_requested;
};

[[nodiscard]] bool draw_water_3d_ui(Water3DUiContext ui);

} // namespace cubey::projects::fluid::water_3d
