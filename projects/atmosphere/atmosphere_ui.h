#pragma once

#include "atmosphere_config.h"

#include <cubey/host/performance_ui.h>

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
    cubey::host::PerformanceUiContext performance;
    AtmosphereRenderView& render_view;
    bool& reset_requested;
    const AtmosphereLoadingStatus& loading_status;
};

void draw_atmosphere_ui(AtmosphereUiContext ui);

} // namespace cubey::projects::atmosphere
