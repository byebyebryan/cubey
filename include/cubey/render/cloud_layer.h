#pragma once

#include <cubey/core/math.h>
#include <cubey/render/material.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph_types.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace cubey::render {

inline constexpr std::uint32_t kCloudLayerComputeGroupSize = 16U;
inline constexpr std::uint32_t kCloudLayerVolumeGroupSize = 4U;
inline constexpr std::uint32_t kCloudLayerBaseNoiseSize = 128U;
inline constexpr std::uint32_t kCloudLayerDetailNoiseSize = 32U;
inline constexpr std::uint32_t kCloudLayerWeatherTextureSize = 1024U;
inline constexpr VkFormat kCloudLayerColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat kCloudLayerNoiseFormat = VK_FORMAT_R8G8B8A8_UNORM;

inline constexpr std::uint32_t kCloudLayerUniformBinding = 0U;
inline constexpr std::uint32_t kCloudLayerOutputBinding = 1U;
inline constexpr std::uint32_t kCloudLayerBaseNoiseBinding = 2U;
inline constexpr std::uint32_t kCloudLayerDetailNoiseBinding = 3U;
inline constexpr std::uint32_t kCloudLayerWeatherBinding = 4U;
inline constexpr std::uint32_t kCloudLayerMetadataBinding = 5U;
inline constexpr std::uint32_t kCloudLayerCompositeCloudBinding = 1U;
inline constexpr std::uint32_t kCloudLayerCompositeMetadataBinding = 2U;
inline constexpr std::uint32_t kCloudLayerCompositeBackgroundBinding = 3U;
inline constexpr std::uint32_t kCloudLayerTemporalCurrentCloudBinding = 0U;
inline constexpr std::uint32_t kCloudLayerTemporalCurrentMetadataBinding = 1U;
inline constexpr std::uint32_t kCloudLayerTemporalHistoryCloudBinding = 2U;
inline constexpr std::uint32_t kCloudLayerTemporalHistoryMetadataBinding = 3U;
inline constexpr std::uint32_t kCloudLayerTemporalUniformBinding = 4U;
inline constexpr std::uint32_t kCloudLayerTemporalOutputBinding = 5U;
inline constexpr std::uint32_t kCloudLayerTemporalOutputMetadataBinding = 6U;

enum class CloudLayerQuality : std::uint32_t {
    Quarter = 0,
    Half = 1,
    Full = 2,
};

enum class CloudLayerSamplingMode : std::uint32_t {
    Interleaved = 0,
    Bayer = 1,
    Off = 2,
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
    CloudLayerBackgroundMode background_mode = CloudLayerBackgroundMode::Atmosphere;
    CloudLayerDistanceMode distance_mode = CloudLayerDistanceMode::Auto;
    CloudLayerOrbitRepresentation orbit_representation =
        CloudLayerOrbitRepresentation::SurfaceShell;
    CloudLayerDebugView debug_view = CloudLayerDebugView::Final;
    bool temporal_enabled = true;
    bool powder_enabled = true;
    bool local_volume_enabled = true;
    bool horizon_layer_enabled = true;

    float planet_radius_m = 600000.0F;
    float bottom_altitude_m = 5000.0F;
    float top_altitude_m = 22000.0F;
    float coverage = 0.45F;
    float density = 0.02F;
    float weather_scale_km = 120.0F;
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
    float sun_intensity = 1.0F;
    VkExtent2D target_extent{};
    std::uint32_t temporal_frame_index = 0;
    float camera_mode = 0.0F;
};

