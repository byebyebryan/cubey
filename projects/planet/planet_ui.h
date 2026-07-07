#pragma once

#include "planet_camera.h"
#include "planet_celestial.h"
#include "planet_config.h"
#include "planet_frame.h"
#include "planet_local_detail.h"
#include "planet_surface.h"

#include <cubey/engine/cloud_environment_config.h>
#include <cubey/host/performance_ui.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/cloud_layer.h>

#include <vulkan/vulkan.h>

#include <functional>
#include <string>

namespace cubey::projects::planet {

struct PlanetUiContext {
    PlanetConfig& edit_config;
    const PlanetConfig& active_config;
    bool& config_apply_pending;
    std::string& rebuild_error;
    PlanetSolarTime& solar_time;
    cubey::render::AtmosphereEnvironmentConfig& atmosphere_look_config;
    cubey::CloudEnvironmentConfig& clouds_config;
    const PlanetSolarSystemConfig& solar_config;
    const PlanetCelestialSystem& celestial_system;
    const PlanetCelestialLighting& celestial_lighting;
    const PlanetFrame& frame;
    const PlanetCameraState& camera_state;
    PlanetExposureConfig& exposure_config;
    const PlanetSurfaceDiagnostics& surface_diagnostics;
    const PlanetLocalDetailDiagnostics& local_detail_diagnostics;
    float local_detail_surface_weight = 0.0F;
    cubey::render::CloudLayerViewRegime cloud_view_regime{};
    bool cloud_scene_depth_occlusion_enabled = false;
    float cloud_scene_depth_fade_m = 0.0F;
    cubey::host::PerformanceUiContext performance;
    VkExtent2D extent{};
    std::function<void()> reset_camera;
    std::function<void()> maybe_apply_config;
    std::function<void()> refresh_celestial_state;
    std::function<float(VkExtent2D)> view_light_fraction;
    std::function<float(VkExtent2D)> display_exposure;
};

void draw_planet_ui(PlanetUiContext ui);

} // namespace cubey::projects::planet
