#pragma once

#include "ocean_config.h"
#include "ocean_horizon.h"

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/host/performance_ui.h>

#include <cstdint>

namespace cubey::projects::ocean {

enum class OceanCameraPreset : std::uint32_t {
    Default = 0,
    Low = 1,
    Mid = 2,
    High = 3,
    Close = 4,
    Overhead = 5,
    Wide = 6,
};

struct OceanDiagnosticsConfig {
    int selected_cascade = -1;
    bool wire_overlay = false;
    bool size_reference_enabled = true;
    float wire_opacity = 0.65F;
    float shape_anti_repeat_strength = 1.0F;
    float detail_anti_repeat_strength = 1.0F;
};

struct OceanUiContext {
    OceanConfig& config;
    OceanDiagnosticsConfig& diagnostics;
    OceanConfig mesh_config;
    OceanHorizonDiagnostics horizon;
    cubey::AtmosphereEnvironmentRunState& atmosphere;
    cubey::host::PerformanceUiContext performance;
    OceanRenderView& render_view;
    OceanCameraPreset& camera_preset;
    bool& paused;
    bool& reset_requested;
    bool& step_requested;
    bool& camera_preset_requested;
    bool& atmosphere_changed;
};

void draw_ocean_ui(OceanUiContext ui);

} // namespace cubey::projects::ocean
