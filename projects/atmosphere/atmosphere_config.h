#pragma once

#include <cubey/core/math.h>
#include <cubey/core/run_config.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/atmosphere_night_sky_atlas.h>
#include <cubey/render/cloud_layer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::atmosphere {

using cubey::render::kNightSkyLayerViews;
using cubey::render::NightSkyLayerView;

enum class AtmospherePreset : std::uint32_t {
    Noon = 0,
    LowSun = 1,
    Sunset = 2,
    Hazy = 3,
    ThinAir = 4,
    HighAltitude = 5,
    Night = 6,
    MoonlitNight = 7,
};

inline constexpr std::array<AtmospherePreset, 8> kAtmospherePresets{
    AtmospherePreset::Noon,  AtmospherePreset::LowSun,       AtmospherePreset::Sunset,
    AtmospherePreset::Hazy,  AtmospherePreset::ThinAir,      AtmospherePreset::HighAltitude,
    AtmospherePreset::Night, AtmospherePreset::MoonlitNight,
};

enum class AtmosphereRenderView : std::uint32_t {
    Final = 0,
    Rayleigh = 1,
    Mie = 2,
    Transmittance = 3,
    OpticalDepth = 4,
    SunDisk = 5,
    AerialPerspective = 6,
    NightSky = 7,
    MilkyWay = 8,
    Moon = 9,
    MoonSurface = 10,
};

inline constexpr std::array<AtmosphereRenderView, 11> kAtmosphereRenderViews{
    AtmosphereRenderView::Final,
    AtmosphereRenderView::Rayleigh,
    AtmosphereRenderView::Mie,
    AtmosphereRenderView::Transmittance,
    AtmosphereRenderView::OpticalDepth,
    AtmosphereRenderView::SunDisk,
    AtmosphereRenderView::AerialPerspective,
    AtmosphereRenderView::NightSky,
    AtmosphereRenderView::MilkyWay,
    AtmosphereRenderView::Moon,
    AtmosphereRenderView::MoonSurface,
};

enum class SunControlMode : std::uint32_t {
    ManualSun = 0,
    SolarClock = 1,
};

inline constexpr std::array<SunControlMode, 2> kSunControlModes{
    SunControlMode::ManualSun,
    SunControlMode::SolarClock,
};

enum class NightSkyVisualMode : std::uint32_t {
    HumanEye = 0,
    Camera = 1,
};

inline constexpr std::array<NightSkyVisualMode, 2> kNightSkyVisualModes{
    NightSkyVisualMode::HumanEye,
    NightSkyVisualMode::Camera,
};

enum class AtmosphereCloudWeatherPreset : std::uint32_t {
    Clear = 0,
    FairWeather = 1,
    BrokenCumulus = 2,
    OvercastStratus = 3,
    StormCells = 4,
    HighCirrus = 5,
    ReferenceParity = 6,
};

inline constexpr std::array<AtmosphereCloudWeatherPreset, 7> kAtmosphereCloudWeatherPresets{
    AtmosphereCloudWeatherPreset::Clear,
    AtmosphereCloudWeatherPreset::FairWeather,
    AtmosphereCloudWeatherPreset::BrokenCumulus,
    AtmosphereCloudWeatherPreset::OvercastStratus,
    AtmosphereCloudWeatherPreset::StormCells,
    AtmosphereCloudWeatherPreset::HighCirrus,
    AtmosphereCloudWeatherPreset::ReferenceParity,
};

struct TimeOfDayConfig {
    SunControlMode mode = SunControlMode::SolarClock;
    float time_hours = cubey::render::kAtmosphereEnvironmentDefaultTimeHours;
    float day_of_year = cubey::render::kAtmosphereEnvironmentDefaultDayOfYear;
    float latitude_degrees = cubey::render::kAtmosphereEnvironmentDefaultLatitudeDegrees;
    float azimuth_offset_degrees = cubey::render::kAtmosphereEnvironmentDefaultAzimuthOffsetDegrees;
    bool playing = true;
    float speed_hours_per_second =
        cubey::render::kAtmosphereEnvironmentDefaultTimeSpeedHoursPerSecond;
    bool auto_exposure_enabled = true;
    float exposure_bias = 0.0F;
};

struct NightSkyConfig {
    NightSkyVisualMode visual_mode = NightSkyVisualMode::HumanEye;
    NightSkyLayerView layer = NightSkyLayerView::Final;
    float twilight_strength = 1.0F;
    float twilight_horizon_warmth = 1.0F;
    float star_intensity = 1.0F;
    float star_density = 0.65F;
    float milky_way_intensity = 0.75F;
    float milky_way_contrast = 1.0F;
    float light_pollution = 0.0F;
    float procedural_variation = 0.0F;
};

struct MoonConfig {
    bool enabled = true;
    float disk_intensity = 1.0F;
    float moonlight_intensity = 1.0F;
    float phase_offset_days = 14.765F;
    float angular_radius_scale = 1.0F;
};

[[nodiscard]] inline cubey::render::CloudLayerConfig default_atmosphere_cloud_layer_config() {
    cubey::render::CloudLayerConfig config{};
    config.quality = cubey::render::CloudLayerQuality::Half;
    config.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    config.distance_mode = cubey::render::CloudLayerDistanceMode::Auto;
    config.density_model = cubey::render::CloudLayerDensityModel::Procedural;
    config.resolve_mode = cubey::render::CloudLayerResolveMode::TerrainPost;
    config.debug_view = cubey::render::CloudLayerDebugView::Final;
    config.sampling_mode = cubey::render::CloudLayerSamplingMode::Bayer;
    config.temporal_enabled = false;
    config.bottom_altitude_m = 5000.0F;
    config.top_altitude_m = 22000.0F;
    config.coverage = 0.45F;
    config.density = 0.020F;
    config.weather_scale_km = 120.0F;
    config.shape_domain_km = 600.0F;
    config.footprint_filter_strength = 1.0F;
    config.edge_softness = 1.0F;
    config.edge_detail_fade = 0.75F;
    config.edge_resolve_strength = 0.70F;
    config.shadow_strength = 0.82F;
    config.horizon_strength = 0.48F;
    config.weather_softness = 0.22F;
    config.detail_erosion = 1.0F;
    config.crispiness = 40.0F;
    config.curliness = 0.10F;
    config.absorption = 0.28F;
    config.ambient_strength = 0.82F;
    config.direct_strength = 1.28F;
    config.phase_strength = 1.14F;
    config.resolve_strength = 0.48F;
    config.final_contrast = 1.17F;
    config.final_saturation = 1.12F;
    config.horizon_glow_strength = 0.55F;
    config.sun_glare_strength = 1.0F;
    config.jitter_strength = 0.65F;
    return config;
}

