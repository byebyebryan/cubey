#pragma once

#include <cubey/render/cloud_layer_config.h>
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
#include <filesystem>
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
inline constexpr std::uint32_t kCloudLayerShadowTextureSize = 256U;
inline constexpr std::uint32_t kCloudLayerShadowStepCount = 8U;
inline constexpr VkFormat kCloudLayerColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat kCloudLayerShadowFormat = VK_FORMAT_R16_SFLOAT;
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
inline constexpr std::uint32_t kCloudLayerShadowUniformBinding = 0U;
inline constexpr std::uint32_t kCloudLayerShadowOutputBinding = 1U;
inline constexpr std::uint32_t kCloudLayerShadowBaseNoiseBinding = 2U;
inline constexpr std::uint32_t kCloudLayerShadowDetailNoiseBinding = 3U;
inline constexpr std::uint32_t kCloudLayerShadowWeatherBinding = 4U;

struct CloudLayerProduct {
    RenderGraphTextureHandle cloud{};
    RenderGraphTextureHandle metadata{};
    RenderGraphTextureHandle resolved_cloud{};
    RenderGraphTextureHandle resolved_metadata{};
    VkExtent2D extent{};
};

struct CloudLayerShadowRequest {
    math::Vec3 receiver_center{0.0F, 0.0F, 0.0F};
    math::Vec3 receiver_axis_u{1.0F, 0.0F, 0.0F};
    math::Vec3 receiver_axis_v{0.0F, 0.0F, 1.0F};
    float half_extent_m = 16000.0F;
    math::Vec3 direct_light_direction{0.0F, 1.0F, 0.0F};
    float direct_light_intensity = 1.0F;
};

struct CloudLayerShadowProjection {
    math::Vec3 receiver_center{0.0F, 0.0F, 0.0F};
    math::Vec3 receiver_axis_u{1.0F, 0.0F, 0.0F};
    math::Vec3 receiver_axis_v{0.0F, 0.0F, 1.0F};
    math::Vec4 world_to_uv_x{};
    math::Vec4 world_to_uv_y{};
    VkExtent2D extent{};
    float texel_world_size_m = 0.0F;
};

struct CloudLayerShadowProduct {
    RenderGraphTextureHandle transmittance{};
    math::Vec4 world_to_uv_x{};
    math::Vec4 world_to_uv_y{};
    VkExtent2D extent{};
    float texel_world_size_m = 0.0F;
};

struct CloudLayerReflectionContribution {
    RenderGraphTextureHandle radiance_transmittance{};
    VkExtent2D extent{};
};

struct CloudLayerGeneratedShaderFiles {
    ShaderStageFile base_noise{};
    ShaderStageFile detail_noise{};
    ShaderStageFile weather{};
    ShaderStageFile blue_noise{};
};

struct CloudLayerRuntimeShaderFiles {
    CloudLayerGeneratedShaderFiles generated{};
    ShaderStageFile general_march{};
    ShaderStageFile surface_march{};
    ShaderStageFile shadow{};
    ShaderStageFile temporal{};
    ShaderStageFile composite_vertex{};
    ShaderStageFile composite_fragment{};
};

enum class CloudLayerCompositeMode : std::uint32_t {
    Standalone = 0,
    ExternalBackground = 1,
    ExternalBackgroundSceneDepth = 2,
};

