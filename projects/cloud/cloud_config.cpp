#include "cloud_config.h"

#include <cubey/procedural/artifact_metadata.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::projects::cloud {
namespace {

[[nodiscard]] bool finite_positive(float value) {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] bool finite_nonnegative(float value) {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] cubey::procedural::ProceduralArtifactMetadata cloud_volume_metadata(
    std::string name, std::string generator, std::string formula_version, std::string domain,
    std::uint32_t size) {
    return cubey::procedural::make_procedural_artifact_metadata(
        cubey::procedural::make_domain_procedural_artifact_identity(
            std::move(name),
            std::move(generator),
            std::move(formula_version),
            std::move(domain),
            cubey::procedural::ProceduralDomainSpace::Volume),
        cubey::procedural::ProceduralArtifactKind::Volume3D,
        cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
        {.width = size,
         .height = size,
         .depth = size,
         .faces = 1U,
         .mip_levels = cloud_generated_volume_mip_count(size)},
        0U);
}

struct CloudsWeatherPresetSettings {
    float coverage = 0.0F;
    float density = 0.0F;
    float weather_scale_km = 0.0F;
    float vertical_shear_fraction = 0.0F;
    float wind_speed_mps = 0.0F;
    float bottom_altitude_m = kCloudsDefaultBottomAltitudeM;
    float top_altitude_m = kCloudsDefaultTopAltitudeM;
    float horizon_strength = 0.48F;
    float horizon_glow_strength = 0.55F;
    CloudsCloudStyle cloud_style = CloudsCloudStyle::BrokenCumulus;
};

[[nodiscard]] CloudsWeatherPresetSettings clouds_weather_preset_settings(
    CloudsWeatherPreset preset) {
    switch (preset) {
    case CloudsWeatherPreset::Clear:
        return {.coverage = 0.08F,
                .density = 0.007F,
                .weather_scale_km = 420.0F,
                .wind_speed_mps = 180.0F,
                .bottom_altitude_m = 6500.0F,
                .top_altitude_m = 16000.0F,
                .horizon_strength = 0.18F,
                .horizon_glow_strength = 0.24F,
                .cloud_style = CloudsCloudStyle::FairWeather};
    case CloudsWeatherPreset::FairWeather:
        return {.coverage = 0.30F,
                .density = 0.016F,
                .weather_scale_km = 260.0F,
                .wind_speed_mps = 260.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 17000.0F,
                .cloud_style = CloudsCloudStyle::FairWeather};
    case CloudsWeatherPreset::BrokenCumulus:
        return {.coverage = 0.45F,
                .density = 0.020F,
                .weather_scale_km = 120.0F,
                .wind_speed_mps = 450.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 22000.0F,
                .cloud_style = CloudsCloudStyle::BrokenCumulus};
    case CloudsWeatherPreset::OvercastStratus:
        return {.coverage = 0.72F,
                .density = 0.018F,
                .weather_scale_km = 280.0F,
                .wind_speed_mps = 320.0F,
                .bottom_altitude_m = 3000.0F,
                .top_altitude_m = 12000.0F,
                .cloud_style = CloudsCloudStyle::OvercastStratus};
    case CloudsWeatherPreset::StormCells:
        return {.coverage = 0.64F,
                .density = 0.032F,
                .weather_scale_km = 105.0F,
                .wind_speed_mps = 650.0F,
                .bottom_altitude_m = 2500.0F,
                .top_altitude_m = 24000.0F,
                .cloud_style = CloudsCloudStyle::StormCells};
    case CloudsWeatherPreset::HighCirrus:
        return {.coverage = 0.36F,
                .density = 0.010F,
                .weather_scale_km = 360.0F,
                .wind_speed_mps = 700.0F,
                .bottom_altitude_m = 11000.0F,
                .top_altitude_m = 22000.0F,
                .cloud_style = CloudsCloudStyle::HighCirrus};
    }
    return clouds_weather_preset_settings(CloudsWeatherPreset::BrokenCumulus);
}

} // namespace

void apply_clouds_weather_preset(CloudsConfig& config, CloudsWeatherPreset preset) {
    config.weather_preset = preset;
    const CloudsWeatherPresetSettings settings = clouds_weather_preset_settings(preset);
    config.coverage = settings.coverage;
    config.density = settings.density;
    config.weather_scale_km = settings.weather_scale_km;
    config.vertical_shear_fraction = settings.vertical_shear_fraction;
    config.wind_speed_mps = settings.wind_speed_mps;
    config.bottom_altitude_m = settings.bottom_altitude_m;
    config.top_altitude_m = settings.top_altitude_m;
    config.horizon_strength = settings.horizon_strength;
    config.horizon_glow_strength = settings.horizon_glow_strength;
    config.cloud_style = settings.cloud_style;
}

