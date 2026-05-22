#pragma once

#include "water_2d_config.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <optional>

namespace cubey::projects::fluid::water_2d {

class Water2DGpuResources {
  public:
    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::ProjectGpuServices& gpu,
                                           const Water2DConfig& config);
    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] const cubey::vulkan::Buffer& phi_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& phi_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& pressure_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& pressure_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& divergence() const;
    [[nodiscard]] const cubey::vulkan::Buffer& solid() const;
    [[nodiscard]] VkDescriptorSet field_descriptor_set() const noexcept {
        return field_descriptor_set_;
    }
    [[nodiscard]] const cubey::render::ComputePipelineResource& reset_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& force_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    advect_velocity_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    advect_phi_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    reinitialize_phi_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    divergence_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& pressure_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    projection_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& render_pipeline_resource() const;

  private:
    void create_field_buffers(cubey::ProjectGpuServices& gpu, const Water2DConfig& config);
    void create_descriptor_resources(cubey::vulkan::Device& device);
    void update_field_descriptors(cubey::vulkan::Device& device);
    void create_compute_pipelines(cubey::vulkan::Device& device);
    [[nodiscard]] VkDescriptorSetLayout field_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& field_descriptor_pool() const;

    std::optional<cubey::vulkan::Buffer> phi_a_;
    std::optional<cubey::vulkan::Buffer> phi_b_;
    std::optional<cubey::vulkan::Buffer> u_a_;
    std::optional<cubey::vulkan::Buffer> u_b_;
    std::optional<cubey::vulkan::Buffer> v_a_;
    std::optional<cubey::vulkan::Buffer> v_b_;
    std::optional<cubey::vulkan::Buffer> pressure_a_;
    std::optional<cubey::vulkan::Buffer> pressure_b_;
    std::optional<cubey::vulkan::Buffer> divergence_;
    std::optional<cubey::vulkan::Buffer> solid_;
    std::optional<cubey::vulkan::DescriptorSetLayout> field_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> field_descriptor_pool_;
    VkDescriptorSet field_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::render::ComputePipelineResource> reset_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> force_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> advect_velocity_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> advect_phi_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> reinitialize_phi_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> divergence_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> pressure_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> projection_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> render_pipeline_resource_;
};

} // namespace cubey::projects::fluid::water_2d
