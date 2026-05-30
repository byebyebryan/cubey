#pragma once

#include "atmosphere_config.h"

#include <cubey/host/frame_stats.h>

#include <optional>

namespace cubey::projects::atmosphere {

struct AtmosphereUiContext {
    AtmosphereConfig& config;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    AtmosphereRenderView& render_view;
    bool& reset_requested;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_atmosphere_ui(AtmosphereUiContext ui);

} // namespace cubey::projects::atmosphere
