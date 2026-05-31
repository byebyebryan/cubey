#pragma once

#include "atmosphere_config.h"

#include <cubey/host/frame_stats.h>

#include <optional>
#include <string>

namespace cubey::projects::atmosphere {

struct AtmosphereLoadingStatus {
    bool moon_pending = false;
    bool night_sky_pending = false;
    bool moon_placeholder = false;
    bool night_sky_placeholder = false;
    std::string moon_error{};
    std::string night_sky_error{};
};

struct AtmosphereUiContext {
    AtmosphereConfig& config;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    AtmosphereRenderView& render_view;
    bool& reset_requested;
    const AtmosphereLoadingStatus& loading_status;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_atmosphere_ui(AtmosphereUiContext ui);

} // namespace cubey::projects::atmosphere
