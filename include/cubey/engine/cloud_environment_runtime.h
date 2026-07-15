#pragma once

#include <cubey/engine/cloud_environment_config.h>
#include <cubey/render/cloud_environment_probe.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <optional>

namespace cubey {

struct CloudEnvironmentSurfaceViewInfo {
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    math::Vec3 camera_right{1.0F, 0.0F, 0.0F};
    math::Vec3 camera_up{0.0F, 1.0F, 0.0F};
    math::Vec3 camera_forward{0.0F, 0.0F, -1.0F};
    float tan_half_fovy = 0.0F;
    VkExtent2D target_extent{};
    float near_plane_m = 1.0F;
    float far_plane_m = 400000.0F;
    bool external_background = true;
    bool scene_depth_occlusion_enabled = false;
    bool scene_depth_foreground_only = false;
    float scene_depth_fade_m = 500.0F;
};

struct CloudEnvironmentRuntimeFrame {
    render::CloudLayerConfig layer{};
    render::CloudLayerFrameInfo view{};
    render::CloudLayerFrameUniforms uniforms{};
    bool enabled = false;
};

[[nodiscard]] CloudEnvironmentRuntimeFrame
cloud_environment_runtime_frame(const CloudEnvironmentConfig& config, double elapsed_seconds,
                                const render::AtmosphereEnvironmentLighting& lighting,
                                const CloudEnvironmentSurfaceViewInfo& view,
                                std::uint32_t temporal_frame_index = 0);

class CloudEnvironmentRuntime {
  public:
    CloudEnvironmentRuntime() = default;

    CloudEnvironmentRuntime(const CloudEnvironmentRuntime&) = delete;
    CloudEnvironmentRuntime& operator=(const CloudEnvironmentRuntime&) = delete;
    CloudEnvironmentRuntime(CloudEnvironmentRuntime&&) = delete;
    CloudEnvironmentRuntime& operator=(CloudEnvironmentRuntime&&) = delete;

    void create_surface_resources(const cubey::vulkan::Device& device,
                                  cubey::vulkan::GpuRuntime& gpu,
                                  const render::CloudLayerGeneratedShaderFiles& shaders,
                                  const CloudEnvironmentConfig& config);
    void create_surface_target_resources(const cubey::vulkan::Device& device,
                                         const render::CloudLayerRuntimeShaderFiles& shaders,
                                         render::CloudLayerCompositeMode composite_mode,
                                         VkFormat color_format, VkExtent2D extent,
                                         std::uint32_t frame_slot_count);
    void destroy_surface_target_resources();
    void destroy_surface_resources();

    void set_config(const CloudEnvironmentConfig& config);
    void set_elapsed_seconds(double elapsed_seconds);
    [[nodiscard]] const CloudEnvironmentConfig& config() const noexcept;
    [[nodiscard]] double elapsed_seconds() const noexcept;
    void update_weather_texture(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                                const render::ShaderStageFile& shader, bool force = false);
    [[nodiscard]] CloudEnvironmentRuntimeFrame
    frame(const CloudEnvironmentSurfaceViewInfo& view,
          const render::AtmosphereEnvironmentLighting& lighting) const;
    void upload_surface_frame(render::FrameSlot frame_slot,
                              const CloudEnvironmentRuntimeFrame& frame) const;
    [[nodiscard]] render::CloudLayerRuntimeFrame
    declare_surface_product(render::RenderGraphBuilder& graph, render::FrameSlot frame_slot,
                            const CloudEnvironmentRuntimeFrame& frame) const;
    void declare_surface_composite(
        render::RenderGraphBuilder& graph, render::RenderGraphTextureHandle target,
        const render::CloudLayerRuntimeFrame& product, render::FrameSlot frame_slot,
        std::optional<render::RenderGraphTextureHandle> background = std::nullopt,
        std::optional<render::RenderGraphTextureHandle> scene_depth = std::nullopt) const;
    [[nodiscard]] render::CloudLayerShadowProduct
    declare_shadow_product(render::RenderGraphBuilder& graph, render::FrameSlot frame_slot,
                           const render::CloudLayerShadowRequest& request) const;
    void update_surface_descriptors(
        const cubey::vulkan::Device& device, render::FrameSlot frame_slot,
        const render::CompiledRenderGraph& graph, const render::RenderGraphResourceSet& resources,
        const render::CloudLayerRuntimeFrame& frame,
        std::optional<render::RenderGraphTextureHandle> background = std::nullopt,
        std::optional<render::RenderGraphTextureHandle> scene_depth = std::nullopt) const;
    void update_shadow_descriptors(const cubey::vulkan::Device& device,
                                   render::FrameSlot frame_slot,
                                   const render::CompiledRenderGraph& graph,
                                   const render::RenderGraphResourceSet& resources,
                                   const render::CloudLayerShadowProduct& product) const;
    void complete_surface_frame(render::FrameSlot frame_slot,
                                const render::CloudLayerRuntimeFrame& frame);

    [[nodiscard]] bool surface_resources_created() const noexcept;
    [[nodiscard]] const render::CloudLayerGeneratedResources& generated_resources() const;
    [[nodiscard]] const cubey::vulkan::Sampler& shadow_sampler() const;

    void create_resources(const cubey::vulkan::Device& device,
                          const render::CloudEnvironmentProbeConfig& config,
                          const render::CloudLayerGeneratedResources& generated,
                          const render::TextureCube& clear_sky);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const render::CloudEnvironmentProbePipelineConfig& config);
    void destroy_probe_resources();
    void destroy();

    void advance(double delta_seconds);
    void invalidate();
    [[nodiscard]] bool record_pending_update(const cubey::vulkan::CommandRecorder& recorder,
                                             const render::CloudEnvironmentProbeUpdateInfo& info);
    [[nodiscard]] bool record_pending_update(const cubey::vulkan::CommandRecorder& recorder,
                                             render::FrameSlot frame_slot,
                                             const CloudEnvironmentRuntimeFrame& frame);

    [[nodiscard]] bool resources_created() const noexcept;
    [[nodiscard]] bool pipelines_created() const noexcept;
    [[nodiscard]] float age_seconds() const noexcept;
    [[nodiscard]] render::CloudEnvironmentProbeSnapshot snapshot() const;
    [[nodiscard]] render::PbrEnvironmentTextureBindings
    pbr_environment_bindings(const render::PbrEnvironmentTextureBindings& fallback) const;

  private:
    CloudEnvironmentConfig config_{};
    double elapsed_seconds_ = 0.0;
    render::CloudLayerRuntime surface_{};
    render::CloudEnvironmentProbe probe_{};
};

} // namespace cubey
