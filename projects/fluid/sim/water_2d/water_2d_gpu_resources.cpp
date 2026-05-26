#include "water_2d_gpu_resources.h"

#include <cubey/render/material.h>

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_WATER_2D_SHADER_DIR
#error "CUBEY_WATER_2D_SHADER_DIR must be defined by the water_2d CMake target"
#endif

namespace cubey::projects::fluid::water_2d {
namespace {

static_assert(kWater2DComputeGroupSize == 8U);
inline constexpr VkDeviceSize kWater2DSimulationPushConstantBytes =
    sizeof(float) * kWater2DSimulationPushConstantFloatCount;
inline constexpr VkDeviceSize kWater2DRenderPushConstantBytes =
    sizeof(float) * kWater2DRenderPushConstantFloatCount;
inline constexpr std::uint32_t kWater2DGpuProfilerPassCapacity = 32;
inline constexpr VkFormat kWater2DSurfaceScalarFormat = VK_FORMAT_R32_SFLOAT;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_WATER_2D_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::vulkan::Buffer
upload_project_device_buffer(cubey::ProjectGpuServices& gpu, const void* data,
                             VkDeviceSize byte_size, VkBufferUsageFlags usage, std::string label) {
    std::optional<cubey::vulkan::Buffer> uploaded;
    static_cast<void>(gpu.submit_and_wait({
        .label = std::move(label),
        .work =
            [&uploaded, data, byte_size, usage](cubey::vulkan::GpuOwnerContext& owner) {
                uploaded.emplace(
                    cubey::vulkan::upload_device_buffer(owner, data, byte_size, usage));
            },
    }));
    return std::move(uploaded.value());
}

[[nodiscard]] VkPushConstantRange simulation_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = kWater2DSimulationPushConstantBytes,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_render_pass_info() {
    const VkPushConstantRange render_push_constant{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = kWater2DRenderPushConstantBytes,
    };
    return cubey::render::MaterialPassInfo{
        .label = "water_2d.render",
        .push_constants = {render_push_constant},
    };
}

[[nodiscard]] VkPushConstantRange surface_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = kWater2DRenderPushConstantBytes,
    };
}

[[nodiscard]] cubey::render::MaterialDescriptorSetLayout
sampled_texture_descriptor_set(std::uint32_t set) {
    return {
        .set = set,
        .bindings = {cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        }},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_density_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_2d.surface.density",
        .push_constants = {surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_smooth_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_2d.surface.smooth",
        .descriptor_sets = {sampled_texture_descriptor_set(0)},
        .push_constants = {surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_composite_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_2d.surface.composite",
        .descriptor_sets = {sampled_texture_descriptor_set(0)},
        .push_constants = {surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

void create_compute_pipeline_resource(
    cubey::vulkan::Device& device, const char* filename, VkDescriptorSetLayout descriptor_layout,
    std::optional<cubey::render::ComputePipelineResource>& destination) {
    const VkPushConstantRange compute_push_constant = simulation_push_constant_range();
    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptor_layout};
    const std::array<VkPushConstantRange, 1> push_constants{compute_push_constant};
    destination.emplace(device, cubey::render::ComputePipelineResourceConfig{
                                    .shader_stage =
                                        {
                                            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                            .path = shader_path(filename),
                                        },
                                    .descriptor_set_layouts = set_layouts,
                                    .push_constants = push_constants,
                                });
}

void create_graphics_pipeline_resource(
    cubey::vulkan::Device& device, VkExtent2D extent, VkFormat color_format,
    std::span<const cubey::render::ShaderStageFile> shader_stage_files,
    std::span<const VkDescriptorSetLayout> set_layouts,
    const cubey::render::MaterialPassInfo& material_pass,
    std::optional<cubey::render::GraphicsPipelineResource>& destination) {
    destination.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                    .extent = extent,
                                    .color_format = color_format,
                                    .shader_stage_files = shader_stage_files,
                                    .descriptor_set_layouts = set_layouts,
                                    .material_pass = material_pass,
                                });
}

template <typename Value>
[[nodiscard]] const Value& require_initialized(const std::optional<Value>& value,
                                               const char* message) {
    if (!value.has_value()) {
        throw std::runtime_error(message);
    }
    return value.value();
}

[[nodiscard]] VkDeviceSize
optional_buffer_size(const std::optional<cubey::vulkan::Buffer>& buffer) {
    if (!buffer.has_value()) {
        return 0;
    }
    return buffer->size();
}

template <typename Value>
[[nodiscard]] VkDeviceSize optional_frame_uniform_buffer_size(
    const std::optional<cubey::render::FrameUniformBuffer<Value>>& buffer) {
    if (!buffer.has_value()) {
        return 0;
    }
    return static_cast<VkDeviceSize>(buffer->slot_count()) * buffer->range();
}

} // namespace

