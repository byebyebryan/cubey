#pragma once

#include <cubey/engine/cloud_environment_config.h>

#include <cstdint>

namespace cubey::host {

struct CloudEnvironmentUiConfig {
    const char* label = "Clouds";
    bool default_open = false;
    std::uint32_t level = 0;
    const char* help =
        "Shared Cloud V1 surface-volume layer composited into the atmosphere-backed scene.";
    const char* enabled_help = "Composite the shared cloud layer in final view.";
    bool show_aerial_orbit_controls = false;
    bool show_regime_status = false;
    cubey::render::CloudLayerViewRegime regime{};
    bool scene_depth_occlusion_enabled = false;
    float scene_depth_fade_m = 0.0F;
};

[[nodiscard]] bool draw_cloud_environment_controls(cubey::CloudEnvironmentConfig& clouds,
                                                   CloudEnvironmentUiConfig config = {});

} // namespace cubey::host
