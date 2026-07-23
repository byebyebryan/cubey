#pragma once

#include "atmosphere_config.h"

#include <cubey/host/performance_ui.h>
#include <cubey/render/atmosphere_atlas_cache.h>

#include <cstdint>
#include <optional>
#include <string>

namespace cubey::projects::atmosphere {

struct AtmosphereLoadingStatus {
    bool moon_pending = false;
    bool night_sky_pending = false;
    bool moon_placeholder = false;
    bool night_sky_placeholder = false;
    std::uint64_t generation = 0U;
    std::string phase{};
    double prepare_milliseconds = 0.0;
    double install_milliseconds = 0.0;
    std::optional<cubey::render::AtmosphereAtlasCacheDiagnostics> moon_cache{};
    std::optional<cubey::render::AtmosphereAtlasCacheDiagnostics> night_sky_cache{};
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
