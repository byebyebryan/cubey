#pragma once

#include <cubey/render/cloud_layer_config.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey {

// Typed startup inputs consumed by the shared cloud environment runtime.
// Public projects bind their own schemas to this narrow subsystem fragment.
struct CloudEnvironmentOptions {
    std::optional<std::string> debug_view{};
    std::optional<std::string> quality{};
    std::optional<std::uint32_t> view_steps{};
    std::optional<std::uint32_t> view_samples{};
    std::optional<std::string> view_sample_mode{};
    std::optional<std::string> weather_preset{};
    std::optional<std::string> sampling_mode{};
    std::optional<std::string> density_model{};
    std::optional<std::string> resolve_mode{};
    std::optional<std::string> background_mode{};
    std::optional<std::string> distance_mode{};
    std::optional<std::string> orbit_representation{};
    std::optional<float> planet_radius_m{};
    std::optional<float> camera_altitude_m{};
    std::optional<float> bottom_altitude_m{};
    std::optional<float> top_altitude_m{};
    std::optional<float> coverage{};
    std::optional<float> density{};
    std::optional<float> weather_scale_km{};
    std::optional<float> shape_domain_km{};
    std::optional<float> footprint_filter_strength{};
    std::optional<float> edge_softness{};
    std::optional<float> edge_detail_fade{};
    std::optional<float> edge_resolve_strength{};
    std::optional<float> vertical_shear_fraction{};
    std::optional<float> wind_speed_mps{};
    std::optional<float> shadow_strength{};
    std::optional<float> horizon_strength{};
    std::optional<float> weather_fronts{};
    std::optional<float> weather_cells{};
    std::optional<float> weather_streaks{};
    std::optional<float> weather_softness{};
    std::optional<float> weather_influence{};
    std::optional<float> detail_erosion{};
    std::optional<float> ambient_strength{};
    std::optional<float> direct_strength{};
    std::optional<float> phase_strength{};
    std::optional<float> twilight_color_strength{};
    std::optional<float> twilight_edge_strength{};
    std::optional<float> twilight_saturation_strength{};
    std::optional<float> afterglow_strength{};
    std::optional<float> powder_strength{};
    std::optional<float> final_contrast{};
    std::optional<float> final_saturation{};
    std::optional<float> resolve_strength{};
    std::optional<float> resolve_radius_px{};
    std::optional<float> horizon_glow_strength{};
    std::optional<float> sun_glare_strength{};
    std::optional<float> jitter_strength{};
    std::optional<float> orbit_transition_start_m{};
    std::optional<float> orbit_transition_end_m{};
    std::optional<float> far_shell_start_m{};
    std::optional<float> far_shell_end_m{};
    std::optional<float> far_shell_strength{};
    std::optional<float> orbit_detail_strength{};
    std::optional<float> orbit_density_scale{};
    std::optional<float> orbit_fill{};
    std::optional<float> orbit_motion_strength{};
    std::optional<float> orbit_shell_extinction{};
    std::optional<bool> enabled{};
    std::optional<bool> temporal{};
    std::optional<bool> local_volume{};
    std::optional<bool> horizon_layer{};
};

inline void validate_cloud_environment_options(const CloudEnvironmentOptions& options) {
    if (options.view_samples && *options.view_samples != 1U && *options.view_samples != 2U &&
        *options.view_samples != 4U) {
        throw std::runtime_error("cloud view samples must be one of 1, 2, or 4");
    }
}

enum class CloudEnvironmentWeatherPreset : std::uint32_t {
    Clear = 0,
    FairWeather = 1,
    BrokenCumulus = 2,
    OvercastStratus = 3,
    StormCells = 4,
    HighCirrus = 5,
    SurfaceVolume = 6,
};

enum class CloudEnvironmentConfigPolicy {
    SurfaceV1Only,
    AllowDeferredDiagnostics,
};