void Water2DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                            cubey::ProjectGpuServices& gpu,
                                                            const Water2DConfig& config,
                                                            std::uint32_t frame_slot_count) {
    if (particle_positions_.has_value()) {
        return;
    }
    if (frame_slot_count == 0) {
        throw std::runtime_error("water 2D resources require at least one frame slot");
    }
    frame_slot_count_ = frame_slot_count;

    create_field_buffers(gpu, config);
    profiler_.emplace(device, frame_slot_count_, kWater2DGpuProfilerPassCapacity);
    simulation_uniforms_.emplace(device, frame_slot_count_);
    create_descriptor_resources(device);
    create_compute_pipelines(device);
}

void Water2DGpuResources::destroy_swapchain_resources() {
    surface_composite_pipeline_resource_.reset();
    surface_smooth_pipeline_resource_.reset();
    surface_density_pipeline_resource_.reset();
    surface_composite_material_.reset();
    surface_source_b_material_.reset();
    surface_source_a_material_.reset();
    surface_source_raw_material_.reset();
    surface_sampler_.reset();
    render_pipeline_resource_.reset();
}

void Water2DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    advect_particles_pipeline_resource_.reset();
    diagnostics_pipeline_resource_.reset();
    grid_to_particle_pipeline_resource_.reset();
    projection_pipeline_resource_.reset();
    pressure_pipeline_resource_.reset();
    divergence_pipeline_resource_.reset();
    force_pipeline_resource_.reset();
    particle_to_grid_pipeline_resource_.reset();
    emit_pipeline_resource_.reset();
    build_bins_pipeline_resource_.reset();
    clear_bins_pipeline_resource_.reset();
    clear_grid_pipeline_resource_.reset();
    reset_pipeline_resource_.reset();
    field_descriptor_pool_.reset();
    field_descriptor_layout_.reset();
    field_descriptor_sets_.clear();
    simulation_uniforms_.reset();
    profiler_.reset();
    frame_slot_count_ = 0;
    diagnostics_.reset();
    cell_particle_indices_.reset();
    cell_counts_.reset();
    solid_.reset();
    divergence_.reset();
    pressure_b_.reset();
    pressure_a_.reset();
    v_weight_.reset();
    u_weight_.reset();
    v_previous_.reset();
    v_.reset();
    u_previous_.reset();
    u_.reset();
    particle_affine_.reset();
    particle_velocities_.reset();
    particle_positions_.reset();
}

VkDeviceSize Water2DGpuResources::allocated_buffer_bytes() const {
    return optional_buffer_size(particle_positions_) + optional_buffer_size(particle_velocities_) +
           optional_buffer_size(particle_affine_) + optional_buffer_size(u_) +
           optional_buffer_size(u_previous_) + optional_buffer_size(v_) +
           optional_buffer_size(v_previous_) + optional_buffer_size(u_weight_) +
           optional_buffer_size(v_weight_) + optional_buffer_size(pressure_a_) +
           optional_buffer_size(pressure_b_) + optional_buffer_size(divergence_) +
           optional_buffer_size(solid_) + optional_buffer_size(cell_counts_) +
           optional_buffer_size(cell_particle_indices_) + optional_buffer_size(diagnostics_) +
           optional_frame_uniform_buffer_size(simulation_uniforms_);
}

