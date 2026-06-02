#pragma once

#include "smoke_2d_config.h"

#include <cubey/host/performance_ui.h>

namespace cubey::projects::fluid::smoke_2d {

class Smoke2DGpuResources;

struct Smoke2DUiContext {
    const char* title = "Smoke 2D";
    Smoke2DConfig& config;
    Smoke2DDebugView& debug_view;
    cubey::host::PerformanceUiContext performance;
    const Smoke2DGpuResources& resources;
    bool& paused;
    bool& reset_requested;
    bool& reset_injectors_requested;
};

void draw_smoke_2d_ui(Smoke2DUiContext ui);

} // namespace cubey::projects::fluid::smoke_2d
