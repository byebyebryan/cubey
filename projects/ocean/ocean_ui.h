#pragma once

#include "ocean_config.h"

#include <cubey/host/frame_stats.h>

#include <optional>

namespace cubey::projects::ocean {

struct OceanUiContext {
    OceanConfig& config;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    OceanRenderView& render_view;
    bool& paused;
    bool& reset_requested;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_ocean_ui(OceanUiContext ui);

} // namespace cubey::projects::ocean
