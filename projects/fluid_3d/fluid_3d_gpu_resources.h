#pragma once

#include "fluid_3d_config.h"
#include "fluid_3d_sources.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cubey::projects::fluid_3d {

class Fluid3DGpuResources {
  public:
    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::ProjectGpuServices& gpu,
                                           const Fluid3DConfig& config,
                                           std::uint32_t frame_slot_count);
    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent);
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
    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* profiler() noexcept {
        return profiler_.has_value() ? &profiler_.value() : nullptr;
    }
    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_timings() const;

  private:
    void create_volume_resources(cubey::vulkan::Device& device, cubey::ProjectGpuServices& gpu,
                                 const Fluid3DConfig& config);
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

    Fluid3DConfig config_{};
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

} // namespace cubey::projects::fluid_3d