CloudsCameraMode clouds_camera_mode_from_string(std::string_view value) {
    if (value == "surface" || value == "surface-horizon") {
        return CloudsCameraMode::Surface;
    }
    if (value == "surface-up") {
        return CloudsCameraMode::SurfaceUp;
    }
    if (value == "high" || value == "high-top") {
        return CloudsCameraMode::High;
    }
    if (value == "high-oblique") {
        return CloudsCameraMode::HighOblique;
    }
    if (value == "orbit" || value == "orbit-day") {
        return CloudsCameraMode::Orbit;
    }
    if (value == "orbit-terminator") {
        return CloudsCameraMode::OrbitTerminator;
    }
    throw std::runtime_error("unknown cloud camera mode: " + std::string(value));
}

const char* clouds_camera_mode_name(CloudsCameraMode mode) {
    switch (mode) {
    case CloudsCameraMode::Surface:
        return "surface";
    case CloudsCameraMode::SurfaceUp:
        return "surface-up";
    case CloudsCameraMode::High:
        return "high";
    case CloudsCameraMode::HighOblique:
        return "high-oblique";
    case CloudsCameraMode::Orbit:
        return "orbit";
    case CloudsCameraMode::OrbitTerminator:
        return "orbit-terminator";
    }
    return "surface";
}

CloudsQuality clouds_quality_from_string(std::string_view value) {
    if (value == "quarter") {
        return CloudsQuality::Quarter;
    }
    if (value == "half") {
        return CloudsQuality::Half;
    }
    if (value == "full") {
        return CloudsQuality::Full;
    }
    throw std::runtime_error("unknown cloud quality: " + std::string(value));
}

const char* clouds_quality_name(CloudsQuality quality) {
    switch (quality) {
    case CloudsQuality::Quarter:
        return "quarter";
    case CloudsQuality::Half:
        return "half";
    case CloudsQuality::Full:
        return "full";
    }
    return "half";
}

CloudsWeatherPreset clouds_weather_preset_from_string(std::string_view value) {
    if (value == "clear") {
        return CloudsWeatherPreset::Clear;
    }
    if (value == "fair-weather") {
        return CloudsWeatherPreset::FairWeather;
    }
    if (value.empty() || value == "broken-cumulus" || value == "scattered" ||
        value == "inspection") {
        return CloudsWeatherPreset::BrokenCumulus;
    }
    if (value == "overcast-stratus" || value == "overcast") {
        return CloudsWeatherPreset::OvercastStratus;
    }
    if (value == "storm-cells" || value == "storm") {
        return CloudsWeatherPreset::StormCells;
    }
    if (value == "high-cirrus") {
        return CloudsWeatherPreset::HighCirrus;
    }
    throw std::runtime_error("unknown cloud weather preset: " + std::string(value));
}

const char* clouds_weather_preset_name(CloudsWeatherPreset preset) {
    switch (preset) {
    case CloudsWeatherPreset::Clear:
        return "clear";
    case CloudsWeatherPreset::FairWeather:
        return "fair-weather";
    case CloudsWeatherPreset::BrokenCumulus:
        return "broken-cumulus";
    case CloudsWeatherPreset::OvercastStratus:
        return "overcast-stratus";
    case CloudsWeatherPreset::StormCells:
        return "storm-cells";
    case CloudsWeatherPreset::HighCirrus:
        return "high-cirrus";
    }
    return "broken-cumulus";
}

CloudsSamplingMode clouds_sampling_mode_from_string(std::string_view value) {
    if (value.empty() || value == "interleaved") {
        return CloudsSamplingMode::Interleaved;
    }
    if (value == "bayer") {
        return CloudsSamplingMode::Bayer;
    }
    if (value == "off" || value == "center") {
        return CloudsSamplingMode::Off;
    }
    throw std::runtime_error("unknown cloud sampling mode: " + std::string(value));
}

const char* clouds_sampling_mode_name(CloudsSamplingMode mode) {
    switch (mode) {
    case CloudsSamplingMode::Interleaved:
        return "interleaved";
    case CloudsSamplingMode::Bayer:
        return "bayer";
    case CloudsSamplingMode::Off:
        return "off";
    }
    return "interleaved";
}

CloudsBackgroundMode clouds_background_mode_from_string(std::string_view value) {
    if (value.empty() || value == "atmosphere" || value == "sky") {
        return CloudsBackgroundMode::Atmosphere;
    }
    if (value == "water-context" || value == "water") {
        return CloudsBackgroundMode::WaterContext;
    }
    throw std::runtime_error("unknown cloud background mode: " + std::string(value));
}

const char* clouds_background_mode_name(CloudsBackgroundMode mode) {
    switch (mode) {
    case CloudsBackgroundMode::Atmosphere:
        return "atmosphere";
    case CloudsBackgroundMode::WaterContext:
        return "water-context";
    }
    return "atmosphere";
}