struct AtmosphereCloudConfig {
    bool enabled = true;
    AtmosphereCloudWeatherPreset weather_preset = AtmosphereCloudWeatherPreset::BrokenCumulus;
    float wind_speed_mps = 450.0F;
    cubey::render::CloudLayerConfig layer = default_atmosphere_cloud_layer_config();
};

struct AtmosphereConfig {
    AtmospherePreset preset = AtmospherePreset::Noon;
    AtmosphereRenderView render_view = AtmosphereRenderView::Final;
    TimeOfDayConfig time_of_day{};
    NightSkyConfig night_sky{};
    MoonConfig moon{};
    AtmosphereCloudConfig clouds{};

    float bottom_radius_km = 6371.0F;
    float top_radius_km = 6471.0F;
    cubey::math::Vec3 rayleigh_scattering{0.005802F, 0.013558F, 0.033100F};
    float rayleigh_scale_height_km = 8.0F;
    float rayleigh_density_scale = 1.0F;

    float mie_scattering = 0.003996F;
    float mie_extinction = 0.004400F;
    float mie_scale_height_km = 1.2F;
    float mie_anisotropy = 0.80F;
    float mie_density_scale = 1.0F;

    cubey::math::Vec3 ozone_absorption{0.000650F, 0.001881F, 0.000085F};
    float ozone_center_altitude_km = 25.0F;
    float ozone_half_width_km = 15.0F;

    float ground_albedo = 0.10F;
    float sun_angular_radius = 0.004675F;
    float sun_elevation_degrees = 60.0F;
    float sun_azimuth_degrees = 0.0F;
    float camera_altitude_km = 0.15F;
    float camera_yaw_offset_degrees = 0.0F;
    float camera_pitch_offset_degrees = 0.0F;
    float exposure = 0.0F;
    bool render_celestial_content = true;
    bool render_sun_disk = true;
    bool render_night_sky = true;
    bool render_moon_disk = true;
    bool reference_geometry_enabled = true;
    float reference_grid_km = 1.0F;
    float reference_intensity = 0.72F;
};

inline constexpr std::array<cubey::render::CloudLayerQuality, 3> kAtmosphereCloudQualities{
    cubey::render::CloudLayerQuality::Quarter,
    cubey::render::CloudLayerQuality::Half,
    cubey::render::CloudLayerQuality::Full,
};

inline constexpr std::array<cubey::render::CloudLayerSamplingMode, 4>
    kAtmosphereCloudSamplingModes{
        cubey::render::CloudLayerSamplingMode::Interleaved,
        cubey::render::CloudLayerSamplingMode::Bayer,
        cubey::render::CloudLayerSamplingMode::BlueNoise,
        cubey::render::CloudLayerSamplingMode::Off,
    };

inline constexpr std::array<cubey::render::CloudLayerViewSampleMode, 2>
    kAtmosphereCloudViewSampleModes{
        cubey::render::CloudLayerViewSampleMode::SingleFrame,
        cubey::render::CloudLayerViewSampleMode::TemporalPhased,
    };

inline constexpr std::array<cubey::render::CloudLayerDistanceMode, 4>
    kAtmosphereCloudDistanceModes{
        cubey::render::CloudLayerDistanceMode::Auto,
        cubey::render::CloudLayerDistanceMode::Local,
        cubey::render::CloudLayerDistanceMode::OrbitShell,
        cubey::render::CloudLayerDistanceMode::BlendDebug,
    };

inline constexpr std::array<cubey::render::CloudLayerOrbitRepresentation, 2>
    kAtmosphereCloudOrbitRepresentations{
        cubey::render::CloudLayerOrbitRepresentation::VolumeRaymarch,
        cubey::render::CloudLayerOrbitRepresentation::SurfaceShell,
    };

inline constexpr std::array<cubey::render::CloudLayerDensityModel, 3>
    kAtmosphereCloudDensityModels{
        cubey::render::CloudLayerDensityModel::RefDensity,
        cubey::render::CloudLayerDensityModel::Procedural,
        cubey::render::CloudLayerDensityModel::CloudRefCompatible,
    };

inline constexpr std::array<cubey::render::CloudLayerResolveMode, 2>
    kAtmosphereCloudResolveModes{
        cubey::render::CloudLayerResolveMode::TerrainPost,
        cubey::render::CloudLayerResolveMode::MetadataBilateral,
    };

[[nodiscard]] inline const char*
atmosphere_cloud_weather_preset_name(AtmosphereCloudWeatherPreset preset) {
    switch (preset) {
    case AtmosphereCloudWeatherPreset::Clear:
        return "clear";
    case AtmosphereCloudWeatherPreset::FairWeather:
        return "fair-weather";
    case AtmosphereCloudWeatherPreset::BrokenCumulus:
        return "broken-cumulus";
    case AtmosphereCloudWeatherPreset::OvercastStratus:
        return "overcast-stratus";
    case AtmosphereCloudWeatherPreset::StormCells:
        return "storm-cells";
    case AtmosphereCloudWeatherPreset::HighCirrus:
        return "high-cirrus";
    case AtmosphereCloudWeatherPreset::ReferenceParity:
        return "reference-parity";
    }
    return "broken-cumulus";
}

[[nodiscard]] inline AtmosphereCloudWeatherPreset
atmosphere_cloud_weather_preset_from_name(std::string_view name) {
    if (name.empty() || name == "broken-cumulus" || name == "scattered" ||
        name == "inspection") {
        return AtmosphereCloudWeatherPreset::BrokenCumulus;
    }
    if (name == "clear") {
        return AtmosphereCloudWeatherPreset::Clear;
    }
    if (name == "fair-weather") {
        return AtmosphereCloudWeatherPreset::FairWeather;
    }
    if (name == "overcast-stratus" || name == "overcast") {
        return AtmosphereCloudWeatherPreset::OvercastStratus;
    }
    if (name == "storm-cells" || name == "storm") {
        return AtmosphereCloudWeatherPreset::StormCells;
    }
    if (name == "high-cirrus") {
        return AtmosphereCloudWeatherPreset::HighCirrus;
    }
    if (name == "reference-parity" || name == "ref-parity" ||
        name == "cloud-ref-parity") {
        return AtmosphereCloudWeatherPreset::ReferenceParity;
    }
    throw std::runtime_error("unknown atmosphere cloud weather preset: " + std::string(name));
}

