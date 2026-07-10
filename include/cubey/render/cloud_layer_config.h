#pragma once

#include <cubey/core/math.h>
#include <cubey/render/celestial_system.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace cubey::render {

enum class CloudLayerQuality : std::uint32_t {
    Quarter = 0,
    Half = 1,
    Full = 2,
};

enum class CloudLayerSamplingMode : std::uint32_t {
    Interleaved = 0,
    Bayer = 1,
    Off = 2,
    BlueNoise = 3,
};

enum class CloudLayerViewSampleMode : std::uint32_t {
    SingleFrame = 0,
    TemporalPhased = 1,
};

enum class CloudLayerBackgroundMode : std::uint32_t {
    Atmosphere = 0,
    WaterContext = 1,
};

enum class CloudLayerDistanceMode : std::uint32_t {
    Auto = 0,
    Local = 1,
    OrbitShell = 2,
    BlendDebug = 3,
};

enum class CloudLayerOrbitRepresentation : std::uint32_t {
    VolumeRaymarch = 0,
    SurfaceShell = 1,
};

enum class CloudLayerDensityModel : std::uint32_t {
    RefDensity = 0,
    ExperimentalAerialOrbit = 1,
    SurfaceVolume = 2,
};

enum class CloudLayerResolveMode : std::uint32_t {
    TerrainPost = 0,
    MetadataBilateral = 1,
};

enum class CloudLayerCloudStyle : std::uint32_t {
    FairWeather = 0,
    BrokenCumulus = 1,
    OvercastStratus = 2,
    StormCells = 3,
    HighCirrus = 4,
};

enum class CloudLayerDebugView : std::uint32_t {
    Final = 0,
    AuthoredWeather = 1,
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
    CoverageBias = 29,
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
    LocalScatter = 41,
    LocalClear = 42,
    LocalStructure = 43,
    LocalEdgeDetail = 44,
    FarShellAlpha = 45,
    LocalWithShellAlpha = 46,
    TransitionWeights = 47,
    SceneDepthOcclusion = 48,
    OrbitShellFootprint = 49,
    OrbitShellFilter = 50,
    OrbitShellMass = 51,
    JitterPattern = 52,
    HorizonStepLength = 53,
    HorizonFilterLod = 54,
    HorizonHandoff = 55,
    LocalTruncation = 56,
    IntegratedHorizonAlpha = 57,
    IntegratedHorizonRadiance = 58,
    EdgeMask = 59,
};

inline constexpr std::array<CloudLayerDebugView, 55> kCloudLayerDebugViews{
    CloudLayerDebugView::Final,
    CloudLayerDebugView::RawFinal,
    CloudLayerDebugView::AuthoredWeather,
    CloudLayerDebugView::Density,
    CloudLayerDebugView::Transmittance,
    CloudLayerDebugView::Lighting,
    CloudLayerDebugView::AmbientLight,
    CloudLayerDebugView::DirectLight,
    CloudLayerDebugView::PhaseLight,
    CloudLayerDebugView::Shadow,
    CloudLayerDebugView::Steps,
    CloudLayerDebugView::Background,
    CloudLayerDebugView::CloudAlpha,
    CloudLayerDebugView::Distance,
    CloudLayerDebugView::MetadataDistance,
    CloudLayerDebugView::MetadataAlpha,
    CloudLayerDebugView::MetadataConfidence,
    CloudLayerDebugView::MetadataDensity,
    CloudLayerDebugView::BaseDensity,
    CloudLayerDebugView::DetailDensity,
    CloudLayerDebugView::CloudType,
    CloudLayerDebugView::LocalScatter,
    CloudLayerDebugView::LocalClear,
    CloudLayerDebugView::LocalStructure,
    CloudLayerDebugView::LocalEdgeDetail,
    CloudLayerDebugView::WeatherEdge,
    CloudLayerDebugView::CoverageBias,
    CloudLayerDebugView::VisibleDensity,
    CloudLayerDebugView::VisibleCloudType,
    CloudLayerDebugView::DistanceRegime,
    CloudLayerDebugView::TransitionWeights,
    CloudLayerDebugView::LocalAlpha,
    CloudLayerDebugView::FarShellAlpha,
    CloudLayerDebugView::LocalWithShellAlpha,
    CloudLayerDebugView::OrbitAlpha,
    CloudLayerDebugView::OrbitCoverage,
    CloudLayerDebugView::OrbitDetail,
    CloudLayerDebugView::OrbitHull,
    CloudLayerDebugView::OrbitEnvelope,
    CloudLayerDebugView::OrbitShellAlpha,
    CloudLayerDebugView::OrbitShellHeight,
    CloudLayerDebugView::OrbitShellNormal,
    CloudLayerDebugView::OrbitShellShadow,
    CloudLayerDebugView::OrbitShellFootprint,
    CloudLayerDebugView::OrbitShellFilter,
    CloudLayerDebugView::OrbitShellMass,
    CloudLayerDebugView::JitterPattern,
    CloudLayerDebugView::HorizonStepLength,
    CloudLayerDebugView::HorizonFilterLod,
    CloudLayerDebugView::HorizonHandoff,
    CloudLayerDebugView::LocalTruncation,
    CloudLayerDebugView::IntegratedHorizonAlpha,
    CloudLayerDebugView::IntegratedHorizonRadiance,
    CloudLayerDebugView::EdgeMask,
    CloudLayerDebugView::SceneDepthOcclusion,
};

