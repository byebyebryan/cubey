#pragma once

#include <cubey/core/run_config.h>
#include <cubey/render/atmosphere_environment.h>

#include <array>
#include <cstdint>

namespace cubey::projects::clouds {

inline constexpr float kCloudsDefaultPlanetRadiusM = 6371000.0F;
inline constexpr float kCloudsDefaultBottomAltitudeM = 1500.0F;
inline constexpr float kCloudsDefaultTopAltitudeM = 7500.0F;

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
    Atmosphere = 8,
    Ground = 9,
    GroundHit = 10,
    CloudAlpha = 11,
    Shell = 12,
    SurfaceShadow = 13,
};

inline constexpr std::array<CloudsDebugView, 14> kCloudsDebugViews{
    CloudsDebugView::Final,        CloudsDebugView::Weather, CloudsDebugView::Density,
    CloudsDebugView::Transmittance, CloudsDebugView::Lighting, CloudsDebugView::Shadow,
    CloudsDebugView::Steps,        CloudsDebugView::Background,
    CloudsDebugView::Atmosphere,   CloudsDebugView::Ground,
    CloudsDebugView::GroundHit,    CloudsDebugView::CloudAlpha,
    CloudsDebugView::Shell,        CloudsDebugView::SurfaceShadow,
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
    CloudsQuality quality = CloudsQuality::Half;
    CloudsWeatherPreset weather_preset = CloudsWeatherPreset::BrokenCumulus;
    CloudsCloudStyle cloud_style = CloudsCloudStyle::BrokenCumulus;
    CloudsDebugView debug_view = CloudsDebugView::Final;
    CloudsTimeConfig time{};

    float planet_radius_m = kCloudsDefaultPlanetRadiusM;
    float camera_altitude_m = 1200.0F;
    float bottom_altitude_m = kCloudsDefaultBottomAltitudeM;
    float top_altitude_m = kCloudsDefaultTopAltitudeM;
    float coverage = 0.58F;
    float density = 1.18F;
    float weather_scale_km = 170.0F;
    float wind_speed_mps = 18.0F;
    float shadow_strength = 0.65F;
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
[[nodiscard]] CloudsQualityBudget clouds_quality_budget(CloudsQuality quality);
[[nodiscard]] float clouds_default_camera_altitude_m(CloudsCameraMode mode);
[[nodiscard]] CloudsConfig clouds_config_from_run_config(const RunConfig& run_config);
void advance_clouds_time(CloudsConfig& config, double delta_seconds);
void validate_clouds_config(const CloudsConfig& config);

} // namespace cubey::projects::clouds