CloudsDistanceMode clouds_distance_mode_from_string(std::string_view value) {
    if (value.empty() || value == "auto") {
        return CloudsDistanceMode::Auto;
    }
    if (value == "local") {
        return CloudsDistanceMode::Local;
    }
    if (value == "orbit-shell" || value == "orbit") {
        return CloudsDistanceMode::OrbitShell;
    }
    if (value == "blend-debug") {
        return CloudsDistanceMode::BlendDebug;
    }
    throw std::runtime_error("unknown cloud distance mode: " + std::string(value));
}

const char* clouds_distance_mode_name(CloudsDistanceMode mode) {
    switch (mode) {
    case CloudsDistanceMode::Auto:
        return "auto";
    case CloudsDistanceMode::Local:
        return "local";
    case CloudsDistanceMode::OrbitShell:
        return "orbit-shell";
    case CloudsDistanceMode::BlendDebug:
        return "blend-debug";
    }
    return "auto";
}

CloudsOrbitRepresentation clouds_orbit_representation_from_string(std::string_view value) {
    if (value.empty() || value == "volume" || value == "raymarch" ||
        value == "volume-raymarch") {
        return CloudsOrbitRepresentation::VolumeRaymarch;
    }
    if (value == "surface-shell" || value == "shell") {
        return CloudsOrbitRepresentation::SurfaceShell;
    }
    throw std::runtime_error("unknown cloud orbit representation: " + std::string(value));
}

const char* clouds_orbit_representation_name(CloudsOrbitRepresentation mode) {
    switch (mode) {
    case CloudsOrbitRepresentation::VolumeRaymarch:
        return "volume";
    case CloudsOrbitRepresentation::SurfaceShell:
        return "surface-shell";
    }
    return "volume";
}

CloudsDebugView clouds_debug_view_from_string(std::string_view value) {
    if (value.empty() || value == "final") {
        return CloudsDebugView::Final;
    }
    if (value == "raw-final") {
        return CloudsDebugView::RawFinal;
    }
    if (value == "weather") {
        return CloudsDebugView::Weather;
    }
    if (value == "density") {
        return CloudsDebugView::Density;
    }
    if (value == "transmittance") {
        return CloudsDebugView::Transmittance;
    }
    if (value == "lighting") {
        return CloudsDebugView::Lighting;
    }
    if (value == "ambient-light") {
        return CloudsDebugView::AmbientLight;
    }
    if (value == "direct-light") {
        return CloudsDebugView::DirectLight;
    }
    if (value == "phase-light") {
        return CloudsDebugView::PhaseLight;
    }
    if (value == "shadow") {
        return CloudsDebugView::Shadow;
    }
    if (value == "steps") {
        return CloudsDebugView::Steps;
    }
    if (value == "background") {
        return CloudsDebugView::Background;
    }
    if (value == "cloud-alpha") {
        return CloudsDebugView::CloudAlpha;
    }
    if (value == "distance") {
        return CloudsDebugView::Distance;
    }
    if (value == "metadata-distance") {
        return CloudsDebugView::MetadataDistance;
    }
    if (value == "metadata-alpha") {
        return CloudsDebugView::MetadataAlpha;
    }
    if (value == "metadata-confidence") {
        return CloudsDebugView::MetadataConfidence;
    }
    if (value == "metadata-density") {
        return CloudsDebugView::MetadataDensity;
    }
    if (value == "base-density") {
        return CloudsDebugView::BaseDensity;
    }
    if (value == "detail-density") {
        return CloudsDebugView::DetailDensity;
    }
    if (value == "cloud-type") {
        return CloudsDebugView::CloudType;
    }
    if (value == "weather-edge") {
        return CloudsDebugView::WeatherEdge;
    }
    if (value == "weather-bias" || value == "weather-mask") {
        return CloudsDebugView::WeatherBias;
    }
    if (value == "visible-density") {
        return CloudsDebugView::VisibleDensity;
    }
    if (value == "visible-cloud-type") {
        return CloudsDebugView::VisibleCloudType;
    }
    if (value == "distance-regime") {
        return CloudsDebugView::DistanceRegime;
    }
    if (value == "local-alpha") {
        return CloudsDebugView::LocalAlpha;
    }
    if (value == "orbit-alpha") {
        return CloudsDebugView::OrbitAlpha;
    }
    if (value == "orbit-weather" || value == "orbit-coverage") {
        return CloudsDebugView::OrbitCoverage;
    }
    if (value == "orbit-detail") {
        return CloudsDebugView::OrbitDetail;
    }
    if (value == "orbit-hull") {
        return CloudsDebugView::OrbitHull;
    }
    if (value == "orbit-envelope") {
        return CloudsDebugView::OrbitEnvelope;
    }
    if (value == "orbit-shell-alpha" || value == "shell-alpha") {
        return CloudsDebugView::OrbitShellAlpha;
    }
    if (value == "orbit-shell-height" || value == "shell-height") {
        return CloudsDebugView::OrbitShellHeight;
    }
    if (value == "orbit-shell-normal" || value == "shell-normal") {
        return CloudsDebugView::OrbitShellNormal;
    }
    if (value == "orbit-shell-shadow" || value == "shell-shadow") {
        return CloudsDebugView::OrbitShellShadow;
    }
    throw std::runtime_error("unknown cloud debug view: " + std::string(value));
}