struct CloudLayerQualityBudget {
    std::int32_t view_steps = 40;
    std::int32_t light_steps = 5;
    float resolution_scale = 0.5F;
};

struct CloudLayerConfig {
    CloudLayerQuality quality = CloudLayerQuality::Full;
    CloudLayerCloudStyle cloud_style = CloudLayerCloudStyle::BrokenCumulus;
    CloudLayerSamplingMode sampling_mode = CloudLayerSamplingMode::Bayer;
    CloudLayerViewSampleMode view_sample_mode = CloudLayerViewSampleMode::SingleFrame;
    CloudLayerBackgroundMode background_mode = CloudLayerBackgroundMode::Atmosphere;
    CloudLayerDistanceMode distance_mode = CloudLayerDistanceMode::Auto;
    CloudLayerOrbitRepresentation orbit_representation =
        CloudLayerOrbitRepresentation::SurfaceShell;
    CloudLayerDensityModel density_model = CloudLayerDensityModel::SurfaceVolume;
    CloudLayerResolveMode resolve_mode = CloudLayerResolveMode::TerrainPost;
    CloudLayerDebugView debug_view = CloudLayerDebugView::Final;
    bool temporal_enabled = false;
    bool powder_enabled = true;
    bool local_volume_enabled = true;
    bool horizon_layer_enabled = true;

    std::int32_t view_steps_override = 0;
    std::int32_t view_samples = 1;

    float planet_radius_m = 600000.0F;
    float bottom_altitude_m = 5000.0F;
    float top_altitude_m = 22000.0F;
    float coverage = 0.45F;
    float density = 0.02F;
    float weather_scale_km = 120.0F;
    float shape_domain_km = 600.0F;
    float footprint_filter_strength = 1.0F;
    float edge_softness = 1.0F;
    float edge_detail_fade = 0.75F;
    float edge_resolve_strength = 0.70F;
    float vertical_shear_fraction = 0.0F;
    float wind_offset_m = 0.0F;
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
    float twilight_color_strength = 0.72F;
    float twilight_edge_strength = 0.45F;
    float twilight_saturation_strength = 0.82F;
    float afterglow_strength = 0.22F;
    float powder_strength = 0.20F;
    float final_contrast = 1.17F;
    float final_saturation = 1.12F;
    float resolve_strength = 0.48F;
    float horizon_glow_strength = 0.55F;
    float sun_glare_strength = 1.0F;
    float jitter_strength = 1.0F;
    float orbit_transition_start_m = 45000.0F;
    float orbit_transition_end_m = 180000.0F;
    float far_shell_start_m = 30000.0F;
    float far_shell_end_m = 160000.0F;
    float far_shell_strength = 1.25F;
    float orbit_detail_strength = 0.70F;
    float orbit_density_scale = 0.02F;
    float orbit_fill = 1.0F;
    float orbit_motion_strength = 1.0F;
    float orbit_shell_extinction = 2.8F;
};

struct CloudLayerFrameInfo {
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    math::Vec3 camera_right{1.0F, 0.0F, 0.0F};
    math::Vec3 camera_up{0.0F, 1.0F, 0.0F};
    math::Vec3 camera_forward{0.0F, 0.0F, -1.0F};
    float tan_half_fovy = 0.0F;
    math::Vec3 sun_direction{0.0F, 1.0F, 0.0F};
    math::Vec3 sun_color{1.0F, 0.94F, 0.82F};
    float sun_intensity = 1.0F;
    math::Vec3 moon_direction{0.0F, 1.0F, 0.0F};
    math::Vec3 moon_color{kCelestialMoonSurfaceColor};
    float moon_intensity = 0.0F;
    math::Vec3 ambient_color{0.045F, 0.045F, 0.045F};
    float ambient_intensity = 1.0F;
    VkExtent2D target_extent{};
    std::uint32_t temporal_frame_index = 0;
    float camera_mode = 0.0F;
    bool external_background = false;
    float near_plane_m = 1.0F;
    float far_plane_m = 1000.0F;
    bool scene_depth_occlusion_enabled = false;
    float scene_depth_fade_m = 500.0F;
};