[[nodiscard]] inline const char*
atmosphere_cloud_quality_name(cubey::render::CloudLayerQuality quality) {
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
atmosphere_cloud_quality_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerQuality::Half;
    }
    for (const cubey::render::CloudLayerQuality quality : kAtmosphereCloudQualities) {
        if (name == atmosphere_cloud_quality_name(quality)) {
            return quality;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud quality: " + std::string(name));
}

[[nodiscard]] inline const char*
atmosphere_cloud_sampling_mode_name(cubey::render::CloudLayerSamplingMode mode) {
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
atmosphere_cloud_sampling_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerSamplingMode::Bayer;
    }
    for (const cubey::render::CloudLayerSamplingMode mode : kAtmosphereCloudSamplingModes) {
        if (name == atmosphere_cloud_sampling_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud sampling mode: " + std::string(name));
}

[[nodiscard]] inline const char*
atmosphere_cloud_view_sample_mode_name(cubey::render::CloudLayerViewSampleMode mode) {
    switch (mode) {
    case cubey::render::CloudLayerViewSampleMode::SingleFrame:
        return "single-frame";
    case cubey::render::CloudLayerViewSampleMode::TemporalPhased:
        return "temporal-phased";
    }
    return "single-frame";
}

[[nodiscard]] inline cubey::render::CloudLayerViewSampleMode
atmosphere_cloud_view_sample_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerViewSampleMode::SingleFrame;
    }
    for (const cubey::render::CloudLayerViewSampleMode mode :
         kAtmosphereCloudViewSampleModes) {
        if (name == atmosphere_cloud_view_sample_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud view sample mode: " +
                             std::string(name));
}

[[nodiscard]] inline const char*
atmosphere_cloud_distance_mode_name(cubey::render::CloudLayerDistanceMode mode) {
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
atmosphere_cloud_distance_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return cubey::render::CloudLayerDistanceMode::Local;
    }
    for (const cubey::render::CloudLayerDistanceMode mode : kAtmosphereCloudDistanceModes) {
        if (name == atmosphere_cloud_distance_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere cloud distance mode: " + std::string(name));
}

[[nodiscard]] inline const char*
atmosphere_cloud_orbit_representation_name(cubey::render::CloudLayerOrbitRepresentation mode) {
    switch (mode) {
    case cubey::render::CloudLayerOrbitRepresentation::VolumeRaymarch:
        return "volume";
    case cubey::render::CloudLayerOrbitRepresentation::SurfaceShell:
        return "surface-shell";
    }
    return "surface-shell";
}

[[nodiscard]] inline cubey::render::CloudLayerOrbitRepresentation
atmosphere_cloud_orbit_representation_from_name(std::string_view name) {
    if (name.empty() || name == "surface-shell" || name == "shell") {
        return cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
    }
    if (name == "volume" || name == "raymarch" || name == "volume-raymarch") {
        return cubey::render::CloudLayerOrbitRepresentation::VolumeRaymarch;
    }
    throw std::runtime_error("unknown atmosphere cloud orbit representation: " +
                             std::string(name));
}

[[nodiscard]] inline const char*
atmosphere_cloud_density_model_name(cubey::render::CloudLayerDensityModel model) {
    switch (model) {
    case cubey::render::CloudLayerDensityModel::RefDensity:
        return "ref-density";
    case cubey::render::CloudLayerDensityModel::Procedural:
        return "procedural";
    case cubey::render::CloudLayerDensityModel::CloudRefCompatible:
        return "cloud-ref-compatible";
    }
    return "procedural";
}

[[nodiscard]] inline cubey::render::CloudLayerDensityModel
atmosphere_cloud_density_model_from_name(std::string_view name) {
    if (name.empty() || name == "procedural" || name == "active") {
        return cubey::render::CloudLayerDensityModel::Procedural;
    }
    if (name == "ref-density" || name == "terrain-ref" || name == "terrain" ||
        name == "ref") {
        return cubey::render::CloudLayerDensityModel::RefDensity;
    }
    if (name == "cloud-ref-compatible" || name == "cloud-ref" ||
        name == "compatible") {
        return cubey::render::CloudLayerDensityModel::CloudRefCompatible;
    }
    throw std::runtime_error("unknown atmosphere cloud density model: " + std::string(name));
}

[[nodiscard]] inline const char*
atmosphere_cloud_resolve_mode_name(cubey::render::CloudLayerResolveMode mode) {
    switch (mode) {
    case cubey::render::CloudLayerResolveMode::TerrainPost:
        return "terrain-post";
    case cubey::render::CloudLayerResolveMode::MetadataBilateral:
        return "metadata-bilateral";
    }
    return "terrain-post";
}

[[nodiscard]] inline cubey::render::CloudLayerResolveMode
atmosphere_cloud_resolve_mode_from_name(std::string_view name) {
    if (name.empty() || name == "terrain-post" || name == "terrain" || name == "gaussian") {
        return cubey::render::CloudLayerResolveMode::TerrainPost;
    }
    if (name == "metadata-bilateral" || name == "bilateral" || name == "metadata") {
        return cubey::render::CloudLayerResolveMode::MetadataBilateral;
    }
    throw std::runtime_error("unknown atmosphere cloud resolve mode: " + std::string(name));
}

struct AtmosphereCloudWeatherPresetSettings {
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

[[nodiscard]] inline AtmosphereCloudWeatherPresetSettings
atmosphere_cloud_weather_preset_settings(AtmosphereCloudWeatherPreset preset) {
    switch (preset) {
    case AtmosphereCloudWeatherPreset::Clear:
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
    case AtmosphereCloudWeatherPreset::FairWeather:
        return {.coverage = 0.30F,
                .density = 0.016F,
                .weather_scale_km = 260.0F,
                .shape_domain_km = 1300.0F,
                .wind_speed_mps = 260.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 17000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::FairWeather};
    case AtmosphereCloudWeatherPreset::BrokenCumulus:
        return {.coverage = 0.45F,
                .density = 0.020F,
                .weather_scale_km = 120.0F,
                .shape_domain_km = 600.0F,
                .wind_speed_mps = 450.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 22000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::BrokenCumulus};
    case AtmosphereCloudWeatherPreset::OvercastStratus:
        return {.coverage = 0.72F,
                .density = 0.018F,
                .weather_scale_km = 280.0F,
                .shape_domain_km = 1400.0F,
                .wind_speed_mps = 320.0F,
                .bottom_altitude_m = 3000.0F,
                .top_altitude_m = 12000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::OvercastStratus};
    case AtmosphereCloudWeatherPreset::StormCells:
        return {.coverage = 0.64F,
                .density = 0.032F,
                .weather_scale_km = 105.0F,
                .shape_domain_km = 525.0F,
                .wind_speed_mps = 650.0F,
                .bottom_altitude_m = 2500.0F,
                .top_altitude_m = 24000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::StormCells};
    case AtmosphereCloudWeatherPreset::HighCirrus:
        return {.coverage = 0.36F,
                .density = 0.010F,
                .weather_scale_km = 360.0F,
                .shape_domain_km = 1800.0F,
                .wind_speed_mps = 700.0F,
                .bottom_altitude_m = 11000.0F,
                .top_altitude_m = 22000.0F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::HighCirrus};
    case AtmosphereCloudWeatherPreset::ReferenceParity:
        return {.coverage = 0.30F,
                .density = 0.016F,
                .weather_scale_km = 260.0F,
                .shape_domain_km = 600.0F,
                .wind_speed_mps = 260.0F,
                .bottom_altitude_m = 5000.0F,
                .top_altitude_m = 14000.0F,
                .horizon_strength = 0.62F,
                .horizon_glow_strength = 1.05F,
                .cloud_style = cubey::render::CloudLayerCloudStyle::FairWeather};
    }
    return atmosphere_cloud_weather_preset_settings(AtmosphereCloudWeatherPreset::BrokenCumulus);
}

inline void apply_atmosphere_cloud_reference_parity(AtmosphereCloudConfig& config) {
    config.layer.quality = cubey::render::CloudLayerQuality::Full;
    config.layer.sampling_mode = cubey::render::CloudLayerSamplingMode::Bayer;
    config.layer.view_sample_mode = cubey::render::CloudLayerViewSampleMode::SingleFrame;
    config.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Local;
    config.layer.density_model = cubey::render::CloudLayerDensityModel::CloudRefCompatible;
    config.layer.resolve_mode = cubey::render::CloudLayerResolveMode::TerrainPost;
    config.layer.temporal_enabled = false;
    config.layer.local_volume_enabled = true;
    config.layer.horizon_layer_enabled = false;
    config.layer.view_steps_override = 64;
    config.layer.view_samples = 1;
    config.layer.footprint_filter_strength = 1.0F;
    config.layer.edge_softness = 1.0F;
    config.layer.edge_detail_fade = 0.75F;
    config.layer.edge_resolve_strength = 0.70F;
    config.layer.shadow_strength = 0.15F;
    config.layer.ambient_strength = 1.30F;
    config.layer.direct_strength = 1.15F;
    config.layer.phase_strength = 1.20F;
    config.layer.final_contrast = 0.98F;
    config.layer.final_saturation = 1.0F;
    config.layer.resolve_strength = 1.0F;
    config.layer.sun_glare_strength = 1.0F;
    config.layer.jitter_strength = 0.65F;
}

inline void apply_atmosphere_cloud_weather_preset(AtmosphereCloudConfig& config,
                                                  AtmosphereCloudWeatherPreset preset) {
    config.weather_preset = preset;
    const AtmosphereCloudWeatherPresetSettings settings =
        atmosphere_cloud_weather_preset_settings(preset);
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
    if (preset == AtmosphereCloudWeatherPreset::ReferenceParity) {
        apply_atmosphere_cloud_reference_parity(config);
    }
}

using SolarPosition = cubey::render::AtmosphereEnvironmentSolarPosition;
using LunarState = cubey::render::AtmosphereEnvironmentLunarState;

[[nodiscard]] inline const char* atmosphere_preset_name(AtmospherePreset preset) {
    switch (preset) {
    case AtmospherePreset::Noon:
        return "noon";
    case AtmospherePreset::LowSun:
        return "low-sun";
    case AtmospherePreset::Sunset:
        return "sunset";
    case AtmospherePreset::Hazy:
        return "hazy";
    case AtmospherePreset::ThinAir:
        return "thin-air";
    case AtmospherePreset::HighAltitude:
        return "high-altitude";
    case AtmospherePreset::Night:
        return "night";
    case AtmospherePreset::MoonlitNight:
        return "moonlit-night";
    }
    return "noon";
}

[[nodiscard]] inline AtmospherePreset atmosphere_preset_from_name(std::string_view name) {
    if (name.empty()) {
        return AtmospherePreset::Noon;
    }
    for (const AtmospherePreset preset : kAtmospherePresets) {
        if (name == atmosphere_preset_name(preset)) {
            return preset;
        }
    }
    throw std::runtime_error("unknown atmosphere preset: " + std::string(name));
}

[[nodiscard]] inline const char* atmosphere_render_view_name(AtmosphereRenderView view) {
    switch (view) {
    case AtmosphereRenderView::Final:
        return "final";
    case AtmosphereRenderView::Rayleigh:
        return "rayleigh";
    case AtmosphereRenderView::Mie:
        return "mie";
    case AtmosphereRenderView::Transmittance:
        return "transmittance";
    case AtmosphereRenderView::OpticalDepth:
        return "optical-depth";
    case AtmosphereRenderView::SunDisk:
        return "sun-disk";
    case AtmosphereRenderView::AerialPerspective:
        return "aerial-perspective";
    case AtmosphereRenderView::NightSky:
        return "night-sky";
    case AtmosphereRenderView::MilkyWay:
        return "milky-way";
    case AtmosphereRenderView::Moon:
        return "moon";
    case AtmosphereRenderView::MoonSurface:
        return "moon-surface";
    }
    return "final";
}

[[nodiscard]] inline AtmosphereRenderView atmosphere_render_view_from_name(std::string_view name) {
    if (name.empty()) {
        return AtmosphereRenderView::Final;
    }
    for (const AtmosphereRenderView view : kAtmosphereRenderViews) {
        if (name == atmosphere_render_view_name(view)) {
            return view;
        }
    }
    throw std::runtime_error("unknown atmosphere render view: " + std::string(name));
}

[[nodiscard]] inline const char* night_sky_visual_mode_name(NightSkyVisualMode mode) {
    switch (mode) {
    case NightSkyVisualMode::HumanEye:
        return "human";
    case NightSkyVisualMode::Camera:
        return "camera";
    }
    return "human";
}

[[nodiscard]] inline NightSkyVisualMode night_sky_visual_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return NightSkyVisualMode::HumanEye;
    }
    for (const NightSkyVisualMode mode : kNightSkyVisualModes) {
        if (name == night_sky_visual_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown night sky visual mode: " + std::string(name));
}

[[nodiscard]] inline const char* night_sky_layer_view_name(NightSkyLayerView layer) {
    switch (layer) {
    case NightSkyLayerView::Final:
        return "final";
    case NightSkyLayerView::StellarEmission:
        return "stellar-emission";
    case NightSkyLayerView::DustTau:
        return "dust-tau";
    case NightSkyLayerView::StarClouds:
        return "star-clouds";
    case NightSkyLayerView::HiiEmission:
        return "hii-emission";
    case NightSkyLayerView::Speckles:
        return "speckles";
    }
    return "final";
}

[[nodiscard]] inline NightSkyLayerView night_sky_layer_view_from_name(std::string_view name) {
    if (name.empty()) {
        return NightSkyLayerView::Final;
    }
    for (const NightSkyLayerView layer : kNightSkyLayerViews) {
        if (name == night_sky_layer_view_name(layer)) {
            return layer;
        }
    }
    throw std::runtime_error("unknown Milky Way layer: " + std::string(name));
}

[[nodiscard]] inline AtmosphereRenderView next_atmosphere_render_view(AtmosphereRenderView view) {
    for (std::size_t index = 0; index < kAtmosphereRenderViews.size(); ++index) {
        if (kAtmosphereRenderViews[index] == view) {
            return kAtmosphereRenderViews[(index + 1U) % kAtmosphereRenderViews.size()];
        }
    }
    return AtmosphereRenderView::Final;
}

[[nodiscard]] inline const char* sun_control_mode_name(SunControlMode mode) {
    switch (mode) {
    case SunControlMode::ManualSun:
        return "manual";
    case SunControlMode::SolarClock:
        return "solar";
    }
    return "solar";
}

[[nodiscard]] inline SunControlMode sun_control_mode_from_name(std::string_view name) {
    if (name.empty()) {
        return SunControlMode::SolarClock;
    }
    for (const SunControlMode mode : kSunControlModes) {
        if (name == sun_control_mode_name(mode)) {
            return mode;
        }
    }
    throw std::runtime_error("unknown atmosphere time-of-day mode: " + std::string(name));
}

[[nodiscard]] inline float atmosphere_degrees_to_radians(float degrees) {
    return cubey::render::atmosphere_environment_degrees_to_radians(degrees);
}

[[nodiscard]] inline float atmosphere_radians_to_degrees(float radians) {
    return cubey::render::atmosphere_environment_radians_to_degrees(radians);
}

[[nodiscard]] inline float atmosphere_wrap_time_hours(float time_hours) {
    return cubey::render::atmosphere_environment_wrap_time_hours(time_hours);
}

[[nodiscard]] inline float atmosphere_wrap_signed_degrees(float degrees) {
    return cubey::render::atmosphere_environment_wrap_signed_degrees(degrees);
}

[[nodiscard]] inline float atmosphere_advance_day_of_year(float day_of_year, int day_delta) {
    return cubey::render::atmosphere_environment_advance_day_of_year(day_of_year, day_delta);
}

[[nodiscard]] inline cubey::render::AtmosphereEnvironmentTimeOfDay
atmosphere_environment_time_of_day(const TimeOfDayConfig& time_of_day) {
    return {
        .time_hours = time_of_day.time_hours,
        .day_of_year = time_of_day.day_of_year,
        .latitude_degrees = time_of_day.latitude_degrees,
        .azimuth_offset_degrees = time_of_day.azimuth_offset_degrees,
    };
}

[[nodiscard]] inline cubey::render::AtmosphereEnvironmentMoon
atmosphere_environment_moon(const MoonConfig& moon) {
    return {
        .enabled = moon.enabled,
        .disk_intensity = moon.disk_intensity,
        .moonlight_intensity = moon.moonlight_intensity,
        .phase_offset_days = moon.phase_offset_days,
        .angular_radius_scale = moon.angular_radius_scale,
    };
}

[[nodiscard]] inline float atmosphere_wrap_unit(float value) {
    return cubey::render::atmosphere_environment_wrap_unit(value);
}

[[nodiscard]] inline SolarPosition atmosphere_solar_position(const TimeOfDayConfig& time_of_day) {
    return cubey::render::atmosphere_environment_solar_position(
        atmosphere_environment_time_of_day(time_of_day));
}

[[nodiscard]] inline cubey::math::Vec3 atmosphere_direction_from_alt_az(float elevation_degrees,
                                                                        float azimuth_degrees) {
    return cubey::render::atmosphere_environment_direction_from_alt_az(elevation_degrees,
                                                                       azimuth_degrees);
}

[[nodiscard]] inline LunarState atmosphere_lunar_state(const TimeOfDayConfig& time_of_day,
                                                       const MoonConfig& moon) {
    return cubey::render::atmosphere_environment_lunar_state(
        atmosphere_environment_time_of_day(time_of_day), atmosphere_environment_moon(moon));
}

[[nodiscard]] inline float atmosphere_sidereal_angle_radians(const TimeOfDayConfig& time_of_day) {
    return cubey::render::atmosphere_environment_sidereal_angle_radians(
        atmosphere_environment_time_of_day(time_of_day));
}

[[nodiscard]] inline float atmosphere_auto_exposure(float sun_elevation_degrees,
                                                    float exposure_bias) {
    return cubey::render::atmosphere_environment_auto_exposure(sun_elevation_degrees,
                                                               exposure_bias);
}

inline void resolve_atmosphere_time_of_day(AtmosphereConfig& config) {
    if (config.time_of_day.mode == SunControlMode::SolarClock) {
        const SolarPosition solar_position = atmosphere_solar_position(config.time_of_day);
        config.sun_elevation_degrees = solar_position.elevation_degrees;
        config.sun_azimuth_degrees = solar_position.azimuth_degrees;
    }
    if (config.time_of_day.auto_exposure_enabled) {
        config.exposure = atmosphere_auto_exposure(config.sun_elevation_degrees,
                                                   config.time_of_day.exposure_bias);
    }
}

inline void set_atmosphere_time_from_elapsed(TimeOfDayConfig& time_of_day, float base_time_hours,
                                             float base_day_of_year, double elapsed_seconds) {
    const float elapsed_hours =
        std::max(static_cast<float>(elapsed_seconds), 0.0F) * time_of_day.speed_hours_per_second;
    const float total_hours = base_time_hours + elapsed_hours;
    const int day_delta = static_cast<int>(std::floor(total_hours / 24.0F));
    time_of_day.time_hours = atmosphere_wrap_time_hours(total_hours);
    time_of_day.day_of_year = atmosphere_advance_day_of_year(base_day_of_year, day_delta);
}

inline void advance_atmosphere_time_of_day(AtmosphereConfig& config, double delta_seconds) {
    if (config.time_of_day.mode != SunControlMode::SolarClock || !config.time_of_day.playing ||
        delta_seconds <= 0.0) {
        return;
    }
    set_atmosphere_time_from_elapsed(config.time_of_day, config.time_of_day.time_hours,
                                     config.time_of_day.day_of_year, delta_seconds);
}

[[nodiscard]] inline AtmosphereConfig atmosphere_config_for_preset(AtmospherePreset preset) {
    AtmosphereConfig config;
    config.preset = preset;
    switch (preset) {
    case AtmospherePreset::Noon:
        config.time_of_day.time_hours = 12.0F;
        config.sun_elevation_degrees = 60.0F;
        config.sun_azimuth_degrees = 0.0F;
        config.camera_altitude_km = 0.15F;
        config.exposure = 0.0F;
        break;
    case AtmospherePreset::LowSun:
        config.time_of_day.time_hours = 17.1F;
        config.sun_elevation_degrees = 12.0F;
        config.sun_azimuth_degrees = -32.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 1.15F;
        break;
    case AtmospherePreset::Sunset:
        config.time_of_day.time_hours = 17.8F;
        config.sun_elevation_degrees = 2.0F;
        config.sun_azimuth_degrees = -48.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 1.45F;
        break;
    case AtmospherePreset::Hazy:
        config.time_of_day.time_hours = 16.6F;
        config.sun_elevation_degrees = 18.0F;
        config.sun_azimuth_degrees = -24.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 3.50F;
        break;
    case AtmospherePreset::ThinAir:
        config.time_of_day.time_hours = 13.9F;
        config.sun_elevation_degrees = 50.0F;
        config.sun_azimuth_degrees = 15.0F;
        config.camera_altitude_km = 0.15F;
        config.rayleigh_density_scale = 0.45F;
        config.mie_density_scale = 0.25F;
        break;
    case AtmospherePreset::HighAltitude:
        config.time_of_day.time_hours = 17.3F;
        config.sun_elevation_degrees = 10.0F;
        config.sun_azimuth_degrees = -58.0F;
        config.camera_altitude_km = 25.0F;
        config.mie_density_scale = 0.35F;
        break;
    case AtmospherePreset::Night:
        config.time_of_day.time_hours = 0.0F;
        config.sun_elevation_degrees = -60.0F;
        config.sun_azimuth_degrees = 180.0F;
        config.camera_altitude_km = 0.15F;
        config.exposure = 2.8F;
        config.night_sky.star_intensity = 1.25F;
        config.night_sky.star_density = 0.72F;
        config.night_sky.milky_way_intensity = 0.90F;
        config.night_sky.milky_way_contrast = 1.10F;
        break;
    case AtmospherePreset::MoonlitNight:
        config.time_of_day.time_hours = 1.75F;
        config.time_of_day.day_of_year = 23.0F;
        config.time_of_day.latitude_degrees = 78.0F;
        config.time_of_day.exposure_bias = -0.40F;
        config.camera_altitude_km = 0.15F;
        config.night_sky.star_intensity = 0.90F;
        config.night_sky.star_density = 0.58F;
        config.night_sky.milky_way_intensity = 0.55F;
        config.night_sky.milky_way_contrast = 0.90F;
        config.moon.disk_intensity = 1.25F;
        config.moon.moonlight_intensity = 1.35F;
        config.moon.angular_radius_scale = 3.0F;
        break;
    }
    resolve_atmosphere_time_of_day(config);
    return config;
}

[[nodiscard]] inline bool atmosphere_vec3_nonnegative(cubey::math::Vec3 value) {
    return value.x >= 0.0F && value.y >= 0.0F && value.z >= 0.0F;
}

inline void validate_atmosphere_config(const AtmosphereConfig& config) {
    const auto require_finite = [](float value, const char* name) {
        if (!std::isfinite(value)) {
            throw std::runtime_error(std::string(name) + " must be finite");
        }
    };
    require_finite(config.bottom_radius_km, "atmosphere bottom radius");
    require_finite(config.top_radius_km, "atmosphere top radius");
    require_finite(config.rayleigh_scale_height_km, "atmosphere Rayleigh scale height");
    require_finite(config.mie_scale_height_km, "atmosphere Mie scale height");
    require_finite(config.mie_anisotropy, "atmosphere Mie anisotropy");
    require_finite(config.ozone_half_width_km, "atmosphere ozone half width");
    require_finite(config.sun_angular_radius, "atmosphere sun angular radius");
    require_finite(config.sun_elevation_degrees, "atmosphere sun elevation");
    require_finite(config.sun_azimuth_degrees, "atmosphere sun azimuth");
    require_finite(config.camera_altitude_km, "atmosphere camera altitude");
    require_finite(config.camera_yaw_offset_degrees, "atmosphere camera yaw offset");
    require_finite(config.camera_pitch_offset_degrees, "atmosphere camera pitch offset");
    require_finite(config.exposure, "atmosphere exposure");
    require_finite(config.reference_grid_km, "atmosphere reference grid scale");
    require_finite(config.reference_intensity, "atmosphere reference intensity");
    require_finite(config.night_sky.twilight_strength, "atmosphere twilight strength");
    require_finite(config.night_sky.twilight_horizon_warmth, "atmosphere twilight horizon warmth");
    require_finite(config.night_sky.star_intensity, "atmosphere star intensity");
    require_finite(config.night_sky.star_density, "atmosphere star density");
    require_finite(config.night_sky.milky_way_intensity, "atmosphere Milky Way intensity");
    require_finite(config.night_sky.milky_way_contrast, "atmosphere Milky Way contrast");
    require_finite(config.night_sky.light_pollution, "atmosphere light pollution");
    require_finite(config.night_sky.procedural_variation, "atmosphere Milky Way variation");
    require_finite(config.moon.disk_intensity, "atmosphere moon intensity");
    require_finite(config.moon.moonlight_intensity, "atmosphere moonlight intensity");
    require_finite(config.moon.phase_offset_days, "atmosphere moon phase offset");
    require_finite(config.moon.angular_radius_scale, "atmosphere moon size scale");
    require_finite(config.time_of_day.time_hours, "atmosphere time hours");
    require_finite(config.time_of_day.day_of_year, "atmosphere day of year");
    require_finite(config.time_of_day.latitude_degrees, "atmosphere latitude");
    require_finite(config.time_of_day.azimuth_offset_degrees, "atmosphere sun azimuth offset");
    require_finite(config.time_of_day.speed_hours_per_second, "atmosphere time speed");
    require_finite(config.time_of_day.exposure_bias, "atmosphere exposure bias");

    if (config.bottom_radius_km <= 0.0F || config.top_radius_km <= config.bottom_radius_km) {
        throw std::runtime_error("atmosphere radii must be positive and ordered bottom < top");
    }
    if (!atmosphere_vec3_nonnegative(config.rayleigh_scattering) ||
        !atmosphere_vec3_nonnegative(config.ozone_absorption)) {
        throw std::runtime_error("atmosphere coefficients must be nonnegative");
    }
    if (config.rayleigh_scale_height_km <= 0.0F || config.mie_scale_height_km <= 0.0F ||
        config.ozone_half_width_km <= 0.0F) {
        throw std::runtime_error("atmosphere scale heights must be positive");
    }
    if (config.rayleigh_density_scale < 0.0F || config.mie_density_scale < 0.0F ||
        config.mie_scattering < 0.0F || config.mie_extinction < 0.0F) {
        throw std::runtime_error("atmosphere density and Mie coefficients must be nonnegative");
    }
    if (config.mie_anisotropy < 0.0F || config.mie_anisotropy >= 1.0F) {
        throw std::runtime_error("atmosphere Mie anisotropy must be in [0, 1)");
    }
    if (config.ground_albedo < 0.0F || config.ground_albedo > 1.0F) {
        throw std::runtime_error("atmosphere ground albedo must be in [0, 1]");
    }
    if (config.sun_elevation_degrees < -90.0F || config.sun_elevation_degrees > 90.0F ||
        config.sun_azimuth_degrees < -360.0F || config.sun_azimuth_degrees > 360.0F) {
        throw std::runtime_error("atmosphere sun angles must be within supported ranges");
    }
    if (config.sun_angular_radius <= 0.0F || config.camera_altitude_km < 0.0F) {
        throw std::runtime_error("atmosphere sun radius must be positive and altitude nonnegative");
    }
    if (config.camera_yaw_offset_degrees < -360.0F ||
        config.camera_yaw_offset_degrees > 360.0F ||
        config.camera_pitch_offset_degrees < -89.0F ||
        config.camera_pitch_offset_degrees > 89.0F) {
        throw std::runtime_error("atmosphere camera view offsets are out of range");
    }
    if (config.reference_grid_km <= 0.0F || config.reference_intensity < 0.0F) {
        throw std::runtime_error("atmosphere reference grid scale must be positive");
    }
    if (config.night_sky.twilight_strength < 0.0F || config.night_sky.twilight_strength > 4.0F ||
        config.night_sky.twilight_horizon_warmth < 0.0F ||
        config.night_sky.twilight_horizon_warmth > 2.0F || config.night_sky.star_intensity < 0.0F ||
        config.night_sky.star_intensity > 4.0F || config.night_sky.star_density < 0.0F ||
        config.night_sky.star_density > 1.0F || config.night_sky.milky_way_intensity < 0.0F ||
        config.night_sky.milky_way_intensity > 4.0F || config.night_sky.milky_way_contrast < 0.0F ||
        config.night_sky.milky_way_contrast > 4.0F || config.night_sky.light_pollution < 0.0F ||
        config.night_sky.light_pollution > 1.0F || config.night_sky.procedural_variation < 0.0F ||
        config.night_sky.procedural_variation > 16.0F) {
        throw std::runtime_error("atmosphere night-sky controls are out of range");
    }
    if (config.moon.disk_intensity < 0.0F || config.moon.disk_intensity > 4.0F ||
        config.moon.moonlight_intensity < 0.0F || config.moon.moonlight_intensity > 4.0F ||
        config.moon.phase_offset_days < 0.0F || config.moon.phase_offset_days > 29.530588F ||
        config.moon.angular_radius_scale <= 0.0F || config.moon.angular_radius_scale > 8.0F) {
        throw std::runtime_error("atmosphere moon controls are out of range");
    }
    if (config.time_of_day.time_hours < 0.0F || config.time_of_day.time_hours > 24.0F ||
        config.time_of_day.day_of_year < 1.0F || config.time_of_day.day_of_year > 366.0F) {
        throw std::runtime_error("atmosphere time fields must be within supported ranges");
    }
    if (config.time_of_day.latitude_degrees < -90.0F ||
        config.time_of_day.latitude_degrees > 90.0F ||
        config.time_of_day.azimuth_offset_degrees < -360.0F ||
        config.time_of_day.azimuth_offset_degrees > 360.0F) {
        throw std::runtime_error("atmosphere solar clock angles must be within supported ranges");
    }
    if (config.time_of_day.speed_hours_per_second < 0.0F ||
        config.time_of_day.exposure_bias < -4.0F || config.time_of_day.exposure_bias > 4.0F) {
        throw std::runtime_error("atmosphere time speed and exposure bias are out of range");
    }
}

inline void apply_atmosphere_cloud_run_config(AtmosphereCloudConfig& config,
                                              const RunConfig::CloudOptions& run_clouds) {
    auto apply_float = [](float value, float& target) {
        if (run_config_float_is_set(value)) {
            target = value;
        }
    };

    if (!run_clouds.weather_preset.empty()) {
        apply_atmosphere_cloud_weather_preset(
            config, atmosphere_cloud_weather_preset_from_name(run_clouds.weather_preset));
    }
    if (!run_clouds.quality.empty()) {
        config.layer.quality = atmosphere_cloud_quality_from_name(run_clouds.quality);
    }
    if (run_clouds.enabled >= 0) {
        config.enabled = run_clouds.enabled != 0;
    }
    if (!run_clouds.debug_view.empty()) {
        config.layer.debug_view =
            cubey::render::cloud_layer_debug_view_from_name(run_clouds.debug_view);
    }
    if (!run_clouds.sampling_mode.empty()) {
        config.layer.sampling_mode =
            atmosphere_cloud_sampling_mode_from_name(run_clouds.sampling_mode);
    }
    if (!run_clouds.view_sample_mode.empty()) {
        config.layer.view_sample_mode =
            atmosphere_cloud_view_sample_mode_from_name(run_clouds.view_sample_mode);
    }
    if (!run_clouds.distance_mode.empty()) {
        config.layer.distance_mode =
            atmosphere_cloud_distance_mode_from_name(run_clouds.distance_mode);
    }
    if (!run_clouds.orbit_representation.empty()) {
        config.layer.orbit_representation =
            atmosphere_cloud_orbit_representation_from_name(run_clouds.orbit_representation);
    }
    if (!run_clouds.density_model.empty()) {
        config.layer.density_model =
            atmosphere_cloud_density_model_from_name(run_clouds.density_model);
    }
    if (!run_clouds.resolve_mode.empty()) {
        config.layer.resolve_mode =
            atmosphere_cloud_resolve_mode_from_name(run_clouds.resolve_mode);
    }
    if (run_clouds.view_steps > 0U) {
        config.layer.view_steps_override = static_cast<std::int32_t>(run_clouds.view_steps);
    }
    if (run_clouds.view_samples > 0U) {
        config.layer.view_samples = static_cast<std::int32_t>(run_clouds.view_samples);
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

    if (run_clouds.temporal >= 0) {
        config.layer.temporal_enabled = run_clouds.temporal != 0;
    }
    if (run_clouds.local_volume >= 0) {
        config.layer.local_volume_enabled = run_clouds.local_volume != 0;
    }
    if (run_clouds.horizon_layer >= 0) {
        config.layer.horizon_layer_enabled = run_clouds.horizon_layer != 0;
    }
    config.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
}

[[nodiscard]] inline AtmosphereConfig atmosphere_config_from_run_config(const RunConfig& run) {
    AtmosphereConfig config =
        atmosphere_config_for_preset(atmosphere_preset_from_name(run.atmosphere.preset));
    config.render_view = atmosphere_render_view_from_name(run.debug_view);
    if (!run.atmosphere.time_of_day_mode.empty()) {
        config.time_of_day.mode = sun_control_mode_from_name(run.atmosphere.time_of_day_mode);
    }
    if (!run.atmosphere.night_sky_mode.empty()) {
        config.night_sky.visual_mode =
            night_sky_visual_mode_from_name(run.atmosphere.night_sky_mode);
    }
    if (!run.atmosphere.milky_way_layer.empty()) {
        config.night_sky.layer = night_sky_layer_view_from_name(run.atmosphere.milky_way_layer);
    }
    const bool manual_sun_override =
        run_config_float_is_set(run.atmosphere.sun_elevation_degrees) ||
        run_config_float_is_set(run.atmosphere.sun_azimuth_degrees);
    if (manual_sun_override) {
        config.time_of_day.mode = SunControlMode::ManualSun;
    }
    if (run_config_float_is_set(run.atmosphere.sun_elevation_degrees)) {
        config.sun_elevation_degrees = run.atmosphere.sun_elevation_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.sun_azimuth_degrees)) {
        config.sun_azimuth_degrees = run.atmosphere.sun_azimuth_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.camera_altitude_km)) {
        config.camera_altitude_km = run.atmosphere.camera_altitude_km;
    }
    if (run_config_float_is_set(run.atmosphere.camera_yaw_offset_degrees)) {
        config.camera_yaw_offset_degrees = run.atmosphere.camera_yaw_offset_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.camera_pitch_offset_degrees)) {
        config.camera_pitch_offset_degrees = run.atmosphere.camera_pitch_offset_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.mie_scale)) {
        config.mie_density_scale = run.atmosphere.mie_scale;
    }
    if (run_config_float_is_set(run.atmosphere.time_hours)) {
        config.time_of_day.time_hours = run.atmosphere.time_hours;
    }
    if (run_config_float_is_set(run.atmosphere.day_of_year)) {
        config.time_of_day.day_of_year = run.atmosphere.day_of_year;
    }
    if (run_config_float_is_set(run.atmosphere.latitude_degrees)) {
        config.time_of_day.latitude_degrees = run.atmosphere.latitude_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.sun_azimuth_offset_degrees)) {
        config.time_of_day.azimuth_offset_degrees = run.atmosphere.sun_azimuth_offset_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.time_speed_hours_per_second)) {
        config.time_of_day.speed_hours_per_second = run.atmosphere.time_speed_hours_per_second;
    }
    if (run.atmosphere.time_paused == 1) {
        config.time_of_day.playing = false;
    }
    if (run.atmosphere.auto_exposure >= 0) {
        config.time_of_day.auto_exposure_enabled = run.atmosphere.auto_exposure == 1;
    }
    if (run.pbr.exposure_explicit) {
        config.exposure = run.pbr.exposure;
        config.time_of_day.auto_exposure_enabled = false;
    }
    if (run_config_float_is_set(run.atmosphere.exposure_bias)) {
        config.time_of_day.exposure_bias = run.atmosphere.exposure_bias;
    }
    if (run_config_float_is_set(run.atmosphere.twilight_strength)) {
        config.night_sky.twilight_strength = run.atmosphere.twilight_strength;
    }
    if (run_config_float_is_set(run.atmosphere.twilight_horizon_warmth)) {
        config.night_sky.twilight_horizon_warmth = run.atmosphere.twilight_horizon_warmth;
    }
    if (run_config_float_is_set(run.atmosphere.star_intensity)) {
        config.night_sky.star_intensity = run.atmosphere.star_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.star_density)) {
        config.night_sky.star_density = run.atmosphere.star_density;
    }
    if (run_config_float_is_set(run.atmosphere.milky_way_intensity)) {
        config.night_sky.milky_way_intensity = run.atmosphere.milky_way_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.milky_way_contrast)) {
        config.night_sky.milky_way_contrast = run.atmosphere.milky_way_contrast;
    }
    if (run_config_float_is_set(run.atmosphere.light_pollution)) {
        config.night_sky.light_pollution = run.atmosphere.light_pollution;
    }
    if (run_config_float_is_set(run.atmosphere.milky_way_variation)) {
        config.night_sky.procedural_variation = run.atmosphere.milky_way_variation;
    }
    if (run.atmosphere.moon >= 0) {
        config.moon.enabled = run.atmosphere.moon == 1;
    }
    if (run.atmosphere.reference_geometry >= 0) {
        config.reference_geometry_enabled = run.atmosphere.reference_geometry == 1;
    }
    if (run_config_float_is_set(run.atmosphere.moon_intensity)) {
        config.moon.disk_intensity = run.atmosphere.moon_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.moonlight_intensity)) {
        config.moon.moonlight_intensity = run.atmosphere.moonlight_intensity;
    }
    if (run_config_float_is_set(run.atmosphere.moon_phase_offset_days)) {
        config.moon.phase_offset_days = run.atmosphere.moon_phase_offset_days;
    }
    if (run_config_float_is_set(run.atmosphere.moon_size_scale)) {
        config.moon.angular_radius_scale = run.atmosphere.moon_size_scale;
    }
    apply_atmosphere_cloud_run_config(config.clouds, run.clouds);
    resolve_atmosphere_time_of_day(config);
    validate_atmosphere_config(config);
    return config;
}

} // namespace cubey::projects::atmosphere
