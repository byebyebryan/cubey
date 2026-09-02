#pragma once

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/engine/ocean_surface_runtime.h>
#include <cubey/engine/pbr_environment_schema.h>
#include <cubey/render/ocean_surface_config.h>

#include <filesystem>
#include <optional>
#include <string>

namespace cubey::projects::ocean {

using namespace cubey::render;
using OceanConfig = cubey::render::OceanSurfaceConfig;

struct OceanCaptureOptions {
    std::optional<float> video_orbit_degrees{};
};

struct OceanStartupOptions {
    struct Ocean : cubey::OceanSurfaceOptions {
        std::optional<bool> backdrop{};
        std::optional<float> foreground_height_m{};
        std::optional<std::string> camera_preset{};
        std::optional<float> camera_orbit_spin_degrees_per_second{};
        std::optional<bool> size_reference{};
        std::optional<int> cascade{};
        std::optional<float> wire_opacity{};
        bool wire_overlay = false;
    } ocean;

    OceanCaptureOptions capture{};

    cubey::AtmosphereEnvironmentOptions atmosphere;
    cubey::CloudEnvironmentOptions clouds;
    struct Pbr : cubey::PbrExposureOptions {
        std::filesystem::path environment_path{};
        std::string environment_source{};
        float ibl_intensity = 1.0F;
        float environment_rotation_degrees = 0.0F;
    } pbr;
    std::string debug_view{};
};

[[nodiscard]] inline OceanConfig ocean_config_from_options(const OceanStartupOptions& config) {
    OceanConfig ocean = cubey::ocean_surface_config_from_options(config.ocean);
    ocean.render_view = ocean_render_view_from_name(config.debug_view);
    ocean.exposure = config.pbr.exposure;
    validate_ocean_config(ocean);
    return ocean;
}

[[nodiscard]] inline cubey::CloudEnvironmentConfig
ocean_cloud_config_from_options(const OceanStartupOptions& config) {
    cubey::CloudEnvironmentConfig clouds{};
    clouds.enabled = true;
    cubey::apply_cloud_environment_options(clouds, config.clouds);
    clouds.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    clouds.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    clouds.layer.orbit_representation = cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
    return clouds;
}

} // namespace cubey::projects::ocean
