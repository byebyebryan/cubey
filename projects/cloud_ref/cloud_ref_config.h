#pragma once

#include <cubey/core/run_config.h>
#include <cubey/render/atmosphere_environment.h>

#include <array>
#include <cstdint>

namespace cubey::projects::cloud_ref {

inline constexpr float kCloudsDefaultPlanetRadiusM = 600000.0F;
inline constexpr float kCloudsDefaultBottomAltitudeM = 5000.0F;
inline constexpr float kCloudsDefaultTopAltitudeM = 16000.0F;

enum class CloudsCameraMode : std::uint32_t {
    Surface = 0,
    SurfaceUp = 1,
    High = 2,
    HighOblique = 3,
    Orbit = 4,
    OrbitTerminator = 5,
    SurfaceSun = 6,
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
};

enum class CloudsCloudStyle : std::uint32_t {
    FairWeather = 0,
    BrokenCumulus = 1,
    OvercastStratus = 2,
    StormCells = 3,
    HighCirrus = 4,
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
    ViewOpticalDepth = 21,
    LightOpticalDepth = 22,
};

enum class CloudsResolveMode : std::uint32_t {
    TerrainPost = 0,
    MetadataBilateral = 1,
};

inline constexpr std::array<CloudsDebugView, 18> kCloudsDebugViews{
    CloudsDebugView::Final,        CloudsDebugView::RawFinal,
    CloudsDebugView::Weather,      CloudsDebugView::Density,
    CloudsDebugView::Transmittance, CloudsDebugView::Lighting,
    CloudsDebugView::AmbientLight, CloudsDebugView::DirectLight,
    CloudsDebugView::PhaseLight,   CloudsDebugView::Shadow,
    CloudsDebugView::Steps,        CloudsDebugView::Background,
    CloudsDebugView::CloudAlpha,   CloudsDebugView::Distance,
    CloudsDebugView::BaseDensity,  CloudsDebugView::DetailDensity,
    CloudsDebugView::ViewOpticalDepth,
    CloudsDebugView::LightOpticalDepth,
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
    CloudsWeatherPreset weather_preset = CloudsWeatherPreset::FairWeather;
    CloudsCloudStyle cloud_style = CloudsCloudStyle::FairWeather;
    CloudsDebugView debug_view = CloudsDebugView::Final;
    bool temporal_enabled = true;
    CloudsTimeConfig time{};
    std::int32_t view_steps_override = 0;
    std::int32_t view_samples = 1;

    float planet_radius_m = kCloudsDefaultPlanetRadiusM;
    float camera_altitude_m = 800.0F;
    float bottom_altitude_m = kCloudsDefaultBottomAltitudeM;
    float top_altitude_m = 14000.0F;
    float coverage = 0.30F;
    float density = 0.016F;
    float weather_scale_km = 260.0F;
    float wind_speed_mps = 260.0F;
    float shadow_strength = 0.15F;
    float horizon_strength = 0.62F;
    float weather_fronts = 1.0F;
    float weather_cells = 1.0F;
    float weather_streaks = 1.0F;
    float detail_erosion = 1.0F;
    float ambient_strength = 1.30F;
    float direct_strength = 1.15F;
    float phase_strength = 1.20F;
    float powder_strength = 0.20F;
    float final_contrast = 0.98F;
    float final_saturation = 1.0F;
    float horizon_glow_strength = 1.05F;
    float sun_glare_strength = 1.0F;
    float crispiness = 40.0F;
    float curliness = 0.10F;
    float absorption = 0.28F;
    bool post_blur_enabled = true;
    CloudsResolveMode resolve_mode = CloudsResolveMode::TerrainPost;
    float post_blur_strength = 1.0F;
    float post_blur_radius_px = 1.5F;
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
[[nodiscard]] CloudsDebugView clouds_debug_view_from_string(std::string_view value);
[[nodiscard]] const char* clouds_debug_view_name(CloudsDebugView view);
[[nodiscard]] CloudsDebugView next_clouds_debug_view(CloudsDebugView view);
[[nodiscard]] CloudsResolveMode clouds_resolve_mode_from_string(std::string_view value);
[[nodiscard]] const char* clouds_resolve_mode_name(CloudsResolveMode mode);
[[nodiscard]] CloudsQualityBudget clouds_quality_budget(CloudsQuality quality);
[[nodiscard]] float clouds_default_camera_altitude_m(CloudsCameraMode mode);
[[nodiscard]] CloudsConfig clouds_config_from_run_config(const RunConfig& run_config);
void advance_clouds_time(CloudsConfig& config, double delta_seconds);
void validate_clouds_config(const CloudsConfig& config);

} // namespace cubey::projects::cloud_ref