void Water2DGpuResources::create_field_buffers(cubey::ProjectGpuServices& gpu,
                                               const Water2DConfig& config) {
    const std::vector<float> particle_initial(particle_value_count(config), 0.0F);
    const std::vector<float> cell_initial(cell_count(config), 0.0F);
    const std::vector<float> u_initial(u_face_count(config), 0.0F);
    const std::vector<float> v_initial(v_face_count(config), 0.0F);
    const std::vector<std::uint32_t> cell_count_initial(cell_count(config), 0U);
    const std::vector<std::uint32_t> bin_initial(particle_bin_index_count(config), 0U);
    const std::vector<std::uint32_t> diagnostics_initial(kWater2DDiagnosticSlotCount, 0U);
    const VkDeviceSize particle_byte_size =
        static_cast<VkDeviceSize>(particle_buffer_byte_size(config));
    const VkDeviceSize cell_byte_size = static_cast<VkDeviceSize>(scalar_field_byte_size(config));
    const VkDeviceSize u_byte_size = static_cast<VkDeviceSize>(u_face_byte_size(config));
    const VkDeviceSize v_byte_size = static_cast<VkDeviceSize>(v_face_byte_size(config));
    const VkDeviceSize cell_uint_byte_size =
        static_cast<VkDeviceSize>(cell_uint_field_byte_size(config));
    const VkDeviceSize bin_byte_size =
        static_cast<VkDeviceSize>(particle_bin_index_byte_size(config));
    const VkDeviceSize diagnostics_byte_size =
        static_cast<VkDeviceSize>(diagnostics_buffer_byte_size(config));

    particle_positions_.emplace(upload_project_device_buffer(
        gpu, particle_initial.data(), particle_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_2d particle position upload"));
    particle_velocities_.emplace(upload_project_device_buffer(
        gpu, particle_initial.data(), particle_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_2d particle velocity upload"));
    particle_affine_.emplace(upload_project_device_buffer(
        gpu, particle_initial.data(), particle_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_2d particle affine upload"));
    u_.emplace(upload_project_device_buffer(gpu, u_initial.data(), u_byte_size,
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                            "water_2d U upload"));
    u_previous_.emplace(upload_project_device_buffer(gpu, u_initial.data(), u_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "water_2d previous U upload"));
    v_.emplace(upload_project_device_buffer(gpu, v_initial.data(), v_byte_size,
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                            "water_2d V upload"));
    v_previous_.emplace(upload_project_device_buffer(gpu, v_initial.data(), v_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "water_2d previous V upload"));
    u_weight_.emplace(upload_project_device_buffer(gpu, u_initial.data(), u_byte_size,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                   "water_2d U weight upload"));
    v_weight_.emplace(upload_project_device_buffer(gpu, v_initial.data(), v_byte_size,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                   "water_2d V weight upload"));
    pressure_a_.emplace(upload_project_device_buffer(gpu, cell_initial.data(), cell_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "water_2d pressure A upload"));
    pressure_b_.emplace(upload_project_device_buffer(gpu, cell_initial.data(), cell_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "water_2d pressure B upload"));
    divergence_.emplace(upload_project_device_buffer(gpu, cell_initial.data(), cell_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "water_2d divergence upload"));
    solid_.emplace(upload_project_device_buffer(gpu, cell_initial.data(), cell_byte_size,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                "water_2d solid upload"));
    cell_counts_.emplace(upload_project_device_buffer(
        gpu, cell_count_initial.data(), cell_uint_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_2d cell count upload"));
    cell_particle_indices_.emplace(upload_project_device_buffer(
        gpu, bin_initial.data(), bin_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_2d cell particle index upload"));
    diagnostics_.emplace(upload_project_device_buffer(
        gpu, diagnostics_initial.data(), diagnostics_byte_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        "water_2d diagnostics upload"));
}

void Water2DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    constexpr VkShaderStageFlags kStages =
        VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 17> field_bindings{{
        {.binding = 0, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 1, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 2, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 3, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 4, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 5, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 6, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 7, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 8, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 9, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 10, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 11, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 12, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 13, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 14,
         .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 15,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 16,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo field_info(field_bindings, frame_slot_count_);
    field_descriptor_layout_.emplace(device, field_info.layout_info());
    field_descriptor_pool_.emplace(device, field_info.pool_info());
    field_descriptor_sets_ =
        field_descriptor_pool().allocate_many(field_descriptor_layout(), frame_slot_count_);

    update_field_descriptors(device);
}

void Water2DGpuResources::update_field_descriptors(cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;
    for (std::uint32_t slot = 0; slot < frame_slot_count_; ++slot) {
        const cubey::render::FrameSlot frame_slot{.index = slot, .count = frame_slot_count_};
        const VkDescriptorSet set = field_descriptor_sets_.at(slot);
        descriptor_writes
            .storage_buffer(set, 0, particle_positions().handle(), particle_positions().size())
            .storage_buffer(set, 1, particle_velocities().handle(), particle_velocities().size())
            .storage_buffer(set, 15, particle_affine().handle(), particle_affine().size())
            .storage_buffer(set, 2, u().handle(), u().size())
            .storage_buffer(set, 3, u_previous().handle(), u_previous().size())
            .storage_buffer(set, 4, v().handle(), v().size())
            .storage_buffer(set, 5, v_previous().handle(), v_previous().size())
            .storage_buffer(set, 6, u_weight().handle(), u_weight().size())
            .storage_buffer(set, 7, v_weight().handle(), v_weight().size())
            .storage_buffer(set, 8, pressure_a().handle(), pressure_a().size())
            .storage_buffer(set, 9, pressure_b().handle(), pressure_b().size())
            .storage_buffer(set, 10, divergence().handle(), divergence().size())
            .storage_buffer(set, 11, solid().handle(), solid().size())
            .storage_buffer(set, 12, cell_counts().handle(), cell_counts().size())
            .storage_buffer(set, 13, cell_particle_indices().handle(),
                            cell_particle_indices().size())
            .storage_buffer(set, 16, diagnostics().handle(), diagnostics().size())
            .uniform_buffer(set, 14, simulation_uniform_buffer(frame_slot).handle(),
                            simulation_uniform_buffer(frame_slot).size());
    }
    descriptor_writes.update(device);
}

void Water2DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    create_compute_pipeline_resource(device, "water_2d_reset.comp.spv", field_descriptor_layout(),
                                     reset_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_clear_grid.comp.spv",
                                     field_descriptor_layout(), clear_grid_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_clear_bins.comp.spv",
                                     field_descriptor_layout(), clear_bins_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_build_bins.comp.spv",
                                     field_descriptor_layout(), build_bins_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_emit_particles.comp.spv",
                                     field_descriptor_layout(), emit_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_particle_to_grid.comp.spv",
                                     field_descriptor_layout(),
                                     particle_to_grid_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_force.comp.spv", field_descriptor_layout(),
                                     force_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_divergence.comp.spv",
                                     field_descriptor_layout(), divergence_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_pressure.comp.spv",
                                     field_descriptor_layout(), pressure_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_projection.comp.spv",
                                     field_descriptor_layout(), projection_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_grid_to_particle.comp.spv",
                                     field_descriptor_layout(),
                                     grid_to_particle_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_advect_particles.comp.spv",
                                     field_descriptor_layout(),
                                     advect_particles_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_diagnostics.comp.spv",
                                     field_descriptor_layout(), diagnostics_pipeline_resource_);
}

void Water2DGpuResources::create_render_pipeline(cubey::vulkan::Device& device,
                                                 VkFormat color_format, VkExtent2D extent) {
    surface_sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                         .min_filter = VK_FILTER_LINEAR,
                                         .mag_filter = VK_FILTER_LINEAR,
                                         .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                     });

    const cubey::render::MaterialPassInfo debug_material_pass = water_render_pass_info();
    const cubey::render::MaterialPassInfo surface_density_material_pass =
        water_surface_density_pass_info();
    const cubey::render::MaterialPassInfo surface_smooth_material_pass =
        water_surface_smooth_pass_info();
    const cubey::render::MaterialPassInfo surface_composite_material_pass =
        water_surface_composite_pass_info();

    surface_source_raw_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                     .material_pass = surface_smooth_material_pass,
                                                     .descriptor_set = 0,
                                                     .set_count = frame_slot_count_,
                                                 });
    surface_source_a_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                   .material_pass = surface_smooth_material_pass,
                                                   .descriptor_set = 0,
                                                   .set_count = frame_slot_count_,
                                               });
    surface_source_b_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                   .material_pass = surface_smooth_material_pass,
                                                   .descriptor_set = 0,
                                                   .set_count = frame_slot_count_,
                                               });
    surface_composite_material_.emplace(device,
                                        cubey::render::MaterialInstanceConfig{
                                            .material_pass = surface_composite_material_pass,
                                            .descriptor_set = 0,
                                            .set_count = frame_slot_count_,
                                        });

    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_2d.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_2d_render.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_density_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_2d_surface_density.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_2d_surface_density.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_smooth_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_2d.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_2d_surface_smooth.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_composite_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_2d.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_2d_surface_composite.frag.spv"),
        },
    };

    const std::array<VkDescriptorSetLayout, 1> set_layouts{field_descriptor_layout()};
    const std::array<VkDescriptorSetLayout, 1> source_set_layouts{
        surface_source_raw_material_->layout(),
    };
    const std::array<VkDescriptorSetLayout, 2> composite_set_layouts{
        surface_composite_material_->layout(),
        field_descriptor_layout(),
    };
    create_graphics_pipeline_resource(device, extent, color_format, shader_stage_files, set_layouts,
                                      debug_material_pass, render_pipeline_resource_);
    create_graphics_pipeline_resource(
        device, extent, kWater2DSurfaceScalarFormat, surface_density_shaders, set_layouts,
        surface_density_material_pass, surface_density_pipeline_resource_);
    create_graphics_pipeline_resource(
        device, extent, kWater2DSurfaceScalarFormat, surface_smooth_shaders, source_set_layouts,
        surface_smooth_material_pass, surface_smooth_pipeline_resource_);
    create_graphics_pipeline_resource(device, extent, color_format, surface_composite_shaders,
                                      composite_set_layouts, surface_composite_material_pass,
                                      surface_composite_pipeline_resource_);
}

const cubey::vulkan::Buffer& Water2DGpuResources::particle_positions() const {
    return require_initialized(particle_positions_, "water particle positions are not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::particle_velocities() const {
    return require_initialized(particle_velocities_,
                               "water particle velocities are not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::particle_affine() const {
    return require_initialized(particle_affine_, "water particle affine state is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::u() const {
    return require_initialized(u_, "water U field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::u_previous() const {
    return require_initialized(u_previous_, "water previous U field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::v() const {
    return require_initialized(v_, "water V field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::v_previous() const {
    return require_initialized(v_previous_, "water previous V field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::u_weight() const {
    return require_initialized(u_weight_, "water U weight field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::v_weight() const {
    return require_initialized(v_weight_, "water V weight field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::pressure_a() const {
    return require_initialized(pressure_a_, "water pressure A field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::pressure_b() const {
    return require_initialized(pressure_b_, "water pressure B field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::divergence() const {
    return require_initialized(divergence_, "water divergence field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::solid() const {
    return require_initialized(solid_, "water solid field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::cell_counts() const {
    return require_initialized(cell_counts_, "water cell counts are not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::cell_particle_indices() const {
    return require_initialized(cell_particle_indices_,
                               "water cell particle indices are not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::diagnostics() const {
    return require_initialized(diagnostics_, "water diagnostics are not initialized");
}

const std::vector<cubey::vulkan::GpuPassTiming>& Water2DGpuResources::latest_timings() const {
    static const std::vector<cubey::vulkan::GpuPassTiming> kEmptyTimings;
    if (!profiler_.has_value()) {
        return kEmptyTimings;
    }
    return profiler_->latest_timings();
}

const cubey::vulkan::Buffer&
Water2DGpuResources::simulation_uniform_buffer(cubey::render::FrameSlot frame_slot) const {
    const cubey::render::FrameUniformBuffer<Water2DSimulationUniforms>& uniforms =
        require_initialized(simulation_uniforms_,
                            "water simulation uniform buffers are not initialized");
    return uniforms.buffer(frame_slot);
}

void Water2DGpuResources::upload_simulation_uniforms(
    cubey::render::FrameSlot frame_slot, const Water2DSimulationUniforms& uniforms) const {
    const cubey::render::FrameUniformBuffer<Water2DSimulationUniforms>& buffers =
        require_initialized(simulation_uniforms_,
                            "water simulation uniform buffers are not initialized");
    buffers.upload(frame_slot, uniforms);
}

VkDescriptorSet
Water2DGpuResources::field_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    cubey::render::validate_frame_slot(frame_slot);
    if (frame_slot.count != frame_slot_count_) {
        throw std::runtime_error("water descriptor frame slot count mismatch");
    }
    if (frame_slot.index >= field_descriptor_sets_.size()) {
        throw std::runtime_error("water descriptor frame slot is out of range");
    }
    return field_descriptor_sets_.at(frame_slot.index);
}

VkDescriptorSetLayout Water2DGpuResources::field_descriptor_layout() const {
    if (!field_descriptor_layout_.has_value()) {
        throw std::runtime_error("water field descriptor layout is not initialized");
    }
    return field_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Water2DGpuResources::field_descriptor_pool() const {
    return require_initialized(field_descriptor_pool_,
                               "water field descriptor pool is not initialized");
}

const cubey::render::ComputePipelineResource& Water2DGpuResources::reset_pipeline_resource() const {
    return require_initialized(reset_pipeline_resource_, "water reset pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::clear_grid_pipeline_resource() const {
    return require_initialized(clear_grid_pipeline_resource_,
                               "water grid clear pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::clear_bins_pipeline_resource() const {
    return require_initialized(clear_bins_pipeline_resource_,
                               "water bin clear pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::build_bins_pipeline_resource() const {
    return require_initialized(build_bins_pipeline_resource_,
                               "water bin build pipeline is not initialized");
}

const cubey::render::ComputePipelineResource& Water2DGpuResources::emit_pipeline_resource() const {
    return require_initialized(emit_pipeline_resource_, "water emit pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::particle_to_grid_pipeline_resource() const {
    return require_initialized(particle_to_grid_pipeline_resource_,
                               "water particle-to-grid pipeline is not initialized");
}

const cubey::render::ComputePipelineResource& Water2DGpuResources::force_pipeline_resource() const {
    return require_initialized(force_pipeline_resource_, "water force pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::divergence_pipeline_resource() const {
    return require_initialized(divergence_pipeline_resource_,
                               "water divergence pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::pressure_pipeline_resource() const {
    return require_initialized(pressure_pipeline_resource_,
                               "water pressure pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::projection_pipeline_resource() const {
    return require_initialized(projection_pipeline_resource_,
                               "water projection pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::grid_to_particle_pipeline_resource() const {
    return require_initialized(grid_to_particle_pipeline_resource_,
                               "water grid-to-particle pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::advect_particles_pipeline_resource() const {
    return require_initialized(advect_particles_pipeline_resource_,
                               "water particle advection pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::diagnostics_pipeline_resource() const {
    return require_initialized(diagnostics_pipeline_resource_,
                               "water diagnostics pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water2DGpuResources::render_pipeline_resource() const {
    return require_initialized(render_pipeline_resource_,
                               "water render pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water2DGpuResources::surface_density_pipeline_resource() const {
    return require_initialized(surface_density_pipeline_resource_,
                               "water surface density pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water2DGpuResources::surface_smooth_pipeline_resource() const {
    return require_initialized(surface_smooth_pipeline_resource_,
                               "water surface smooth pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water2DGpuResources::surface_composite_pipeline_resource() const {
    return require_initialized(surface_composite_pipeline_resource_,
                               "water surface composite pipeline is not initialized");
}

VkDescriptorSet
Water2DGpuResources::surface_source_raw_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_source_raw_material_,
                               "water surface raw source material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water2DGpuResources::surface_source_a_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_source_a_material_,
                               "water surface source A material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water2DGpuResources::surface_source_b_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_source_b_material_,
                               "water surface source B material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water2DGpuResources::surface_composite_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_composite_material_,
                               "water surface composite material is not initialized")
        .set(frame_slot);
}

void Water2DGpuResources::update_surface_descriptors(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    cubey::render::RenderGraphSampledTextureView raw_density,
    cubey::render::RenderGraphSampledTextureView surface_a,
    cubey::render::RenderGraphSampledTextureView surface_b,
    cubey::render::RenderGraphSampledTextureView final_surface) {
    const cubey::vulkan::Sampler& sampler =
        require_initialized(surface_sampler_, "water surface sampler is not initialized");
    const VkSampler sampler_handle = sampler.handle();
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(surface_source_raw_descriptor_set(frame_slot), 0, sampler_handle,
                                raw_density.view, raw_density.layout)
        .combined_image_sampler(surface_source_a_descriptor_set(frame_slot), 0, sampler_handle,
                                surface_a.view, surface_a.layout)
        .combined_image_sampler(surface_source_b_descriptor_set(frame_slot), 0, sampler_handle,
                                surface_b.view, surface_b.layout)
        .combined_image_sampler(surface_composite_descriptor_set(frame_slot), 0, sampler_handle,
                                final_surface.view, final_surface.layout);
    writes.update(device);
}

} // namespace cubey::projects::fluid::water_2d