const char* clouds_debug_view_name(CloudsDebugView view) {
    switch (view) {
    case CloudsDebugView::Final:
        return "final";
    case CloudsDebugView::RawFinal:
        return "raw-final";
    case CloudsDebugView::Weather:
        return "weather";
    case CloudsDebugView::Density:
        return "density";
    case CloudsDebugView::Transmittance:
        return "transmittance";
    case CloudsDebugView::Lighting:
        return "lighting";
    case CloudsDebugView::AmbientLight:
        return "ambient-light";
    case CloudsDebugView::DirectLight:
        return "direct-light";
    case CloudsDebugView::PhaseLight:
        return "phase-light";
    case CloudsDebugView::Shadow:
        return "shadow";
    case CloudsDebugView::Steps:
        return "steps";
    case CloudsDebugView::Background:
        return "background";
    case CloudsDebugView::CloudAlpha:
        return "cloud-alpha";
    case CloudsDebugView::Distance:
        return "distance";
    case CloudsDebugView::MetadataDistance:
        return "metadata-distance";
    case CloudsDebugView::MetadataAlpha:
        return "metadata-alpha";
    case CloudsDebugView::MetadataConfidence:
        return "metadata-confidence";
    case CloudsDebugView::MetadataDensity:
        return "metadata-density";
    case CloudsDebugView::BaseDensity:
        return "base-density";
    case CloudsDebugView::DetailDensity:
        return "detail-density";
    case CloudsDebugView::CloudType:
        return "cloud-type";
    case CloudsDebugView::WeatherEdge:
        return "weather-edge";
    case CloudsDebugView::WeatherBias:
        return "weather-bias";
    case CloudsDebugView::VisibleDensity:
        return "visible-density";
    case CloudsDebugView::VisibleCloudType:
        return "visible-cloud-type";
    case CloudsDebugView::DistanceRegime:
        return "distance-regime";
    case CloudsDebugView::LocalAlpha:
        return "local-alpha";
    case CloudsDebugView::OrbitAlpha:
        return "orbit-alpha";
    case CloudsDebugView::OrbitCoverage:
        return "orbit-coverage";
    case CloudsDebugView::OrbitDetail:
        return "orbit-detail";
    case CloudsDebugView::OrbitHull:
        return "orbit-hull";
    case CloudsDebugView::OrbitEnvelope:
        return "orbit-envelope";
    case CloudsDebugView::OrbitShellAlpha:
        return "orbit-shell-alpha";
    case CloudsDebugView::OrbitShellHeight:
        return "orbit-shell-height";
    case CloudsDebugView::OrbitShellNormal:
        return "orbit-shell-normal";
    case CloudsDebugView::OrbitShellShadow:
        return "orbit-shell-shadow";
    }
    return "final";
}

CloudsDebugView next_clouds_debug_view(CloudsDebugView view) {
    const auto it = std::find(kCloudsDebugViews.begin(), kCloudsDebugViews.end(), view);
    if (it == kCloudsDebugViews.end() || std::next(it) == kCloudsDebugViews.end()) {
        return kCloudsDebugViews.front();
    }
    return *std::next(it);
}

std::uint32_t cloud_generated_volume_mip_count(std::uint32_t size) {
    if (size == 0U) {
        throw std::runtime_error("cloud generated volume size must be non-zero");
    }

    std::uint32_t mip_levels = 1U;
    while (size > 1U) {
        size /= 2U;
        ++mip_levels;
    }
    return mip_levels;
}

cubey::procedural::ProceduralArtifactMetadata clouds_generated_artifact_metadata(
    CloudsGeneratedArtifact artifact) {
    switch (artifact) {
    case CloudsGeneratedArtifact::BaseNoiseVolume:
        return cloud_volume_metadata("cloud base density volume",
                                     "cubey::projects::cloud::cloud_perlin_worley",
                                     "cloud-base-density-volume-v1",
                                     "cloud.base_density_volume", kCloudBaseNoiseSize);
    case CloudsGeneratedArtifact::DetailNoiseVolume:
        return cloud_volume_metadata("cloud detail erosion volume",
                                     "cubey::projects::cloud::cloud_worley",
                                     "cloud-detail-erosion-volume-v1",
                                     "cloud.detail_erosion_volume", kCloudDetailNoiseSize);
    case CloudsGeneratedArtifact::WeatherMap:
        return cubey::procedural::make_procedural_artifact_metadata(
            cubey::procedural::make_domain_procedural_artifact_identity(
                "cloud weather map",
                "cubey::projects::cloud::cloud_weather",
                "cloud-weather-map-v1",
                "cloud.weather_map",
                cubey::procedural::ProceduralDomainSpace::World),
            cubey::procedural::ProceduralArtifactKind::Texture2D,
            cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
            {.width = kCloudWeatherTextureSize,
             .height = kCloudWeatherTextureSize,
             .depth = 1U,
             .faces = 1U,
             .mip_levels = 1U},
            0U);
    }

    throw std::runtime_error("unknown cloud generated artifact");
}

