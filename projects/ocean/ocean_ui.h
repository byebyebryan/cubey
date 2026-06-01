#pragma once

#include "ocean_config.h"

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/host/frame_stats.h>

#include <cstdint>
#include <optional>

namespace cubey::projects::ocean {

enum class OceanCameraPreset : std::uint32_t {
    Default = 0,
    Low = 1,
    Close = 2,
    Overhead = 3,
    Wide = 4,
};

struct OceanDiagnosticsConfig {
    int selected_cascade = -1;
    bool wire_overlay = false;
    float wire_opacity = 0.65F;
    float anti_repeat_strength = 1.0F;
};

struct OceanUiContext {
    OceanConfig& config;
    OceanDiagnosticsConfig& diagnostics;
    cubey::AtmosphereEnvironmentRunState& atmosphere;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    OceanRenderView& render_view;
    OceanCameraPreset& camera_preset;
    bool& paused;
    bool& reset_requested;
    bool& step_requested;
    bool& camera_preset_requested;
    bool& atmosphere_changed;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_ocean_ui(OceanUiContext ui);

} // namespace cubey::projects::ocean