[[nodiscard]] CloudLayerRuntimeShaderFiles
cloud_layer_runtime_shader_files(const std::filesystem::path& shader_dir,
                                 CloudLayerCompositeMode composite_mode);

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
    bool surface_march_enabled = false;
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
    void update_weather_texture(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                                const ShaderStageFile& shader, const CloudLayerConfig& config,
                                bool force = false);
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

    void upload_frame_uniforms(FrameSlot frame_slot, const CloudLayerFrameUniforms& uniforms) const;
    [[nodiscard]] CloudLayerRuntimeFrame declare_product(RenderGraphBuilder& graph,
                                                         VkExtent2D target_extent,
                                                         const CloudLayerConfig& config,
                                                         FrameSlot frame_slot,
                                                         CloudLayerFrameUniforms uniforms) const;
    [[nodiscard]] CloudLayerShadowProduct
    declare_shadow_product(RenderGraphBuilder& graph, FrameSlot frame_slot,
                           const CloudLayerShadowRequest& request) const;
    void
    declare_composite(RenderGraphBuilder& graph, RenderGraphTextureHandle target,
                      const CloudLayerRuntimeFrame& frame, FrameSlot frame_slot,
                      std::optional<RenderGraphTextureHandle> background = std::nullopt,
                      std::optional<RenderGraphTextureHandle> scene_depth = std::nullopt) const;
    void
    update_descriptors(const cubey::vulkan::Device& device, FrameSlot frame_slot,
                       const CompiledRenderGraph& graph, const RenderGraphResourceSet& resources,
                       const CloudLayerRuntimeFrame& frame,
                       std::optional<RenderGraphTextureHandle> background = std::nullopt,
                       std::optional<RenderGraphTextureHandle> scene_depth = std::nullopt) const;
    void update_shadow_descriptors(const cubey::vulkan::Device& device, FrameSlot frame_slot,
                                   const CompiledRenderGraph& graph,
                                   const RenderGraphResourceSet& resources,
                                   const CloudLayerShadowProduct& product) const;
    [[nodiscard]] const cubey::vulkan::Sampler& product_sampler() const;
    [[nodiscard]] const cubey::vulkan::Sampler& shadow_sampler() const;
    void invalidate_history();
    void complete_frame(FrameSlot frame_slot, const CloudLayerRuntimeFrame& frame);

  private:
    [[nodiscard]] const FrameUniformBuffer<CloudLayerFrameUniforms>& frame_uniforms() const;
    [[nodiscard]] const FrameUniformBuffer<CloudLayerTemporalUniforms>& temporal_uniforms() const;
    [[nodiscard]] const MaterialInstance& march_material() const;
    [[nodiscard]] const MaterialInstance& shadow_material() const;
    [[nodiscard]] const MaterialInstance& temporal_material() const;
    [[nodiscard]] const MaterialInstance& composite_material() const;
    [[nodiscard]] const ComputePipelineResource& general_march_pipeline() const;
    [[nodiscard]] const ComputePipelineResource& surface_march_pipeline() const;
    [[nodiscard]] const ComputePipelineResource& shadow_pipeline() const;
    [[nodiscard]] const ComputePipelineResource& temporal_pipeline() const;
    [[nodiscard]] const GraphicsPipelineResource& composite_pipeline() const;
    [[nodiscard]] const cubey::vulkan::Sampler& composite_sampler() const;
    [[nodiscard]] const Texture2D& history_cloud_texture(FrameSlot frame_slot,
                                                         std::uint32_t ping_pong) const;
    [[nodiscard]] const Texture2D& history_metadata_texture(FrameSlot frame_slot,
                                                            std::uint32_t ping_pong) const;
    [[nodiscard]] bool history_texture_valid(FrameSlot frame_slot, std::uint32_t ping_pong) const;
    [[nodiscard]] bool history_extent_matches(VkExtent2D extent) const noexcept;
    [[nodiscard]] CloudLayerTemporalUniforms
    temporal_frame_uniforms(FrameSlot frame_slot, CloudLayerFrameUniforms current,
                            std::uint32_t history_read_index) const;

    void create_history_textures(const cubey::vulkan::Device& device, VkExtent2D extent,
                                 std::uint32_t frame_slot_count);
    void record_march_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                               VkDescriptorSet descriptor_set, VkExtent2D extent,
                               bool surface_march_enabled) const;
    void record_shadow_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                VkDescriptorSet descriptor_set,
                                const CloudLayerShadowRequest& request,
                                const CloudLayerShadowProjection& projection) const;
    void record_temporal_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                  VkDescriptorSet descriptor_set, VkExtent2D extent) const;
    void record_composite_draw(const cubey::vulkan::CommandRecorder& recorder,
                               const ColorTargetView& target, FrameSlot frame_slot) const;

    std::optional<CloudLayerGeneratedResources> generated_{};
    std::optional<FrameUniformBuffer<CloudLayerFrameUniforms>> frame_uniforms_{};
    std::optional<FrameUniformBuffer<CloudLayerTemporalUniforms>> temporal_uniforms_{};
    std::optional<MaterialInstance> march_material_{};
    std::optional<MaterialInstance> shadow_material_{};
    std::optional<MaterialInstance> temporal_material_{};
    std::optional<MaterialInstance> composite_material_{};
    std::optional<ComputePipelineResource> general_march_pipeline_{};
    std::optional<ComputePipelineResource> surface_march_pipeline_{};
    std::optional<ComputePipelineResource> shadow_pipeline_{};
    std::optional<ComputePipelineResource> temporal_pipeline_{};
    std::optional<GraphicsPipelineResource> composite_pipeline_{};
    std::optional<cubey::vulkan::Sampler> composite_sampler_{};
    std::optional<cubey::vulkan::Sampler> shadow_sampler_{};
    std::vector<std::array<std::optional<Texture2D>, 2>> history_cloud_textures_{};
    std::vector<std::array<std::optional<Texture2D>, 2>> history_metadata_textures_{};
    std::vector<std::uint32_t> history_read_indices_{};
    std::vector<std::array<bool, 2>> history_texture_valid_{};
    std::vector<std::optional<CloudLayerFrameUniforms>> history_frame_states_{};
    CloudLayerCompositeMode composite_mode_ = CloudLayerCompositeMode::Standalone;
    std::uint32_t temporal_frame_index_ = 0;
};