struct CloudLayerViewRegimeInput {
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    math::Vec3 camera_forward{0.0F, 0.0F, -1.0F};
    float planet_radius_m = 1.0F;
    float orbit_transition_start_m = 45000.0F;
    float orbit_transition_end_m = 180000.0F;
};

struct CloudLayerViewRegime {
    float camera_mode = 0.0F;
    float altitude_m = 0.0F;
    float altitude_blend = 0.0F;
    float horizon_grazing = 0.0F;
};

struct CloudLayerFrameUniforms {
    math::Vec4 camera_right_aspect;
    math::Vec4 camera_up_tan_half_fovy;
    math::Vec4 camera_forward_mode;
    math::Vec4 camera_position_radius;
    math::Vec4 cloud_shell;
    math::Vec4 weather;
    math::Vec4 sun_direction_intensity;
    math::Vec4 sun_color;
    math::Vec4 moon_direction_intensity;
    math::Vec4 moon_color;
    math::Vec4 ambient_color_intensity;
    math::Vec4 ref_options;
    math::Vec4 shape_options;
    math::Vec4 weather_feature_weights;
    math::Vec4 cloud_color_top_shadow;
    math::Vec4 cloud_color_bottom_horizon;
    math::Vec4 lighting_strengths;
    math::Vec4 twilight_options;
    math::Vec4 composite_options;
    math::Vec4 sampling_options;
    math::Vec4 temporal_options;
    math::Vec4 background_options;
    math::Vec4 distance_options;
    math::Vec4 orbit_options;
    math::Vec4 orbit_shell_options;
    math::Vec4 scene_depth_options;
    math::Vec4 density_options;
    math::Vec4 edge_options;
};

static_assert(sizeof(CloudLayerFrameUniforms) == sizeof(float) * 112U);

struct CloudLayerTemporalUniforms {
    math::Vec4 current_camera_right_aspect;
    math::Vec4 current_camera_up_tan_half_fovy;
    math::Vec4 current_camera_forward_mode;
    math::Vec4 current_camera_position_radius;
    math::Vec4 previous_camera_right_aspect;
    math::Vec4 previous_camera_up_tan_half_fovy;
    math::Vec4 previous_camera_forward_mode;
    math::Vec4 previous_camera_position_radius;
    math::Vec4 current_weather;
    math::Vec4 previous_weather;
    math::Vec4 options;
};

static_assert(sizeof(CloudLayerTemporalUniforms) == sizeof(float) * 44U);

struct CloudLayerWeatherPushConstants {
    float fronts = 1.0F;
    float cells = 1.0F;
    float streaks = 1.0F;
    float cloud_style = 1.0F;
    float density_model = 1.0F;
};

static_assert(sizeof(CloudLayerWeatherPushConstants) == sizeof(float) * 5U);

[[nodiscard]] VkExtent2D cloud_layer_product_extent(VkExtent2D target_extent,
                                                    CloudLayerQuality quality);
[[nodiscard]] CloudLayerQualityBudget cloud_layer_quality_budget(CloudLayerQuality quality);
[[nodiscard]] CloudLayerDebugView cloud_layer_debug_view_from_name(std::string_view value);
[[nodiscard]] const char* cloud_layer_debug_view_name(CloudLayerDebugView view);
[[nodiscard]] CloudLayerDebugView next_cloud_layer_debug_view(CloudLayerDebugView view);
[[nodiscard]] CloudLayerFrameUniforms cloud_layer_frame_uniforms(const CloudLayerConfig& config,
                                                                 const CloudLayerFrameInfo& frame);
[[nodiscard]] CloudLayerViewRegime cloud_layer_view_regime(const CloudLayerViewRegimeInput& input);
[[nodiscard]] CloudLayerWeatherPushConstants
cloud_layer_weather_push_constants(const CloudLayerConfig& config);
[[nodiscard]] bool cloud_layer_weather_generation_equal(const CloudLayerWeatherPushConstants& lhs,
                                                        const CloudLayerWeatherPushConstants& rhs);

} // namespace cubey::render
