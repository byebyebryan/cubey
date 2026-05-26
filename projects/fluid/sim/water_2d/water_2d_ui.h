#pragma once

#include "water_2d_config.h"

#include <cubey/host/frame_stats.h>

#include <optional>

namespace cubey::vulkan {
class Device;
} // namespace cubey::vulkan

namespace cubey::projects::fluid::water_2d {

class Water2DGpuResources;

struct Water2DUiContext {
    const char* title = nullptr;
    Water2DConfig& config;
    Water2DRuntimeState& runtime_state;
    Water2DGpuResources& resources;
    cubey::vulkan::Device& device;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    Water2DDebugView& debug_view;
    bool& paused;
    bool& reset_requested;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_water_2d_ui(Water2DUiContext ui);

} // namespace cubey::projects::fluid::water_2d