inline constexpr std::array<CloudEnvironmentWeatherPreset, 7> kCloudEnvironmentWeatherPresets{
    CloudEnvironmentWeatherPreset::Clear,         CloudEnvironmentWeatherPreset::FairWeather,
    CloudEnvironmentWeatherPreset::BrokenCumulus, CloudEnvironmentWeatherPreset::OvercastStratus,
    CloudEnvironmentWeatherPreset::StormCells,    CloudEnvironmentWeatherPreset::HighCirrus,
    CloudEnvironmentWeatherPreset::SurfaceVolume,
};

[[nodiscard]] inline cubey::render::CloudLayerConfig default_cloud_environment_layer_config() {
    cubey::render::CloudLayerConfig config{};
    config.quality = cubey::render::CloudLayerQuality::Full;
    config.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    config.distance_mode = cubey::render::CloudLayerDistanceMode::Auto;
    config.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    config.resolve_mode = cubey::render::CloudLayerResolveMode::TerrainPost;
    config.debug_view = cubey::render::CloudLayerDebugView::Final;
    config.sampling_mode = cubey::render::CloudLayerSamplingMode::Bayer;
    config.temporal_enabled = false;
    config.bottom_altitude_m = 5000.0F;
    config.top_altitude_m = 15000.0F;
    config.coverage = 0.38F;
    config.density = 0.018F;
    config.weather_scale_km = 230.0F;
    config.shape_domain_km = 600.0F;
    config.footprint_filter_strength = 1.0F;
    config.edge_softness = 1.0F;
    config.edge_detail_fade = 0.75F;
    config.edge_resolve_strength = 0.70F;
    config.shadow_strength = 0.18F;
    config.horizon_strength = 0.62F;
    config.horizon_layer_enabled = true;
    config.view_steps_override = 64;
    config.view_samples = 1;
    config.weather_softness = 0.22F;
    config.detail_erosion = 1.0F;
    config.crispiness = 40.0F;
    config.curliness = 0.10F;
    config.absorption = 0.28F;
    config.ambient_strength = 1.22F;
    config.direct_strength = 1.25F;
    config.phase_strength = 1.42F;
    config.twilight_color_strength = 1.02F;
    config.twilight_edge_strength = 0.78F;
    config.twilight_saturation_strength = 1.05F;
    config.afterglow_strength = 0.40F;
    config.powder_strength = 0.32F;
    config.resolve_strength = 1.0F;
    config.final_contrast = 1.03F;
    config.final_saturation = 1.06F;
    config.horizon_glow_strength = 1.05F;
    config.sun_glare_strength = 1.10F;
    config.jitter_strength = 1.0F;
    return config;
}

struct CloudEnvironmentConfig {
    bool enabled = true;
    CloudEnvironmentWeatherPreset weather_preset = CloudEnvironmentWeatherPreset::SurfaceVolume;
    float wind_speed_mps = 260.0F;
    cubey::render::CloudLayerConfig layer = default_cloud_environment_layer_config();
};

inline constexpr std::array<cubey::render::CloudLayerQuality, 3> kCloudEnvironmentQualities{
    cubey::render::CloudLayerQuality::Quarter,
    cubey::render::CloudLayerQuality::Half,
    cubey::render::CloudLayerQuality::Full,
};

inline constexpr std::array<cubey::render::CloudLayerSamplingMode, 4>
    kCloudEnvironmentSamplingModes{
        cubey::render::CloudLayerSamplingMode::Interleaved,
        cubey::render::CloudLayerSamplingMode::Bayer,
        cubey::render::CloudLayerSamplingMode::BlueNoise,
        cubey::render::CloudLayerSamplingMode::Off,
    };

inline constexpr std::array<cubey::render::CloudLayerViewSampleMode, 2>
    kCloudEnvironmentViewSampleModes{
        cubey::render::CloudLayerViewSampleMode::SingleFrame,
        cubey::render::CloudLayerViewSampleMode::TemporalPhased,
    };