struct CloudLayerFrameUniforms {
    math::Vec4 camera_right_aspect;
    math::Vec4 camera_up_tan_half_fovy;
    math::Vec4 camera_forward_mode;
    math::Vec4 camera_position_radius;
    math::Vec4 cloud_shell;
    math::Vec4 weather;
    math::Vec4 sun_direction_intensity;
    math::Vec4 ref_options;
    math::Vec4 shape_options;
    math::Vec4 weather_feature_weights;
    math::Vec4 cloud_color_top_shadow;
    math::Vec4 cloud_color_bottom_horizon;
    math::Vec4 lighting_strengths;
    math::Vec4 composite_options;
    math::Vec4 sampling_options;
    math::Vec4 temporal_options;
    math::Vec4 background_options;
    math::Vec4 distance_options;
    math::Vec4 orbit_options;
    math::Vec4 orbit_shell_options;
};

static_assert(sizeof(CloudLayerFrameUniforms) == sizeof(float) * 80U);

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
};

static_assert(sizeof(CloudLayerWeatherPushConstants) == sizeof(float) * 4U);

struct CloudLayerProduct {
    RenderGraphTextureHandle cloud{};
    RenderGraphTextureHandle metadata{};
    RenderGraphTextureHandle resolved_cloud{};
    RenderGraphTextureHandle resolved_metadata{};
    VkExtent2D extent{};
};

struct CloudLayerShadowProduct {
    RenderGraphTextureHandle shadow{};
    math::Vec4 world_to_shadow_x{};
    math::Vec4 world_to_shadow_y{};
    math::Vec4 options{};
};

struct CloudLayerReflectionContribution {
    bool available = false;
    float intensity = 0.0F;
};

struct CloudLayerGeneratedShaderFiles {
    ShaderStageFile base_noise{};
    ShaderStageFile detail_noise{};
    ShaderStageFile weather{};
};

struct CloudLayerGeneratedResources {
    Texture3D base_noise;
    Texture3D detail_noise;
    Texture2D weather;
    CloudLayerWeatherPushConstants weather_generation{};
};

[[nodiscard]] vulkan::SamplerConfig cloud_layer_repeat_sampler_config(
    std::uint32_t mip_levels = 1);
[[nodiscard]] std::uint32_t cloud_layer_generated_volume_mip_count(std::uint32_t size);
[[nodiscard]] Texture3DConfig cloud_layer_volume_texture_config(std::uint32_t size);
[[nodiscard]] Texture2DConfig cloud_layer_weather_texture_config();
[[nodiscard]] MaterialPassInfo cloud_layer_march_pass_info();
[[nodiscard]] MaterialPassInfo cloud_layer_composite_pass_info(bool external_background = false);
[[nodiscard]] MaterialPassInfo cloud_layer_temporal_pass_info();
[[nodiscard]] RenderGraphTextureState cloud_layer_sampled_texture_state();
[[nodiscard]] RenderGraphTextureDesc cloud_layer_color_texture_desc(std::string label,
                                                                    VkExtent2D extent);
[[nodiscard]] VkExtent2D cloud_layer_product_extent(VkExtent2D target_extent,
                                                    CloudLayerQuality quality);
[[nodiscard]] CloudLayerQualityBudget cloud_layer_quality_budget(CloudLayerQuality quality);
[[nodiscard]] CloudLayerFrameUniforms cloud_layer_frame_uniforms(const CloudLayerConfig& config,
                                                                 const CloudLayerFrameInfo& frame);
[[nodiscard]] CloudLayerWeatherPushConstants
cloud_layer_weather_push_constants(const CloudLayerConfig& config);
[[nodiscard]] bool cloud_layer_weather_generation_equal(
    const CloudLayerWeatherPushConstants& lhs, const CloudLayerWeatherPushConstants& rhs);
[[nodiscard]] CloudLayerGeneratedResources create_cloud_layer_generated_resources(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const CloudLayerGeneratedShaderFiles& shaders, const CloudLayerConfig& config);
void update_cloud_layer_weather_texture(const cubey::vulkan::Device& device,
                                        cubey::vulkan::GpuRuntime& gpu,
                                        const ShaderStageFile& shader, Texture2D& weather_texture,
                                        CloudLayerWeatherPushConstants push_constants);

} // namespace cubey::render
