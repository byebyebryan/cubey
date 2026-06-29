#pragma once

#include <cubey/core/math.h>
#include <cubey/render/generated_texture.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/render_graph_types.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::render {

inline constexpr std::uint32_t kCloudLayerComputeGroupSize = 16U;
inline constexpr std::uint32_t kCloudLayerVolumeGroupSize = 4U;
inline constexpr std::uint32_t kCloudLayerBaseNoiseSize = 128U;
inline constexpr std::uint32_t kCloudLayerDetailNoiseSize = 32U;
inline constexpr std::uint32_t kCloudLayerWeatherTextureSize = 1024U;
inline constexpr std::uint32_t kCloudLayerBlueNoiseTextureSize = 128U;
inline constexpr VkFormat kCloudLayerColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat kCloudLayerNoiseFormat = VK_FORMAT_R8G8B8A8_UNORM;

inline constexpr std::uint32_t kCloudLayerUniformBinding = 0U;
inline constexpr std::uint32_t kCloudLayerOutputBinding = 1U;
inline constexpr std::uint32_t kCloudLayerBaseNoiseBinding = 2U;
inline constexpr std::uint32_t kCloudLayerDetailNoiseBinding = 3U;
inline constexpr std::uint32_t kCloudLayerWeatherBinding = 4U;
inline constexpr std::uint32_t kCloudLayerMetadataBinding = 5U;
inline constexpr std::uint32_t kCloudLayerBlueNoiseBinding = 6U;
inline constexpr std::uint32_t kCloudLayerCompositeCloudBinding = 1U;
inline constexpr std::uint32_t kCloudLayerCompositeMetadataBinding = 2U;
inline constexpr std::uint32_t kCloudLayerCompositeBackgroundBinding = 3U;
inline constexpr std::uint32_t kCloudLayerCompositeSceneDepthBinding = 4U;
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
    BlueNoise = 3,
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
};

inline constexpr std::array<CloudLayerDebugView, 54> kCloudLayerDebugViews{
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
    CloudLayerBackgroundMode background_mode = CloudLayerBackgroundMode::Atmosphere;
    CloudLayerDistanceMode distance_mode = CloudLayerDistanceMode::Auto;
    CloudLayerOrbitRepresentation orbit_representation =
        CloudLayerOrbitRepresentation::SurfaceShell;
    CloudLayerDebugView debug_view = CloudLayerDebugView::Final;
    bool temporal_enabled = false;
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
    float jitter_strength = 0.65F;
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
    math::Vec4 scene_depth_options;
};

static_assert(sizeof(CloudLayerFrameUniforms) == sizeof(float) * 84U);

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
    ShaderStageFile blue_noise{};
};

struct CloudLayerRuntimeShaderFiles {
    CloudLayerGeneratedShaderFiles generated{};
    ShaderStageFile march{};
    ShaderStageFile temporal{};
    ShaderStageFile composite_vertex{};
    ShaderStageFile composite_fragment{};
};

enum class CloudLayerCompositeMode : std::uint32_t {
    Standalone = 0,
    ExternalBackground = 1,
    ExternalBackgroundSceneDepth = 2,
};

struct CloudLayerGeneratedResources {
    Texture3D base_noise;
    Texture3D detail_noise;
    Texture2D weather;
    Texture2D blue_noise;
    CloudLayerWeatherPushConstants weather_generation{};
};

struct CloudLayerRuntimeFrame {
    CloudLayerProduct product{};
    RenderGraphTextureHandle history_cloud_read{};
    RenderGraphTextureHandle history_metadata_read{};
    RenderGraphTextureHandle history_cloud_write{};
    RenderGraphTextureHandle history_metadata_write{};
    bool temporal_pass_enabled = false;
    std::uint32_t history_write_index = 0;
    CloudLayerFrameUniforms frame_uniforms{};
};

class CloudLayerRuntime {
  public:
    CloudLayerRuntime() = default;

    CloudLayerRuntime(const CloudLayerRuntime&) = delete;
    CloudLayerRuntime& operator=(const CloudLayerRuntime&) = delete;
    CloudLayerRuntime(CloudLayerRuntime&&) = delete;
    CloudLayerRuntime& operator=(CloudLayerRuntime&&) = delete;

    void create_generated_resources(const cubey::vulkan::Device& device,
                                    cubey::vulkan::GpuRuntime& gpu,
                                    const CloudLayerGeneratedShaderFiles& shaders,
                                    const CloudLayerConfig& config);
    void destroy_generated_resources();
    void update_weather_texture(const cubey::vulkan::Device& device,
                                cubey::vulkan::GpuRuntime& gpu, const ShaderStageFile& shader,
                                const CloudLayerConfig& config, bool force = false);
    void destroy_swapchain_resources();
    void create_swapchain_resources(const cubey::vulkan::Device& device,
                                    const CloudLayerRuntimeShaderFiles& shaders,
                                    CloudLayerCompositeMode composite_mode, VkFormat color_format,
                                    VkExtent2D extent, std::uint32_t frame_slot_count,
                                    const CloudLayerConfig& config);

    [[nodiscard]] std::uint32_t temporal_frame_index() const noexcept {
        return temporal_frame_index_;
    }
    [[nodiscard]] const CloudLayerGeneratedResources& generated_resources() const;