inline constexpr std::array<cubey::render::CloudLayerDistanceMode, 4>
    kCloudEnvironmentDistanceModes{
        cubey::render::CloudLayerDistanceMode::Auto,
        cubey::render::CloudLayerDistanceMode::Local,
        cubey::render::CloudLayerDistanceMode::OrbitShell,
        cubey::render::CloudLayerDistanceMode::BlendDebug,
    };

inline constexpr std::array<cubey::render::CloudLayerOrbitRepresentation, 2>
    kCloudEnvironmentOrbitRepresentations{
        cubey::render::CloudLayerOrbitRepresentation::VolumeRaymarch,
        cubey::render::CloudLayerOrbitRepresentation::SurfaceShell,
    };

inline constexpr std::array<cubey::render::CloudLayerDensityModel, 3>
    kCloudEnvironmentDensityModels{
        cubey::render::CloudLayerDensityModel::SurfaceVolume,
        cubey::render::CloudLayerDensityModel::ExperimentalAerialOrbit,
        cubey::render::CloudLayerDensityModel::RefDensity,
    };

inline constexpr std::array<cubey::render::CloudLayerResolveMode, 2> kCloudEnvironmentResolveModes{
    cubey::render::CloudLayerResolveMode::TerrainPost,
    cubey::render::CloudLayerResolveMode::MetadataBilateral,
};

[[nodiscard]] inline const char*
cloud_environment_weather_preset_name(CloudEnvironmentWeatherPreset preset) {
    switch (preset) {
    case CloudEnvironmentWeatherPreset::Clear:
        return "clear";
    case CloudEnvironmentWeatherPreset::FairWeather:
        return "fair-weather";
    case CloudEnvironmentWeatherPreset::BrokenCumulus:
        return "broken-cumulus";
    case CloudEnvironmentWeatherPreset::OvercastStratus:
        return "overcast-stratus";
    case CloudEnvironmentWeatherPreset::StormCells:
        return "storm-cells";
    case CloudEnvironmentWeatherPreset::HighCirrus:
        return "high-cirrus";
    case CloudEnvironmentWeatherPreset::SurfaceVolume:
        return "surface-volume";
    }
    return "broken-cumulus";
}

[[nodiscard]] inline CloudEnvironmentWeatherPreset
cloud_environment_weather_preset_from_name(std::string_view name) {
    if (name.empty() || name == "broken-cumulus" || name == "scattered" || name == "inspection") {
        return CloudEnvironmentWeatherPreset::BrokenCumulus;
    }
    if (name == "clear") {
        return CloudEnvironmentWeatherPreset::Clear;
    }
    if (name == "fair-weather") {
        return CloudEnvironmentWeatherPreset::FairWeather;
    }
    if (name == "overcast-stratus" || name == "overcast") {
        return CloudEnvironmentWeatherPreset::OvercastStratus;
    }
    if (name == "storm-cells" || name == "storm") {
        return CloudEnvironmentWeatherPreset::StormCells;
    }
    if (name == "high-cirrus") {
        return CloudEnvironmentWeatherPreset::HighCirrus;
    }
    if (name == "surface-volume" || name == "reference-parity" || name == "ref-parity" ||
        name == "cloud-ref-parity") {
        return CloudEnvironmentWeatherPreset::SurfaceVolume;
    }
    throw std::runtime_error("unknown atmosphere cloud weather preset: " + std::string(name));
}

[[nodiscard]] inline const char*
cloud_environment_quality_name(cubey::render::CloudLayerQuality quality) {
    switch (quality) {
    case cubey::render::CloudLayerQuality::Quarter:
        return "quarter";
    case cubey::render::CloudLayerQuality::Half:
        return "half";
    case cubey::render::CloudLayerQuality::Full:
        return "full";
    }
    return "half";
}

