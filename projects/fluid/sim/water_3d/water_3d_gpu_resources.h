#pragma once

#include "water_3d_config.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph_types.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>

namespace cubey::projects::fluid::water_3d {

class Water3DGpuResources {
  public:
    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::ProjectGpuServices& gpu,
                                           const Water3DConfig& config,
                                           std::uint32_t frame_slot_count);
    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] VkDeviceSize allocated_buffer_bytes() const;

    [[nodiscard]] const cubey::vulkan::Buffer& particle_positions() const;
    [[nodiscard]] const cubey::vulkan::Buffer& particle_velocities() const;
    [[nodiscard]] const cubey::vulkan::Buffer& particle_affine() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u_previous() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v_previous() const;
    [[nodiscard]] const cubey::vulkan::Buffer& w() const;
    [[nodiscard]] const cubey::vulkan::Buffer& w_previous() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u_weight() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v_weight() const;
    [[nodiscard]] const cubey::vulkan::Buffer& w_weight() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u_previous_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v_previous_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& w_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& w_previous_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& u_weight_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& v_weight_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& w_weight_scratch() const;
    [[nodiscard]] const cubey::vulkan::Buffer& pressure_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& pressure_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& divergence() const;
    [[nodiscard]] const cubey::vulkan::Buffer& solid() const;
    [[nodiscard]] const cubey::vulkan::Buffer& cell_counts() const;
    [[nodiscard]] const cubey::vulkan::Buffer& cell_particle_indices() const;
    [[nodiscard]] const cubey::vulkan::Buffer&
    simulation_uniform_buffer(cubey::render::FrameSlot frame_slot) const;
    void upload_simulation_uniforms(cubey::render::FrameSlot frame_slot,
                                    const Water3DSimulationUniforms& uniforms) const;
    [[nodiscard]] VkDescriptorSet field_descriptor_set(cubey::render::FrameSlot frame_slot) const;

    [[nodiscard]] const cubey::render::ComputePipelineResource& reset_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    clear_grid_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    clear_bins_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    build_bins_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    particle_to_grid_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& force_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    divergence_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& pressure_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    projection_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    extrapolate_velocity_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    grid_to_particle_pipeline_resource() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    advect_particles_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& render_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource&
    surface_depth_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource&
    surface_thickness_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource&
    surface_pack_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource&
    surface_smooth_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource&
    surface_composite_pipeline_resource() const;
    [[nodiscard]] VkDescriptorSet
    surface_thickness_descriptor_set(cubey::render::FrameSlot frame_slot) const;
    [[nodiscard]] VkDescriptorSet
    surface_pack_descriptor_set(cubey::render::FrameSlot frame_slot) const;
    [[nodiscard]] VkDescriptorSet
    surface_smooth_x_descriptor_set(cubey::render::FrameSlot frame_slot) const;
    [[nodiscard]] VkDescriptorSet
    surface_smooth_y_descriptor_set(cubey::render::FrameSlot frame_slot) const;
    [[nodiscard]] VkDescriptorSet
    surface_composite_descriptor_set(cubey::render::FrameSlot frame_slot) const;
    void update_surface_descriptors(const cubey::vulkan::Device& device,
                                    cubey::render::FrameSlot frame_slot,
                                    cubey::render::RenderGraphSampledTextureView raw_depth,
                                    cubey::render::RenderGraphSampledTextureView raw_thickness,
                                    cubey::render::RenderGraphSampledTextureView packed_a,
                                    cubey::render::RenderGraphSampledTextureView packed_b);
    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const;

  private:
    void create_field_buffers(cubey::ProjectGpuServices& gpu, const Water3DConfig& config);
    void create_descriptor_resources(cubey::vulkan::Device& device);
    void update_field_descriptors(cubey::vulkan::Device& device);
    void create_compute_pipelines(cubey::vulkan::Device& device);
    [[nodiscard]] VkDescriptorSetLayout field_descriptor_layout() const;
    [[nodiscard]] const cubey::vulkan::DescriptorPool& field_descriptor_pool() const;

    std::optional<cubey::vulkan::Buffer> particle_positions_;
    std::optional<cubey::vulkan::Buffer> particle_velocities_;
    std::optional<cubey::vulkan::Buffer> particle_affine_;
    std::optional<cubey::vulkan::Buffer> u_;
    std::optional<cubey::vulkan::Buffer> u_previous_;
    std::optional<cubey::vulkan::Buffer> v_;
    std::optional<cubey::vulkan::Buffer> v_previous_;
    std::optional<cubey::vulkan::Buffer> w_;
    std::optional<cubey::vulkan::Buffer> w_previous_;
    std::optional<cubey::vulkan::Buffer> u_weight_;
    std::optional<cubey::vulkan::Buffer> v_weight_;
    std::optional<cubey::vulkan::Buffer> w_weight_;
    std::optional<cubey::vulkan::Buffer> u_scratch_;
    std::optional<cubey::vulkan::Buffer> u_previous_scratch_;
    std::optional<cubey::vulkan::Buffer> v_scratch_;
    std::optional<cubey::vulkan::Buffer> v_previous_scratch_;
    std::optional<cubey::vulkan::Buffer> w_scratch_;
    std::optional<cubey::vulkan::Buffer> w_previous_scratch_;
    std::optional<cubey::vulkan::Buffer> u_weight_scratch_;
    std::optional<cubey::vulkan::Buffer> v_weight_scratch_;
    std::optional<cubey::vulkan::Buffer> w_weight_scratch_;
    std::optional<cubey::vulkan::Buffer> pressure_a_;
    std::optional<cubey::vulkan::Buffer> pressure_b_;
    std::optional<cubey::vulkan::Buffer> divergence_;
    std::optional<cubey::vulkan::Buffer> solid_;
    std::optional<cubey::vulkan::Buffer> cell_counts_;
    std::optional<cubey::vulkan::Buffer> cell_particle_indices_;
    std::optional<cubey::render::FrameUniformBuffer<Water3DSimulationUniforms>>
        simulation_uniforms_;
    std::optional<cubey::vulkan::DescriptorSetLayout> field_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> field_descriptor_pool_;
    std::vector<VkDescriptorSet> field_descriptor_sets_;
    std::uint32_t frame_slot_count_ = 0;
    std::optional<cubey::render::ComputePipelineResource> reset_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> clear_grid_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> clear_bins_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> build_bins_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> particle_to_grid_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> force_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> divergence_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> pressure_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> projection_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> extrapolate_velocity_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> grid_to_particle_pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> advect_particles_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> render_pipeline_resource_;
    std::optional<cubey::vulkan::Sampler> surface_sampler_;
    std::optional<cubey::render::MaterialInstance> surface_thickness_material_;
    std::optional<cubey::render::MaterialInstance> surface_pack_material_;
    std::optional<cubey::render::MaterialInstance> surface_smooth_x_material_;
    std::optional<cubey::render::MaterialInstance> surface_smooth_y_material_;
    std::optional<cubey::render::MaterialInstance> surface_composite_material_;
    std::optional<cubey::render::GraphicsPipelineResource> surface_depth_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> surface_thickness_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> surface_pack_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> surface_smooth_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> surface_composite_pipeline_resource_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
};

} // namespace cubey::projects::fluid::water_3d