[[nodiscard]] vulkan::SamplerConfig cloud_layer_repeat_sampler_config(std::uint32_t mip_levels = 1);
[[nodiscard]] std::uint32_t cloud_layer_generated_volume_mip_count(std::uint32_t size);
[[nodiscard]] Texture3DConfig cloud_layer_volume_texture_config(std::uint32_t size);
[[nodiscard]] Texture2DConfig cloud_layer_weather_texture_config();
[[nodiscard]] ComputeGeneratedTexture2DConfig
cloud_layer_blue_noise_texture_config(ShaderStageFile shader);
[[nodiscard]] MaterialPassInfo cloud_layer_march_pass_info();
[[nodiscard]] MaterialPassInfo cloud_layer_shadow_pass_info();
[[nodiscard]] MaterialPassInfo cloud_layer_composite_pass_info(bool external_background = false,
                                                               bool scene_depth = false);
[[nodiscard]] MaterialPassInfo cloud_layer_temporal_pass_info();
[[nodiscard]] RenderGraphTextureState cloud_layer_sampled_texture_state();
[[nodiscard]] RenderGraphTextureDesc cloud_layer_color_texture_desc(std::string label,
                                                                    VkExtent2D extent);
[[nodiscard]] RenderGraphTextureDesc cloud_layer_shadow_texture_desc();
[[nodiscard]] CloudLayerShadowProjection
cloud_layer_shadow_projection(const CloudLayerShadowRequest& request);
[[nodiscard]] CloudLayerGeneratedResources create_cloud_layer_generated_resources(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const CloudLayerGeneratedShaderFiles& shaders, const CloudLayerConfig& config);
void update_cloud_layer_weather_texture(const cubey::vulkan::Device& device,
                                        cubey::vulkan::GpuRuntime& gpu,
                                        const ShaderStageFile& shader, Texture2D& weather_texture,
                                        CloudLayerWeatherPushConstants push_constants);

} // namespace cubey::render