[[nodiscard]] inline cubey::render::CloudLayerQuality
cloud_environment_quality_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerQuality::Half;
    }
    for (const cubey::render::CloudLayerQuality quality : kCloudEnvironmentQualities) {
        if (name == cloud_environment_quality_name(quality)) {
            return quality;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud quality: " + std::string(name));
}

[[nodiscard]] inline const char*
cloud_environment_sampling_mode_name(cubey::render::CloudLayerSamplingMode mode) {
    switch (mode) {
    case cubey::render::CloudLayerSamplingMode::Interleaved:
        return "interleaved";
    case cubey::render::CloudLayerSamplingMode::Bayer:
        return "bayer";
    case cubey::render::CloudLayerSamplingMode::BlueNoise:
        return "blue-noise";
    case cubey::render::CloudLayerSamplingMode::Off:
        return "off";
    }
    return "bayer";
}

[[nodiscard]] inline cubey::render::CloudLayerSamplingMode
cloud_environment_sampling_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerSamplingMode::Bayer;
    }
    for (const cubey::render::CloudLayerSamplingMode mode : kCloudEnvironmentSamplingModes) {
        if (name == cloud_environment_sampling_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud sampling mode: " + std::string(name));
}

[[nodiscard]] inline const char*
cloud_environment_view_sample_mode_name(cubey::render::CloudLayerViewSampleMode mode) {
    switch (mode) {
    case cubey::render::CloudLayerViewSampleMode::SingleFrame:
        return "single-frame";
    case cubey::render::CloudLayerViewSampleMode::TemporalPhased:
        return "temporal-phased";
    }
    return "single-frame";
}

[[nodiscard]] inline cubey::render::CloudLayerViewSampleMode
cloud_environment_view_sample_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerViewSampleMode::SingleFrame;
    }
    for (const cubey::render::CloudLayerViewSampleMode mode : kCloudEnvironmentViewSampleModes) {
        if (name == cloud_environment_view_sample_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud view sample mode: " + std::string(name));
}

[[nodiscard]] inline const char*
cloud_environment_distance_mode_name(cubey::render::CloudLayerDistanceMode mode) {
    switch (mode) {
    case cubey::render::CloudLayerDistanceMode::Auto:
        return "auto";
    case cubey::render::CloudLayerDistanceMode::Local:
        return "local";
    case cubey::render::CloudLayerDistanceMode::OrbitShell:
        return "orbit-shell";
    case cubey::render::CloudLayerDistanceMode::BlendDebug:
        return "blend-debug";
    }
    return "local";
}

[[nodiscard]] inline cubey::render::CloudLayerDistanceMode
cloud_environment_distance_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerDistanceMode::Local;
    }
    for (const cubey::render::CloudLayerDistanceMode mode : kCloudEnvironmentDistanceModes) {
        if (name == cloud_environment_distance_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud distance mode: " + std::string(name));
}

[[nodiscard]] inline const char*
cloud_environment_orbit_representation_name(cubey::render::CloudLayerOrbitRepresentation mode) {
    switch (mode) {
    case cubey::render::CloudLayerOrbitRepresentation::VolumeRaymarch:
        return "volume";
    case cubey::render::CloudLayerOrbitRepresentation::SurfaceShell:
        return "surface-shell";
    }
    return "surface-shell";
}

[[nodiscard]] inline cubey::render::CloudLayerOrbitRepresentation
cloud_environment_orbit_representation_from_name(std::string_view name) {
    if (name.empty() || name == "surface-shell" || name == "shell") {
        return cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
    }
    if (name == "volume" || name == "raymarch" || name == "volume-raymarch") {
        return cubey::render::CloudLayerOrbitRepresentation::VolumeRaymarch;
    }
    throw std::runtime_error("unknown atmosphere cloud orbit representation: " + std::string(name));
}

[[nodiscard]] inline const char*
cloud_environment_density_model_name(cubey::render::CloudLayerDensityModel model) {
    switch (model) {
    case cubey::render::CloudLayerDensityModel::RefDensity:
        return "ref-density";
    case cubey::render::CloudLayerDensityModel::ExperimentalAerialOrbit:
        return "experimental-aerial-orbit";
    case cubey::render::CloudLayerDensityModel::SurfaceVolume:
        return "surface-volume";
    }
    return "surface-volume";
}

[[nodiscard]] inline cubey::render::CloudLayerDensityModel
cloud_environment_density_model_from_name(std::string_view name) {
    if (name.empty() || name == "surface-volume" || name == "surface" || name == "local-volume" ||
        name == "cloud-ref-compatible" || name == "cloud-ref" || name == "compatible" ||
        name == "reference-parity") {
        return cubey::render::CloudLayerDensityModel::SurfaceVolume;
    }
    if (name == "experimental-aerial-orbit" || name == "aerial-orbit" ||
        name == "legacy-procedural" || name == "procedural" || name == "active") {
        return cubey::render::CloudLayerDensityModel::ExperimentalAerialOrbit;
    }
    if (name == "ref-density" || name == "terrain-ref" || name == "terrain" || name == "ref") {
        return cubey::render::CloudLayerDensityModel::RefDensity;
    }
    throw std::runtime_error("unknown atmosphere cloud density model: " + std::string(name));
}

[[nodiscard]] inline const char*
cloud_environment_resolve_mode_name(cubey::render::CloudLayerResolveMode mode) {
    switch (mode) {
    case cubey::render::CloudLayerResolveMode::TerrainPost:
        return "terrain-post";
    case cubey::render::CloudLayerResolveMode::MetadataBilateral:
        return "metadata-bilateral";
    }
    return "terrain-post";
}

[[nodiscard]] inline cubey::render::CloudLayerResolveMode
cloud_environment_resolve_mode_from_name(std::string_view name) {
    if (name.empty() || name == "terrain-post" || name == "terrain" || name == "gaussian") {
        return cubey::render::CloudLayerResolveMode::TerrainPost;
    }
    if (name == "metadata-bilateral" || name == "bilateral" || name == "metadata") {
        return cubey::render::CloudLayerResolveMode::MetadataBilateral;
    }
    throw std::runtime_error("unknown atmosphere cloud resolve mode: " + std::string(name));
}

struct CloudEnvironmentWeatherPresetSettings {
    float coverage = 0.0F;
    float density = 0.0F;
    float weather_scale_km = 0.0F;
    float shape_domain_km = 600.0F;
    float vertical_shear_fraction = 0.0F;
    float wind_speed_mps = 0.0F;
    float bottom_altitude_m = 5000.0F;
    float top_altitude_m = 22000.0F;
    float horizon_strength = 0.48F;
    float horizon_glow_strength = 0.55F;
    cubey::render::CloudLayerCloudStyle cloud_style =
        cubey::render::CloudLayerCloudStyle::BrokenCumulus;
};

[[nodiscard]] inline CloudEnvironmentWeatherPresetSettings
cloud_environment_weather_preset_settings(CloudEnvironmentWeatherPreset preset) {
    switch (preset) {
    case CloudEnvironmentWeatherPreset::Clear:
        return {.coverage = 0.08F,
                .density = 0.007F,
                .weather_scale_km = 420.0F,
                .shape_domain_km = 2100.0F,
                .wind_speed_mps = 180.0F,
                .bottom_altitude_m = 6500.0F,
                .top_altitude_m = 16000.0F,
                .horizon_strength = 0.18F,
                .horizon_glow_strength = 0.24F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::FairWeather};
    case CloudEnvironmentWeatherPreset::FairWeather:
        return {.coverage = 0.30F,
                .density = 0.016F,
                .weather_scale_km = 260.0F,
                .shape_domain_km = 1300.0F,
                .wind_speed_mps = 260.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 17000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::FairWeather};
    case CloudEnvironmentWeatherPreset::BrokenCumulus:
        return {.coverage = 0.45F,
                .density = 0.020F,
                .weather_scale_km = 120.0F,
                .shape_domain_km = 600.0F,
                .wind_speed_mps = 450.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 22000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::BrokenCumulus};
    case CloudEnvironmentWeatherPreset::OvercastStratus:
        return {.coverage = 0.72F,
                .density = 0.018F,
                .weather_scale_km = 280.0F,
                .shape_domain_km = 1400.0F,
                .wind_speed_mps = 320.0F,
                .bottom_altitude_m = 3000.0F,
                .top_altitude_m = 12000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::OvercastStratus};
    case CloudEnvironmentWeatherPreset::StormCells:
        return {.coverage = 0.64F,
                .density = 0.032F,
                .weather_scale_km = 105.0F,
                .shape_domain_km = 525.0F,
                .wind_speed_mps = 650.0F,
                .bottom_altitude_m = 2500.0F,
                .top_altitude_m = 24000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::StormCells};
    case CloudEnvironmentWeatherPreset::HighCirrus:
        return {.coverage = 0.36F,
                .density = 0.010F,
                .weather_scale_km = 360.0F,
                .shape_domain_km = 1800.0F,
                .wind_speed_mps = 700.0F,
                .bottom_altitude_m = 11000.0F,
                .top_altitude_m = 22000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::HighCirrus};
    case CloudEnvironmentWeatherPreset::SurfaceVolume:
        return {.coverage = 0.38F,
                .density = 0.018F,
                .weather_scale_km = 230.0F,
                .shape_domain_km = 600.0F,
                .wind_speed_mps = 260.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 15000.0F,
                .horizon_strength = 0.62F,
                .horizon_glow_strength = 1.05F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::FairWeather};
    }
    return cloud_environment_weather_preset_settings(CloudEnvironmentWeatherPreset::SurfaceVolume);
}

inline void apply_cloud_environment_surface_volume(CloudEnvironmentConfig& config) {
    config.layer.quality = cubey::render::CloudLayerQuality::Full;
    config.layer.sampling_mode = cubey::render::CloudLayerSamplingMode::Bayer;
    config.layer.view_sample_mode = cubey::render::CloudLayerViewSampleMode::SingleFrame;
    config.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Auto;
    config.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    config.layer.resolve_mode = cubey::render::CloudLayerResolveMode::TerrainPost;
    config.layer.temporal_enabled = false;
    config.layer.local_volume_enabled = true;
    config.layer.horizon_layer_enabled = true;
    config.layer.view_steps_override = 64;
    config.layer.view_samples = 1;
    config.layer.footprint_filter_strength = 1.0F;
    config.layer.edge_softness = 1.0F;
    config.layer.edge_detail_fade = 0.75F;
    config.layer.edge_resolve_strength = 0.70F;
    config.layer.shadow_strength = 0.18F;
    config.layer.ambient_strength = 1.22F;
    config.layer.direct_strength = 1.25F;
    config.layer.phase_strength = 1.42F;
    config.layer.twilight_color_strength = 1.02F;
    config.layer.twilight_edge_strength = 0.78F;
    config.layer.twilight_saturation_strength = 1.05F;
    config.layer.afterglow_strength = 0.40F;
    config.layer.powder_strength = 0.32F;
    config.layer.final_contrast = 1.03F;
    config.layer.final_saturation = 1.06F;
    config.layer.resolve_strength = 1.0F;
    config.layer.sun_glare_strength = 1.10F;
    config.layer.jitter_strength = 1.0F;
}

inline void apply_cloud_environment_surface_v1_policy(CloudEnvironmentConfig& config) {
    config.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    if (config.layer.distance_mode == cubey::render::CloudLayerDistanceMode::OrbitShell ||
        config.layer.distance_mode == cubey::render::CloudLayerDistanceMode::BlendDebug) {
        config.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Auto;
    }
    config.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    config.layer.orbit_representation = cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
    config.layer.resolve_mode = cubey::render::CloudLayerResolveMode::TerrainPost;
    config.layer.view_sample_mode = cubey::render::CloudLayerViewSampleMode::SingleFrame;
    config.layer.temporal_enabled = false;
    config.layer.local_volume_enabled = true;
}

inline void apply_cloud_environment_weather_preset(CloudEnvironmentConfig& config,
                                                   CloudEnvironmentWeatherPreset preset) {
    config.weather_preset = preset;
    const CloudEnvironmentWeatherPresetSettings settings =
        cloud_environment_weather_preset_settings(preset);
    config.layer.coverage = settings.coverage;
    config.layer.density = settings.density;
    config.layer.weather_scale_km = settings.weather_scale_km;
    config.layer.shape_domain_km = settings.shape_domain_km;
    config.layer.vertical_shear_fraction = settings.vertical_shear_fraction;
    config.wind_speed_mps = settings.wind_speed_mps;
    config.layer.bottom_altitude_m = settings.bottom_altitude_m;
    config.layer.top_altitude_m = settings.top_altitude_m;
    config.layer.horizon_strength = settings.horizon_strength;
    config.layer.horizon_glow_strength = settings.horizon_glow_strength;
    config.layer.cloud_style = settings.cloud_style;
    if (preset == CloudEnvironmentWeatherPreset::SurfaceVolume) {
        apply_cloud_environment_surface_volume(config);
    }
}

inline void apply_cloud_environment_options(
    CloudEnvironmentConfig& config,
    const CloudEnvironmentOptions& run_clouds,
    CloudEnvironmentConfigPolicy policy = CloudEnvironmentConfigPolicy::SurfaceV1Only) {
    validate_cloud_environment_options(run_clouds);
    auto apply_float = [](const std::optional<float>& value, float& target) {
        if (value) {
            target = *value;
        }
    };

    if (run_clouds.weather_preset) {
        apply_cloud_environment_weather_preset(
            config, cloud_environment_weather_preset_from_name(*run_clouds.weather_preset));
    }
    if (run_clouds.quality) {
        config.layer.quality = cloud_environment_quality_from_name(*run_clouds.quality);
    }
    if (run_clouds.enabled) {
        config.enabled = *run_clouds.enabled;
    }
    if (run_clouds.debug_view) {
        config.layer.debug_view =
            cubey::render::cloud_layer_debug_view_from_name(*run_clouds.debug_view);
    }
    if (run_clouds.sampling_mode) {
        config.layer.sampling_mode =
            cloud_environment_sampling_mode_from_name(*run_clouds.sampling_mode);
    }
    if (run_clouds.view_sample_mode) {
        config.layer.view_sample_mode =
            cloud_environment_view_sample_mode_from_name(*run_clouds.view_sample_mode);
    }
    if (run_clouds.distance_mode) {
        config.layer.distance_mode =
            cloud_environment_distance_mode_from_name(*run_clouds.distance_mode);
    }
    if (run_clouds.orbit_representation) {
        config.layer.orbit_representation =
            cloud_environment_orbit_representation_from_name(*run_clouds.orbit_representation);
    }
    if (run_clouds.density_model) {
        config.layer.density_model =
            cloud_environment_density_model_from_name(*run_clouds.density_model);
    }
    if (run_clouds.resolve_mode) {
        config.layer.resolve_mode =
            cloud_environment_resolve_mode_from_name(*run_clouds.resolve_mode);
    }
    if (run_clouds.view_steps) {
        config.layer.view_steps_override = static_cast<std::int32_t>(*run_clouds.view_steps);
    }
    if (run_clouds.view_samples) {
        config.layer.view_samples = static_cast<std::int32_t>(*run_clouds.view_samples);
    }

    apply_float(run_clouds.bottom_altitude_m, config.layer.bottom_altitude_m);
    apply_float(run_clouds.top_altitude_m, config.layer.top_altitude_m);
    apply_float(run_clouds.planet_radius_m, config.layer.planet_radius_m);
    apply_float(run_clouds.coverage, config.layer.coverage);
    apply_float(run_clouds.density, config.layer.density);
    apply_float(run_clouds.weather_scale_km, config.layer.weather_scale_km);
    apply_float(run_clouds.shape_domain_km, config.layer.shape_domain_km);
    apply_float(run_clouds.footprint_filter_strength, config.layer.footprint_filter_strength);
    apply_float(run_clouds.edge_softness, config.layer.edge_softness);
    apply_float(run_clouds.edge_detail_fade, config.layer.edge_detail_fade);
    apply_float(run_clouds.edge_resolve_strength, config.layer.edge_resolve_strength);
    apply_float(run_clouds.vertical_shear_fraction, config.layer.vertical_shear_fraction);
    apply_float(run_clouds.wind_speed_mps, config.wind_speed_mps);
    apply_float(run_clouds.shadow_strength, config.layer.shadow_strength);
    apply_float(run_clouds.horizon_strength, config.layer.horizon_strength);
    apply_float(run_clouds.weather_fronts, config.layer.weather_fronts);
    apply_float(run_clouds.weather_cells, config.layer.weather_cells);
    apply_float(run_clouds.weather_streaks, config.layer.weather_streaks);
    apply_float(run_clouds.weather_softness, config.layer.weather_softness);
    apply_float(run_clouds.weather_influence, config.layer.weather_influence);
    apply_float(run_clouds.detail_erosion, config.layer.detail_erosion);
    apply_float(run_clouds.ambient_strength, config.layer.ambient_strength);
    apply_float(run_clouds.direct_strength, config.layer.direct_strength);
    apply_float(run_clouds.phase_strength, config.layer.phase_strength);
    apply_float(run_clouds.twilight_color_strength, config.layer.twilight_color_strength);
    apply_float(run_clouds.twilight_edge_strength, config.layer.twilight_edge_strength);
    apply_float(run_clouds.twilight_saturation_strength, config.layer.twilight_saturation_strength);
    apply_float(run_clouds.afterglow_strength, config.layer.afterglow_strength);
    apply_float(run_clouds.powder_strength, config.layer.powder_strength);
    apply_float(run_clouds.final_contrast, config.layer.final_contrast);
    apply_float(run_clouds.final_saturation, config.layer.final_saturation);
    apply_float(run_clouds.resolve_strength, config.layer.resolve_strength);
    apply_float(run_clouds.horizon_glow_strength, config.layer.horizon_glow_strength);
    apply_float(run_clouds.sun_glare_strength, config.layer.sun_glare_strength);
    apply_float(run_clouds.jitter_strength, config.layer.jitter_strength);
    apply_float(run_clouds.orbit_transition_start_m, config.layer.orbit_transition_start_m);
    apply_float(run_clouds.orbit_transition_end_m, config.layer.orbit_transition_end_m);
    apply_float(run_clouds.far_shell_start_m, config.layer.far_shell_start_m);
    apply_float(run_clouds.far_shell_end_m, config.layer.far_shell_end_m);
    apply_float(run_clouds.far_shell_strength, config.layer.far_shell_strength);
    apply_float(run_clouds.orbit_detail_strength, config.layer.orbit_detail_strength);
    apply_float(run_clouds.orbit_density_scale, config.layer.orbit_density_scale);
    apply_float(run_clouds.orbit_fill, config.layer.orbit_fill);
    apply_float(run_clouds.orbit_motion_strength, config.layer.orbit_motion_strength);
    apply_float(run_clouds.orbit_shell_extinction, config.layer.orbit_shell_extinction);

    if (run_clouds.temporal) {
        config.layer.temporal_enabled = *run_clouds.temporal;
    }
    if (run_clouds.local_volume) {
        config.layer.local_volume_enabled = *run_clouds.local_volume;
    }
    if (run_clouds.horizon_layer) {
        config.layer.horizon_layer_enabled = *run_clouds.horizon_layer;
    }
    config.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    if (policy == CloudEnvironmentConfigPolicy::SurfaceV1Only) {
        apply_cloud_environment_surface_v1_policy(config);
    }
}

} // namespace cubey
