#pragma once

#include <cubey/core/run_config.h>
#include <cubey/procedural/artifact_metadata.h>
#include <cubey/render/atmosphere_environment.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace cubey::projects::cloud {

inline constexpr float kCloudsDefaultPlanetRadiusM = 600000.0F;
inline constexpr float kCloudsDefaultBottomAltitudeM = 5000.0F;
inline constexpr float kCloudsDefaultTopAltitudeM = 22000.0F;
inline constexpr std::uint32_t kCloudBaseNoiseSize = 128U;
inline constexpr std::uint32_t kCloudDetailNoiseSize = 32U;
inline constexpr std::uint32_t kCloudWeatherTextureSize = 1024U;

enum class CloudsCameraMode : std::uint32_t {
    Surface = 0,
    SurfaceUp = 1,
    High = 2,
    HighOblique = 3,
    Orbit = 4,
    OrbitTerminator = 5,
};

enum class CloudsQuality : std::uint32_t {
    Quarter = 0,
    Half = 1,
    Full = 2,
};

enum class CloudsWeatherPreset : std::uint32_t {
    FairWeather = 0,
    BrokenCumulus = 1,
    OvercastStratus = 2,
    StormCells = 3,
    HighCirrus = 4,
    Clear = 5,
};

enum class CloudsCloudStyle : std::uint32_t {
    FairWeather = 0,
    BrokenCumulus = 1,
    OvercastStratus = 2,
    StormCells = 3,
    HighCirrus = 4,
};

enum class CloudsSamplingMode : std::uint32_t {
    Interleaved = 0,
    Bayer = 1,
    Off = 2,
};

inline constexpr std::array<CloudsSamplingMode, 3> kCloudsSamplingModes{
    CloudsSamplingMode::Interleaved,
    CloudsSamplingMode::Bayer,
    CloudsSamplingMode::Off,
};

enum class CloudsBackgroundMode : std::uint32_t {
    Atmosphere = 0,
    WaterContext = 1,
};

inline constexpr std::array<CloudsBackgroundMode, 2> kCloudsBackgroundModes{
    CloudsBackgroundMode::Atmosphere,
    CloudsBackgroundMode::WaterContext,
};

enum class CloudsDistanceMode : std::uint32_t {
    Auto = 0,
    Local = 1,
    OrbitShell = 2,
    BlendDebug = 3,
};

inline constexpr std::array<CloudsDistanceMode, 4> kCloudsDistanceModes{
    CloudsDistanceMode::Auto,
    CloudsDistanceMode::Local,
    CloudsDistanceMode::OrbitShell,
    CloudsDistanceMode::BlendDebug,
};

enum class CloudsOrbitRepresentation : std::uint32_t {
    VolumeRaymarch = 0,
    SurfaceShell = 1,
};

inline constexpr std::array<CloudsOrbitRepresentation, 2> kCloudsOrbitRepresentations{
    CloudsOrbitRepresentation::VolumeRaymarch,
    CloudsOrbitRepresentation::SurfaceShell,
};

enum class CloudsDebugView : std::uint32_t {
    Final = 0,
    Weather = 1,
    Density = 2,
    Transmittance = 3,
    Lighting = 4,
    Shadow = 5,
    Steps = 6,
    Background = 7,
    RawFinal = 8,
    CloudAlpha = 11,
    Distance = 15,
    BaseDensity = 16,
    DetailDensity = 17,
    AmbientLight = 18,
    DirectLight = 19,
    PhaseLight = 20,
    MetadataDistance = 21,
    MetadataAlpha = 22,
    MetadataConfidence = 23,
    MetadataDensity = 24,
    CloudType = 25,
    VisibleDensity = 26,
    VisibleCloudType = 27,
    WeatherEdge = 28,
    WeatherBias = 29,
    DistanceRegime = 30,
    LocalAlpha = 31,
    OrbitAlpha = 32,
    OrbitCoverage = 33,
    OrbitDetail = 34,
    OrbitHull = 35,
    OrbitEnvelope = 36,
    OrbitShellAlpha = 37,
    OrbitShellHeight = 38,
    OrbitShellNormal = 39,
    OrbitShellShadow = 40,
};