    void upload_frame_uniforms(FrameSlot frame_slot,
                               const CloudLayerFrameUniforms& uniforms) const;
    [[nodiscard]] CloudLayerRuntimeFrame declare_product(RenderGraphBuilder& graph,
                                                         VkExtent2D target_extent,
                                                         const CloudLayerConfig& config,
                                                         FrameSlot frame_slot,
                                                         CloudLayerFrameUniforms uniforms) const;
    void declare_composite(RenderGraphBuilder& graph, RenderGraphTextureHandle target,
                           const CloudLayerRuntimeFrame& frame, FrameSlot frame_slot,
                           std::optional<RenderGraphTextureHandle> background = std::nullopt,
                           std::optional<RenderGraphTextureHandle> scene_depth = std::nullopt) const;
    void update_descriptors(const cubey::vulkan::Device& device, FrameSlot frame_slot,
                            const CompiledRenderGraph& graph,
                            const RenderGraphResourceSet& resources,
                            const CloudLayerRuntimeFrame& frame,
                            std::optional<RenderGraphTextureHandle> background = std::nullopt,
                            std::optional<RenderGraphTextureHandle> scene_depth = std::nullopt) const;
    void invalidate_history();
    void complete_frame(FrameSlot frame_slot, const CloudLayerRuntimeFrame& frame);

  private:
    [[nodiscard]] const FrameUniformBuffer<CloudLayerFrameUniforms>& frame_uniforms() const;
    [[nodiscard]] const FrameUniformBuffer<CloudLayerTemporalUniforms>& temporal_uniforms() const;
    [[nodiscard]] const MaterialInstance& march_material() const;
    [[nodiscard]] const MaterialInstance& temporal_material() const;
    [[nodiscard]] const MaterialInstance& composite_material() const;
    [[nodiscard]] const ComputePipelineResource& march_pipeline() const;
    [[nodiscard]] const ComputePipelineResource& temporal_pipeline() const;
    [[nodiscard]] const GraphicsPipelineResource& composite_pipeline() const;
    [[nodiscard]] const cubey::vulkan::Sampler& composite_sampler() const;
    [[nodiscard]] const Texture2D& history_cloud_texture(FrameSlot frame_slot,
                                                         std::uint32_t ping_pong) const;
    [[nodiscard]] const Texture2D& history_metadata_texture(FrameSlot frame_slot,
                                                            std::uint32_t ping_pong) const;
    [[nodiscard]] bool history_texture_valid(FrameSlot frame_slot,
                                             std::uint32_t ping_pong) const;
    [[nodiscard]] bool history_extent_matches(VkExtent2D extent) const noexcept;
    [[nodiscard]] CloudLayerTemporalUniforms temporal_frame_uniforms(
        FrameSlot frame_slot, CloudLayerFrameUniforms current,
        std::uint32_t history_read_index) const;

    void create_history_textures(const cubey::vulkan::Device& device, VkExtent2D extent,
                                 std::uint32_t frame_slot_count);
    void record_march_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                               VkDescriptorSet descriptor_set, VkExtent2D extent) const;
    void record_temporal_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                  VkDescriptorSet descriptor_set, VkExtent2D extent) const;
    void record_composite_draw(const cubey::vulkan::CommandRecorder& recorder,
                               const ColorTargetView& target, FrameSlot frame_slot) const;

    std::optional<CloudLayerGeneratedResources> generated_{};
    std::optional<FrameUniformBuffer<CloudLayerFrameUniforms>> frame_uniforms_{};
    std::optional<FrameUniformBuffer<CloudLayerTemporalUniforms>> temporal_uniforms_{};
    std::optional<MaterialInstance> march_material_{};
    std::optional<MaterialInstance> temporal_material_{};
    std::optional<MaterialInstance> composite_material_{};
    std::optional<ComputePipelineResource> march_pipeline_{};
    std::optional<ComputePipelineResource> temporal_pipeline_{};
    std::optional<GraphicsPipelineResource> composite_pipeline_{};
    std::optional<cubey::vulkan::Sampler> composite_sampler_{};
    std::vector<std::array<std::optional<Texture2D>, 2>> history_cloud_textures_{};
    std::vector<std::array<std::optional<Texture2D>, 2>> history_metadata_textures_{};
    std::vector<std::uint32_t> history_read_indices_{};
    std::vector<std::array<bool, 2>> history_texture_valid_{};
    std::vector<std::optional<CloudLayerFrameUniforms>> history_frame_states_{};
    CloudLayerCompositeMode composite_mode_ = CloudLayerCompositeMode::Standalone;
    std::uint32_t temporal_frame_index_ = 0;
};

[[nodiscard]] vulkan::SamplerConfig cloud_layer_repeat_sampler_config(
    std::uint32_t mip_levels = 1);
[[nodiscard]] std::uint32_t cloud_layer_generated_volume_mip_count(std::uint32_t size);
[[nodiscard]] Texture3DConfig cloud_layer_volume_texture_config(std::uint32_t size);
[[nodiscard]] Texture2DConfig cloud_layer_weather_texture_config();
[[nodiscard]] ComputeGeneratedTexture2DConfig
cloud_layer_blue_noise_texture_config(ShaderStageFile shader);
[[nodiscard]] MaterialPassInfo cloud_layer_march_pass_info();
[[nodiscard]] MaterialPassInfo cloud_layer_composite_pass_info(bool external_background = false,
                                                               bool scene_depth = false);
[[nodiscard]] MaterialPassInfo cloud_layer_temporal_pass_info();
[[nodiscard]] RenderGraphTextureState cloud_layer_sampled_texture_state();
[[nodiscard]] RenderGraphTextureDesc cloud_layer_color_texture_desc(std::string label,
                                                                    VkExtent2D extent);
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
