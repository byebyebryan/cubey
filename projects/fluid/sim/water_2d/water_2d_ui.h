#pragma once

#include "water_2d_config.h"

#include <cubey/host/performance_ui.h>

namespace cubey::projects::fluid::water_2d {

class Water2DGpuResources;

struct Water2DUiContext {
    const char* title = nullptr;
    Water2DConfig& config;
    Water2DRuntimeState& runtime_state;
    Water2DGpuResources& resources;
    cubey::host::PerformanceUiContext performance;
    Water2DDebugView& debug_view;
    bool& paused;
    bool& reset_requested;
};

void draw_water_2d_ui(Water2DUiContext ui);

} // namespace cubey::projects::fluid::water_2d