CloudsQualityBudget clouds_quality_budget(CloudsQuality quality) {
    switch (quality) {
    case CloudsQuality::Quarter:
        return {.view_steps = 32, .light_steps = 3, .resolution_scale = 0.25F};
    case CloudsQuality::Half:
        return {.view_steps = 48, .light_steps = 4, .resolution_scale = 0.5F};
    case CloudsQuality::Full:
        return {.view_steps = 64, .light_steps = 6, .resolution_scale = 1.0F};
    }
    return {};
}

float clouds_default_camera_altitude_m(CloudsCameraMode mode) {
    switch (mode) {
    case CloudsCameraMode::Surface:
        return 800.0F;
    case CloudsCameraMode::SurfaceUp:
        return 800.0F;
    case CloudsCameraMode::High:
        return 12000.0F;
    case CloudsCameraMode::HighOblique:
        return 28000.0F;
    case CloudsCameraMode::Orbit:
        return 730000.0F;
    case CloudsCameraMode::OrbitTerminator:
        return 730000.0F;
    }
    return 1200.0F;
}

CloudsConfig clouds_config_from_run_config(const RunConfig& run_config) {
    CloudsConfig config{};

    if (!run_config.clouds.camera_mode.empty()) {
        config.camera_mode = clouds_camera_mode_from_string(run_config.clouds.camera_mode);
    }
    config.camera_altitude_m = clouds_default_camera_altitude_m(config.camera_mode);
    bool time_hours_overridden = false;
    if (!run_config.clouds.quality.empty()) {
        config.quality = clouds_quality_from_string(run_config.clouds.quality);
    }
    if (!run_config.clouds.weather_preset.empty()) {
        config.weather_preset =
            clouds_weather_preset_from_string(run_config.clouds.weather_preset);
    }
    apply_clouds_weather_preset(config, config.weather_preset);
    if (!run_config.clouds.sampling_mode.empty()) {
        config.sampling_mode = clouds_sampling_mode_from_string(run_config.clouds.sampling_mode);
    }
    if (!run_config.clouds.background_mode.empty()) {
        config.background_mode =
            clouds_background_mode_from_string(run_config.clouds.background_mode);
    }
    if (!run_config.clouds.distance_mode.empty()) {
        config.distance_mode = clouds_distance_mode_from_string(run_config.clouds.distance_mode);
    }
    if (!run_config.clouds.orbit_representation.empty()) {
        config.orbit_representation =
            clouds_orbit_representation_from_string(run_config.clouds.orbit_representation);
    }
    if (!run_config.debug_view.empty()) {
        config.debug_view = clouds_debug_view_from_string(run_config.debug_view);
    }
    if (run_config_float_is_set(run_config.clouds.planet_radius_m)) {
        config.planet_radius_m = run_config.clouds.planet_radius_m;
    }
    if (run_config_float_is_set(run_config.clouds.camera_altitude_m)) {
        config.camera_altitude_m = run_config.clouds.camera_altitude_m;
    }
    if (run_config_float_is_set(run_config.clouds.bottom_altitude_m)) {
        config.bottom_altitude_m = run_config.clouds.bottom_altitude_m;
    }
    if (run_config_float_is_set(run_config.clouds.top_altitude_m)) {
        config.top_altitude_m = run_config.clouds.top_altitude_m;
    }
    if (run_config_float_is_set(run_config.clouds.coverage)) {
        config.coverage = run_config.clouds.coverage;
    }
    if (run_config_float_is_set(run_config.clouds.density)) {
        config.density = run_config.clouds.density;
    }
    if (run_config_float_is_set(run_config.clouds.weather_scale_km)) {
        config.weather_scale_km = run_config.clouds.weather_scale_km;
    }
    if (run_config_float_is_set(run_config.clouds.vertical_shear_fraction)) {
        config.vertical_shear_fraction = run_config.clouds.vertical_shear_fraction;
    }
    if (run_config_float_is_set(run_config.clouds.wind_speed_mps)) {
        config.wind_speed_mps = run_config.clouds.wind_speed_mps;
    }
    if (run_config_float_is_set(run_config.clouds.shadow_strength)) {
        config.shadow_strength = run_config.clouds.shadow_strength;
    }
    if (run_config_float_is_set(run_config.clouds.horizon_strength)) {
        config.horizon_strength = run_config.clouds.horizon_strength;
    }
    if (run_config_float_is_set(run_config.clouds.weather_fronts)) {
        config.weather_fronts = run_config.clouds.weather_fronts;
    }
    if (run_config_float_is_set(run_config.clouds.weather_cells)) {
        config.weather_cells = run_config.clouds.weather_cells;
    }
    if (run_config_float_is_set(run_config.clouds.weather_streaks)) {
        config.weather_streaks = run_config.clouds.weather_streaks;
    }
    if (run_config_float_is_set(run_config.clouds.weather_softness)) {
        config.weather_softness = run_config.clouds.weather_softness;
    }
    if (run_config_float_is_set(run_config.clouds.weather_influence)) {
        config.weather_influence = run_config.clouds.weather_influence;
    }
    if (run_config_float_is_set(run_config.clouds.detail_erosion)) {
        config.detail_erosion = run_config.clouds.detail_erosion;
    }
    if (run_config_float_is_set(run_config.clouds.ambient_strength)) {
        config.ambient_strength = run_config.clouds.ambient_strength;
    }
    if (run_config_float_is_set(run_config.clouds.direct_strength)) {
        config.direct_strength = run_config.clouds.direct_strength;
    }
    if (run_config_float_is_set(run_config.clouds.phase_strength)) {
        config.phase_strength = run_config.clouds.phase_strength;
    }
    if (run_config_float_is_set(run_config.clouds.final_contrast)) {
        config.final_contrast = run_config.clouds.final_contrast;
    }
    if (run_config_float_is_set(run_config.clouds.final_saturation)) {
        config.final_saturation = run_config.clouds.final_saturation;
    }
    if (run_config_float_is_set(run_config.clouds.resolve_strength)) {
        config.resolve_strength = run_config.clouds.resolve_strength;
    }
    if (run_config_float_is_set(run_config.clouds.horizon_glow_strength)) {
        config.horizon_glow_strength = run_config.clouds.horizon_glow_strength;
    }
    if (run_config_float_is_set(run_config.clouds.sun_glare_strength)) {
        config.sun_glare_strength = run_config.clouds.sun_glare_strength;
    }
    if (run_config_float_is_set(run_config.clouds.jitter_strength)) {
        config.jitter_strength = run_config.clouds.jitter_strength;
    }
    if (run_config_float_is_set(run_config.clouds.orbit_transition_start_m)) {
        config.orbit_transition_start_m = run_config.clouds.orbit_transition_start_m;
    }
    if (run_config_float_is_set(run_config.clouds.orbit_transition_end_m)) {
        config.orbit_transition_end_m = run_config.clouds.orbit_transition_end_m;
    }
    if (run_config_float_is_set(run_config.clouds.far_shell_start_m)) {
        config.far_shell_start_m = run_config.clouds.far_shell_start_m;
    }
    if (run_config_float_is_set(run_config.clouds.far_shell_end_m)) {
        config.far_shell_end_m = run_config.clouds.far_shell_end_m;
    }
    if (run_config_float_is_set(run_config.clouds.far_shell_strength)) {
        config.far_shell_strength = run_config.clouds.far_shell_strength;
    }
    if (run_config_float_is_set(run_config.clouds.orbit_detail_strength)) {
        config.orbit_detail_strength = run_config.clouds.orbit_detail_strength;
    }
    if (run_config_float_is_set(run_config.clouds.orbit_density_scale)) {
        config.orbit_density_scale = run_config.clouds.orbit_density_scale;
    }
    if (run_config_float_is_set(run_config.clouds.orbit_fill)) {
        config.orbit_fill = run_config.clouds.orbit_fill;
    }
    if (run_config_float_is_set(run_config.clouds.orbit_motion_strength)) {
        config.orbit_motion_strength = run_config.clouds.orbit_motion_strength;
    }
    if (run_config_float_is_set(run_config.clouds.orbit_shell_extinction)) {
        config.orbit_shell_extinction = run_config.clouds.orbit_shell_extinction;
    }
    if (run_config.clouds.temporal >= 0) {
        config.temporal_enabled = run_config.clouds.temporal != 0;
    }
    if (run_config.clouds.local_volume >= 0) {
        config.local_volume_enabled = run_config.clouds.local_volume != 0;
    }
    if (run_config.clouds.horizon_layer >= 0) {
        config.horizon_layer_enabled = run_config.clouds.horizon_layer != 0;
    }

    if (run_config.atmosphere.time_of_day_mode == "manual") {
        config.time.solar_clock = false;
    } else if (run_config.atmosphere.time_of_day_mode == "solar") {
        config.time.solar_clock = true;
    }
    if (run_config_float_is_set(run_config.atmosphere.time_hours)) {
        config.time.time_hours = run_config.atmosphere.time_hours;
        time_hours_overridden = true;
    }
    if (run_config_float_is_set(run_config.atmosphere.day_of_year)) {
        config.time.day_of_year = run_config.atmosphere.day_of_year;
    }
    if (run_config_float_is_set(run_config.atmosphere.latitude_degrees)) {
        config.time.latitude_degrees = run_config.atmosphere.latitude_degrees;
    }
    if (run_config_float_is_set(run_config.atmosphere.sun_azimuth_offset_degrees)) {
        config.time.azimuth_offset_degrees = run_config.atmosphere.sun_azimuth_offset_degrees;
    }
    if (run_config_float_is_set(run_config.atmosphere.time_speed_hours_per_second)) {
        config.time.speed_hours_per_second = run_config.atmosphere.time_speed_hours_per_second;
    }
    if (run_config.atmosphere.time_paused >= 0) {
        config.time.playing = run_config.atmosphere.time_paused == 0;
    }
    if (run_config_float_is_set(run_config.atmosphere.sun_elevation_degrees)) {
        config.time.manual_sun_elevation_degrees = run_config.atmosphere.sun_elevation_degrees;
    }
    if (run_config_float_is_set(run_config.atmosphere.sun_azimuth_degrees)) {
        config.time.manual_sun_azimuth_degrees = run_config.atmosphere.sun_azimuth_degrees;
    }
    if (!time_hours_overridden && config.camera_mode == CloudsCameraMode::OrbitTerminator) {
        config.time.time_hours = 6.0F;
    }

    validate_clouds_config(config);
    return config;
}

