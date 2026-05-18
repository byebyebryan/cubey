#pragma once

#include "fluid_2d_config.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>

namespace cubey::projects::fluid_2d {

class Fluid2DGpuResources {
  public:
    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::ProjectGpuServices& gpu,
                                           const Fluid2DConfig& config);
    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] const cubey::vulkan::Buffer& field_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& field_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& field_temp() const;
    [[nodiscard]] const cubey::vulkan::Buffer& divergence() const;
    [[nodiscard]] const cubey::vulkan::Buffer& pressure_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& pressure_b() const;
    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& render_descriptors() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& inject_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& advect_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    advect_correct_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    divergence_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& pressure_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    projection_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& render_pipeline_resource() const;
    [[nodiscard]] VkDescriptorSet inject_descriptor_set() const noexcept {
        return inject_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet advect_descriptor_set() const noexcept {
        return advect_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet advect_correct_descriptor_set() const noexcept {
        return advect_correct_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet divergence_descriptor_set() const noexcept {
        return divergence_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet pressure_a_to_b_descriptor_set() const noexcept {
        return pressure_a_to_b_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet pressure_b_to_a_descriptor_set() const noexcept {
        return pressure_b_to_a_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet projection_pressure_a_descriptor_set() const noexcept {
        return projection_pressure_a_descriptor_set_;
    }
    [[nodiscard]] VkDescriptorSet projection_pressure_b_descriptor_set() const noexcept {
        return projection_pressure_b_descriptor_set_;
    }

  private:
    void create_field_buffers(cubey::ProjectGpuServices& gpu, const Fluid2DConfig& config);
    void create_descriptor_resources(cubey::vulkan::Device& device);
    void update_field_descriptors(cubey::vulkan::Device& device);
    void create_compute_pipelines(cubey::vulkan::Device& device);
    [[nodiscard]] VkDescriptorSetLayout compute_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& compute_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout advect_correct_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& advect_correct_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout divergence_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& divergence_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout pressure_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& pressure_descriptor_pool() const;
    [[nodiscard]] VkDescriptorSetLayout projection_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& projection_descriptor_pool() const;

    Fluid2DConfig config_{};
    std::optional<cubey::vulkan::Buffer> field_a_;
    std::optional<cubey::vulkan::Buffer> field_b_;
    std::optional<cubey::vulkan::Buffer> field_temp_;
    std::optional<cubey::vulkan::Buffer> divergence_;
    std::optional<cubey::vulkan::Buffer> pressure_a_;
    std::optional<cubey::vulkan::Buffer> pressure_b_;
    std::optional<cubey::vulkan::DescriptorSetLayout> compute_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> compute_descriptor_pool_;
    VkDescriptorSet inject_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet advect_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> advect_correct_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> advect_correct_descriptor_pool_;
    VkDescriptorSet advect_correct_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> divergence_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> divergence_descriptor_pool_;
    VkDescriptorSet divergence_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> pressure_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> pressure_descriptor_pool_;
    VkDescriptorSet pressure_a_to_b_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet pressure_b_to_a_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> projection_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> projection_descriptor_pool_;
    VkDescriptorSet projection_pressure_a_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet projection_pressure_b_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_descriptors_;
    std::optional<cubey::render::ComputePipelineResource> inject_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> advect_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> advect_correct_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> divergence_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> pressure_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> projection_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> render_pipeline_resource_;
};

} // namespace cubey::projects::fluid_2d