enum class CloudsGeneratedArtifact : std::uint32_t {
    BaseNoiseVolume = 0,
    DetailNoiseVolume = 1,
    WeatherMap = 2,
};

inline constexpr std::array<CloudsGeneratedArtifact, 3> kCloudsGeneratedArtifacts{
    CloudsGeneratedArtifact::BaseNoiseVolume,
    CloudsGeneratedArtifact::DetailNoiseVolume,
    CloudsGeneratedArtifact::WeatherMap,
};

inline constexpr std::array<CloudsDebugView, 36> kCloudsDebugViews{
    CloudsDebugView::Final,        CloudsDebugView::RawFinal,
    CloudsDebugView::Weather,      CloudsDebugView::Density,
    CloudsDebugView::Transmittance, CloudsDebugView::Lighting,
    CloudsDebugView::AmbientLight, CloudsDebugView::DirectLight,
    CloudsDebugView::PhaseLight,   CloudsDebugView::Shadow,
    CloudsDebugView::Steps,        CloudsDebugView::Background,
    CloudsDebugView::CloudAlpha,   CloudsDebugView::Distance,
    CloudsDebugView::MetadataDistance,
    CloudsDebugView::MetadataAlpha,
    CloudsDebugView::MetadataConfidence,
    CloudsDebugView::MetadataDensity,
    CloudsDebugView::BaseDensity,  CloudsDebugView::DetailDensity,
    CloudsDebugView::CloudType,
    CloudsDebugView::WeatherEdge,
    CloudsDebugView::WeatherBias,
    CloudsDebugView::VisibleDensity,
    CloudsDebugView::VisibleCloudType,
    CloudsDebugView::DistanceRegime,
    CloudsDebugView::LocalAlpha,
    CloudsDebugView::OrbitAlpha,
    CloudsDebugView::OrbitCoverage,
    CloudsDebugView::OrbitDetail,
    CloudsDebugView::OrbitHull,
    CloudsDebugView::OrbitEnvelope,
    CloudsDebugView::OrbitShellAlpha,
    CloudsDebugView::OrbitShellHeight,
    CloudsDebugView::OrbitShellNormal,
    CloudsDebugView::OrbitShellShadow,
};

struct CloudsTimeConfig {
    bool solar_clock = true;
    bool playing = true;
    float time_hours = 14.0F;
    float day_of_year = cubey::render::kAtmosphereEnvironmentDefaultDayOfYear;
    float latitude_degrees = cubey::render::kAtmosphereEnvironmentDefaultLatitudeDegrees;
    float azimuth_offset_degrees =
        cubey::render::kAtmosphereEnvironmentDefaultAzimuthOffsetDegrees;
    float speed_hours_per_second = 1.0F;
    float manual_sun_elevation_degrees = 38.0F;
    float manual_sun_azimuth_degrees = -20.0F;
};

struct CloudsConfig {
    CloudsCameraMode camera_mode = CloudsCameraMode::Surface;
    CloudsQuality quality = CloudsQuality::Full;
    CloudsWeatherPreset weather_preset = CloudsWeatherPreset::BrokenCumulus;
    CloudsCloudStyle cloud_style = CloudsCloudStyle::BrokenCumulus;
    CloudsSamplingMode sampling_mode = CloudsSamplingMode::Bayer;
    CloudsBackgroundMode background_mode = CloudsBackgroundMode::Atmosphere;
    CloudsDistanceMode distance_mode = CloudsDistanceMode::Auto;
    CloudsOrbitRepresentation orbit_representation = CloudsOrbitRepresentation::SurfaceShell;
    CloudsDebugView debug_view = CloudsDebugView::Final;
    bool temporal_enabled = true;
    CloudsTimeConfig time{};

