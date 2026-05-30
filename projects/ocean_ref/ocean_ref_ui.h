#pragma once

#include "ocean_ref_config.h"

#include <cubey/host/frame_stats.h>

#include <optional>

namespace cubey::projects::ocean_ref {

struct OceanRefDiagnosticsConfig {
    bool wire_overlay = false;
    float wire_opacity = 0.65F;
};

struct OceanRefUiContext {
    OceanRefConfig& config;
    OceanRefDiagnosticsConfig& diagnostics;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    OceanRefRenderView& render_view;
    bool& paused;
    bool& reset_requested;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_ocean_ref_ui(OceanRefUiContext ui);

} // namespace cubey::projects::ocean_ref
