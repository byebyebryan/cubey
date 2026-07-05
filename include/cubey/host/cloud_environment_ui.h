#pragma once

#include <cubey/engine/cloud_environment_config.h>
#include <cubey/render/cloud_layer.h>

#include <cstdint>

namespace cubey::host {

struct CloudEnvironmentUiConfig {
    const char* label = "Clouds";
    bool default_open = false;
    std::uint32_t level = 0;
    const char* help = "Shared cloud layer composited into the atmosphere-backed scene.";
    bool show_aerial_orbit_controls = true;
    bool show_regime_status = false;
    cubey::render::CloudLayerViewRegime regime{};
    bool scene_depth_occlusion_enabled = false;
    float scene_depth_fade_m = 0.0F;
};

[[nodiscard]] bool draw_cloud_environment_controls(cubey::CloudEnvironmentConfig& clouds,
                                                   CloudEnvironmentUiConfig config = {});

} // namespace cubey::host