    float planet_radius_m = kCloudsDefaultPlanetRadiusM;
    float camera_altitude_m = 800.0F;
    float bottom_altitude_m = kCloudsDefaultBottomAltitudeM;
    float top_altitude_m = kCloudsDefaultTopAltitudeM;
    float coverage = 0.45F;
    float density = 0.02F;
    float weather_scale_km = 120.0F;
    float vertical_shear_fraction = 0.0F;
    float wind_speed_mps = 450.0F;
    float shadow_strength = 0.82F;
    float horizon_strength = 0.48F;
    float weather_fronts = 1.0F;
    float weather_cells = 1.0F;
    float weather_streaks = 1.0F;
    float weather_softness = 0.22F;
    float weather_influence = 0.0F;
    float detail_erosion = 1.0F;
    float crispiness = 40.0F;
    float curliness = 0.10F;
    float absorption = 0.28F;
    float ambient_strength = 0.82F;
    float direct_strength = 1.28F;
    float phase_strength = 1.14F;
    float final_contrast = 1.17F;
    float final_saturation = 1.12F;
    float resolve_strength = 0.48F;
    float horizon_glow_strength = 0.55F;
    float sun_glare_strength = 1.0F;
    float jitter_strength = 1.0F;
    float orbit_transition_start_m = 16000.0F;
    float orbit_transition_end_m = 80000.0F;
    float far_shell_start_m = 45000.0F;
    float far_shell_end_m = 220000.0F;
    float orbit_detail_strength = 0.70F;
    float orbit_density_scale = 0.02F;
    float orbit_fill = 1.0F;
    float orbit_motion_strength = 1.0F;
    float orbit_shell_extinction = 2.8F;
    bool powder_enabled = true;
    bool local_volume_enabled = true;
    bool horizon_layer_enabled = true;
};

struct CloudsQualityBudget {
    std::int32_t view_steps = 40;
    std::int32_t light_steps = 5;
    float resolution_scale = 0.5F;
};

[[nodiscard]] CloudsCameraMode clouds_camera_mode_from_string(std::string_view value);
[[nodiscard]] const char* clouds_camera_mode_name(CloudsCameraMode mode);
[[nodiscard]] CloudsQuality clouds_quality_from_string(std::string_view value);
[[nodiscard]] const char* clouds_quality_name(CloudsQuality quality);
[[nodiscard]] CloudsWeatherPreset clouds_weather_preset_from_string(std::string_view value);
[[nodiscard]] const char* clouds_weather_preset_name(CloudsWeatherPreset preset);
void apply_clouds_weather_preset(CloudsConfig& config, CloudsWeatherPreset preset);
[[nodiscard]] CloudsSamplingMode clouds_sampling_mode_from_string(std::string_view value);
[[nodiscard]] const char* clouds_sampling_mode_name(CloudsSamplingMode mode);
[[nodiscard]] CloudsBackgroundMode clouds_background_mode_from_string(std::string_view value);
[[nodiscard]] const char* clouds_background_mode_name(CloudsBackgroundMode mode);
[[nodiscard]] CloudsDistanceMode clouds_distance_mode_from_string(std::string_view value);
[[nodiscard]] const char* clouds_distance_mode_name(CloudsDistanceMode mode);
[[nodiscard]] CloudsOrbitRepresentation
clouds_orbit_representation_from_string(std::string_view value);
[[nodiscard]] const char* clouds_orbit_representation_name(CloudsOrbitRepresentation mode);
[[nodiscard]] CloudsDebugView clouds_debug_view_from_string(std::string_view value);
[[nodiscard]] const char* clouds_debug_view_name(CloudsDebugView view);
[[nodiscard]] CloudsDebugView next_clouds_debug_view(CloudsDebugView view);
[[nodiscard]] CloudsQualityBudget clouds_quality_budget(CloudsQuality quality);
[[nodiscard]] std::uint32_t cloud_generated_volume_mip_count(std::uint32_t size);
[[nodiscard]] cubey::procedural::ProceduralArtifactMetadata clouds_generated_artifact_metadata(
    CloudsGeneratedArtifact artifact);
[[nodiscard]] float clouds_default_camera_altitude_m(CloudsCameraMode mode);
[[nodiscard]] CloudsConfig clouds_config_from_run_config(const RunConfig& run_config);
void advance_clouds_time(CloudsConfig& config, double delta_seconds);
void validate_clouds_config(const CloudsConfig& config);

} // namespace cubey::projects::cloud
