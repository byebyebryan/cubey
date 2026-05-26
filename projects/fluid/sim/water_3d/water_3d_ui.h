#pragma once

#include "water_3d_config.h"

#include <cubey/host/frame_stats.h>

#include <optional>

namespace cubey::vulkan {
class Device;
} // namespace cubey::vulkan

namespace cubey::projects::fluid::water_3d {

class Water3DGpuResources;

struct Water3DUiContext {
    const char* title = nullptr;
    Water3DConfig& config;
    Water3DRuntimeState& runtime_state;
    Water3DGpuResources& resources;
    cubey::vulkan::Device& device;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    Water3DRenderView& render_view;
    bool& paused;
    bool& reset_requested;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_water_3d_ui(Water3DUiContext ui);

} // namespace cubey::projects::fluid::water_3d