void advance_clouds_time(CloudsConfig& config, double delta_seconds) {
    if (!config.time.solar_clock || !config.time.playing) {
        return;
    }
    const float delta_hours =
        static_cast<float>(delta_seconds) * config.time.speed_hours_per_second;
    const float whole_days = std::floor((config.time.time_hours + delta_hours) / 24.0F);
    config.time.time_hours =
        cubey::render::atmosphere_environment_wrap_time_hours(config.time.time_hours + delta_hours);
    if (whole_days != 0.0F) {
        config.time.day_of_year = cubey::render::atmosphere_environment_advance_day_of_year(
            config.time.day_of_year, static_cast<int>(whole_days));
    }
}

void validate_clouds_config(const CloudsConfig& config) {
    if (!finite_positive(config.planet_radius_m)) {
        throw std::runtime_error("cloud planet radius must be finite and positive");
    }
    if (!finite_nonnegative(config.camera_altitude_m)) {
        throw std::runtime_error("cloud camera altitude must be finite and nonnegative");
    }
    if (!finite_nonnegative(config.bottom_altitude_m)) {
        throw std::runtime_error("cloud bottom altitude must be finite and nonnegative");
    }
    if (!finite_positive(config.top_altitude_m) ||
        config.top_altitude_m <= config.bottom_altitude_m) {
        throw std::runtime_error("cloud top altitude must be finite and above cloud bottom");
    }
    if (!std::isfinite(config.coverage) || config.coverage < 0.0F || config.coverage > 1.0F) {
        throw std::runtime_error("cloud coverage must be finite and in [0, 1]");
    }
    if (!finite_nonnegative(config.density)) {
        throw std::runtime_error("cloud density must be finite and nonnegative");
    }
    if (!finite_positive(config.weather_scale_km)) {
        throw std::runtime_error("cloud weather scale must be finite and positive");
    }
    if (!std::isfinite(config.vertical_shear_fraction) ||
        config.vertical_shear_fraction < 0.0F || config.vertical_shear_fraction > 0.5F) {
        throw std::runtime_error("cloud vertical shear fraction must be finite and in [0, 0.5]");
    }
    if (!finite_nonnegative(config.wind_speed_mps)) {
        throw std::runtime_error("cloud wind speed must be finite and nonnegative");
    }
    if (!std::isfinite(config.shadow_strength) || config.shadow_strength < 0.0F ||
        config.shadow_strength > 2.0F) {
        throw std::runtime_error("cloud shadow strength must be finite and in [0, 2]");
    }
    if (!std::isfinite(config.horizon_strength) || config.horizon_strength < 0.0F ||
        config.horizon_strength > 2.0F) {
        throw std::runtime_error("cloud horizon strength must be finite and in [0, 2]");
    }
    if (!std::isfinite(config.weather_fronts) || config.weather_fronts < 0.0F ||
        config.weather_fronts > 1.0F) {
        throw std::runtime_error("cloud weather fronts must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.weather_cells) || config.weather_cells < 0.0F ||
        config.weather_cells > 1.0F) {
        throw std::runtime_error("cloud weather cells must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.weather_streaks) || config.weather_streaks < 0.0F ||
        config.weather_streaks > 1.0F) {
        throw std::runtime_error("cloud weather streaks must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.weather_softness) || config.weather_softness < 0.02F ||
        config.weather_softness > 0.60F) {
        throw std::runtime_error("cloud weather softness must be finite and in [0.02, 0.6]");
    }
    if (!std::isfinite(config.weather_influence) || config.weather_influence < 0.0F ||
        config.weather_influence > 1.0F) {
        throw std::runtime_error("cloud weather influence must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.detail_erosion) || config.detail_erosion < 0.0F ||
        config.detail_erosion > 1.0F) {
        throw std::runtime_error("cloud detail erosion must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.crispiness) || config.crispiness <= 0.0F) {
        throw std::runtime_error("cloud crispiness must be finite and positive");
    }
    if (!std::isfinite(config.curliness) || config.curliness <= 0.0F) {
        throw std::runtime_error("cloud curliness must be finite and positive");
    }
    if (!finite_nonnegative(config.absorption)) {
        throw std::runtime_error("cloud absorption must be finite and nonnegative");
    }
    if (!std::isfinite(config.ambient_strength) || config.ambient_strength < 0.0F ||
        config.ambient_strength > 3.0F) {
        throw std::runtime_error("cloud ambient strength must be finite and in [0, 3]");
    }
    if (!std::isfinite(config.direct_strength) || config.direct_strength < 0.0F ||
        config.direct_strength > 3.0F) {
        throw std::runtime_error("cloud direct strength must be finite and in [0, 3]");
    }
    if (!std::isfinite(config.phase_strength) || config.phase_strength < 0.0F ||
        config.phase_strength > 3.0F) {
        throw std::runtime_error("cloud phase strength must be finite and in [0, 3]");
    }
    if (!std::isfinite(config.final_contrast) || config.final_contrast < 0.0F ||
        config.final_contrast > 3.0F) {
        throw std::runtime_error("cloud final contrast must be finite and in [0, 3]");
    }
    if (!std::isfinite(config.final_saturation) || config.final_saturation < 0.0F ||
        config.final_saturation > 3.0F) {
        throw std::runtime_error("cloud final saturation must be finite and in [0, 3]");
    }
    if (!std::isfinite(config.resolve_strength) || config.resolve_strength < 0.0F ||
        config.resolve_strength > 1.0F) {
        throw std::runtime_error("cloud resolve strength must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.horizon_glow_strength) ||
        config.horizon_glow_strength < 0.0F || config.horizon_glow_strength > 3.0F) {
        throw std::runtime_error("cloud horizon glow strength must be finite and in [0, 3]");
    }
    if (!std::isfinite(config.sun_glare_strength) || config.sun_glare_strength < 0.0F ||
        config.sun_glare_strength > 3.0F) {
        throw std::runtime_error("cloud sun glare strength must be finite and in [0, 3]");
    }
    if (!std::isfinite(config.jitter_strength) || config.jitter_strength < 0.0F ||
        config.jitter_strength > 1.0F) {
        throw std::runtime_error("cloud jitter strength must be finite and in [0, 1]");
    }
    if (!finite_nonnegative(config.orbit_transition_start_m) ||
        !finite_nonnegative(config.orbit_transition_end_m) ||
        config.orbit_transition_end_m <= config.orbit_transition_start_m) {
        throw std::runtime_error(
            "cloud orbit transition range must be finite and increasing");
    }
    if (!finite_nonnegative(config.far_shell_start_m) ||
        !finite_nonnegative(config.far_shell_end_m) ||
        config.far_shell_end_m <= config.far_shell_start_m) {
        throw std::runtime_error("cloud far shell range must be finite and increasing");
    }
    if (!std::isfinite(config.far_shell_strength) ||
        config.far_shell_strength < 0.0F || config.far_shell_strength > 1.5F) {
        throw std::runtime_error("cloud far shell strength must be finite and in [0, 1.5]");
    }
    if (!std::isfinite(config.orbit_detail_strength) ||
        config.orbit_detail_strength < 0.0F || config.orbit_detail_strength > 1.0F) {
        throw std::runtime_error("cloud orbit detail strength must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.orbit_density_scale) || config.orbit_density_scale < 0.0F ||
        config.orbit_density_scale > 2.0F) {
        throw std::runtime_error("cloud orbit density scale must be finite and in [0, 2]");
    }
    if (!std::isfinite(config.orbit_fill) || config.orbit_fill < 0.0F ||
        config.orbit_fill > 2.0F) {
        throw std::runtime_error("cloud orbit fill must be finite and in [0, 2]");
    }
    if (!std::isfinite(config.orbit_motion_strength) ||
        config.orbit_motion_strength < 0.0F || config.orbit_motion_strength > 4.0F) {
        throw std::runtime_error("cloud orbit motion strength must be finite and in [0, 4]");
    }
    if (!std::isfinite(config.orbit_shell_extinction) ||
        config.orbit_shell_extinction < 0.0F || config.orbit_shell_extinction > 8.0F) {
        throw std::runtime_error("cloud orbit shell extinction must be finite and in [0, 8]");
    }
}

} // namespace cubey::projects::cloud
