#pragma once

#include "pyro_3d_config.h"
#include "pyro_3d_sources.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/celestial_body_frame.h>
#include <cubey/render/environment_lighting.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph_types.h>
#include <cubey/render/texture.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cubey::projects::fluid::pyro_3d {

inline constexpr VkFormat kPyro3DSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat kPyro3DSceneDepthFormat = VK_FORMAT_D32_SFLOAT;

class Pyro3DGpuResources {
  public:
    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& mesh_gpu,
                                           cubey::ProjectGpuServices& gpu,
                                           const Pyro3DConfig& config,
                                           std::uint32_t frame_slot_count);
    void
    create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
                           const std::optional<cubey::render::AtmosphereBackgroundTextureBindings>&
                               atmosphere_background_textures);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] const cubey::render::Texture3D& density_a() const;
    [[nodiscard]] const cubey::render::Texture3D& density_b() const;
    [[nodiscard]] const cubey::render::Texture3D& velocity_a() const;
    [[nodiscard]] const cubey::render::Texture3D& velocity_b() const;
    [[nodiscard]] const cubey::render::Texture3D& density_prediction() const;
    [[nodiscard]] const cubey::render::Texture3D& velocity_prediction() const;
    [[nodiscard]] const cubey::render::Texture3D& divergence() const;
    [[nodiscard]] const cubey::render::Texture3D& pressure_a() const;
    [[nodiscard]] const cubey::render::Texture3D& pressure_b() const;
    [[nodiscard]] const cubey::render::Texture3D& shadow_volume() const;
    [[nodiscard]] const cubey::vulkan::Buffer& sources() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& reset_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& advect_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& advect_correct_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& combustion_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& divergence_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& pressure_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& projection_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& shadow_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& render_pipeline() const;

    [[nodiscard]] VkDescriptorSet reset_descriptor_set() const noexcept {
        return reset_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet advect_descriptor_set(bool density_a_current,
                                                        bool velocity_a_current) const;
    [[nodiscard]] VkDescriptorSet advect_correct_descriptor_set(bool density_a_current,
                                                                bool velocity_a_current) const;
    [[nodiscard]] VkDescriptorSet combustion_descriptor_set(bool density_a_current,
                                                            bool velocity_a_current) const;
    [[nodiscard]] VkDescriptorSet divergence_descriptor_set(bool density_a_current,
                                                            bool velocity_a_current) const;
    [[nodiscard]] VkDescriptorSet pressure_a_to_b_descriptor_set() const noexcept {
        return pressure_a_to_b_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet pressure_b_to_a_descriptor_set() const noexcept {
        return pressure_b_to_a_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet projection_descriptor_set(bool velocity_a_current,
                                                            bool pressure_a_current) const;
    [[nodiscard]] VkDescriptorSet shadow_descriptor_set(bool density_a_current) const;
    [[nodiscard]] VkDescriptorSet render_descriptor_set(bool density_a_current,
                                                        bool velocity_a_current) const;
    [[nodiscard]] VkDescriptorSet environment_descriptor_set(std::uint32_t frame_slot_index) const;
    [[nodiscard]] VkDescriptorSet scene_descriptor_set(std::uint32_t frame_slot_index) const;
    void update_scene_descriptors(const cubey::vulkan::Device& device,
                                  std::uint32_t frame_slot_index,
                                  cubey::render::RenderGraphSampledTextureView scene_color,
                                  cubey::render::RenderGraphSampledTextureView scene_depth) const;
    void
    upload_environment_lighting(std::uint32_t frame_slot_index,
                                const cubey::render::EnvironmentLightingUniforms& uniforms) const;
    void upload_atmosphere_background(
        std::uint32_t frame_slot_index,
        const cubey::render::AtmosphereEnvironmentFrameUniforms& uniforms) const;
    void upload_moon_body(std::uint32_t frame_slot_index,
                          const cubey::render::CelestialBodyFrameUniforms& uniforms) const;
    [[nodiscard]] const cubey::render::AtmosphereBackgroundFrame& atmosphere_background() const;
    [[nodiscard]] const cubey::render::CelestialBodyFrame& moon_body_frame() const;
    [[nodiscard]] const cubey::render::Mesh& moon_mesh() const;
    [[nodiscard]] std::uint32_t frame_slot_count() const noexcept {
        return frame_slot_count_;
    }
    [[nodiscard]] VkDescriptorSet
    atmosphere_background_descriptor_set(std::uint32_t frame_slot_index) const;
    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* profiler() noexcept {
        return profiler_.has_value() ? &profiler_.value() : nullptr;
    }
    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_timings() const;

  private:
    void create_volume_resources(cubey::vulkan::Device& device, cubey::ProjectGpuServices& gpu,
                                 const Pyro3DConfig& config);
    void create_moon_mesh_if_needed(cubey::vulkan::GpuRuntime& gpu);
    void create_descriptor_resources(cubey::vulkan::Device& device);
    void update_descriptors(cubey::vulkan::Device& device);
    void create_compute_pipelines(cubey::vulkan::Device& device);
    [[nodiscard]] VkDescriptorSetLayout reset_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& reset_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout advect_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& advect_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout advect_correct_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& advect_correct_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout combustion_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& combustion_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout divergence_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& divergence_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout pressure_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& pressure_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout projection_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& projection_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout shadow_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& shadow_descriptor_pool() const;
    [[nodiscard]] const cubey::vulkan::DescriptorSetArray& render_descriptors() const;
    [[nodiscard]] VkDescriptorSetLayout environment_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorSetArray& environment_descriptors() const;
    [[nodiscard]] const cubey::vulkan::DescriptorSetArray& scene_descriptors() const;
    [[nodiscard]] const cubey::vulkan::Buffer&
    environment_lighting_uniform_buffer(std::uint32_t frame_slot_index) const;

    std::optional<cubey::render::Texture3D> density_a_;
    std::optional<cubey::render::Texture3D> density_b_;
    std::optional<cubey::render::Texture3D> velocity_a_;
    std::optional<cubey::render::Texture3D> velocity_b_;
    std::optional<cubey::render::Texture3D> density_prediction_;
    std::optional<cubey::render::Texture3D> velocity_prediction_;
    std::optional<cubey::render::Texture3D> divergence_;
    std::optional<cubey::render::Texture3D> pressure_a_;
    std::optional<cubey::render::Texture3D> pressure_b_;
    std::optional<cubey::render::Texture3D> shadow_volume_;
    std::optional<cubey::vulkan::Buffer> sources_;
    std::optional<cubey::vulkan::DescriptorSetLayout> reset_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> reset_descriptor_pool_;
    VkDescriptorSet reset_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> advect_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> advect_descriptor_pool_;
    std::array<VkDescriptorSet, 4> advect_descriptor_sets_{};
    std::optional<cubey::vulkan::DescriptorSetLayout> advect_correct_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> advect_correct_descriptor_pool_;
    std::array<VkDescriptorSet, 4> advect_correct_descriptor_sets_{};
    std::optional<cubey::vulkan::DescriptorSetLayout> combustion_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> combustion_descriptor_pool_;
    std::array<VkDescriptorSet, 4> combustion_descriptor_sets_{};
    std::optional<cubey::vulkan::DescriptorSetLayout> divergence_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> divergence_descriptor_pool_;
    std::array<VkDescriptorSet, 4> divergence_descriptor_sets_{};
    std::optional<cubey::vulkan::DescriptorSetLayout> pressure_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> pressure_descriptor_pool_;
    VkDescriptorSet pressure_a_to_b_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet pressure_b_to_a_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> projection_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> projection_descriptor_pool_;
    std::array<VkDescriptorSet, 4> projection_descriptor_sets_{};
    std::optional<cubey::vulkan::DescriptorSetLayout> shadow_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> shadow_descriptor_pool_;
    std::array<VkDescriptorSet, 2> shadow_descriptor_sets_{};
    std::optional<cubey::vulkan::DescriptorSetArray> render_descriptors_;
    std::optional<cubey::render::FrameUniformBuffer<cubey::render::EnvironmentLightingUniforms>>
        environment_lighting_uniforms_;
    std::optional<cubey::vulkan::DescriptorSetArray> environment_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetArray> scene_descriptors_;
    std::optional<cubey::vulkan::Sampler> scene_sampler_;
    std::optional<cubey::vulkan::Sampler> scene_depth_sampler_;
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_;
    cubey::render::CelestialBodyFrame moon_body_frame_;
    std::optional<cubey::render::Mesh> moon_mesh_;
    std::uint32_t frame_slot_count_ = 0;
    std::optional<cubey::render::ComputePipelineResource> reset_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> advect_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> advect_correct_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> combustion_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> divergence_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> pressure_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> projection_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> shadow_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> render_pipeline_;
    std::optional<cubey::vulkan::GpuTimestampProfiler> profiler_;
};

} // namespace cubey::projects::fluid::pyro_3d
