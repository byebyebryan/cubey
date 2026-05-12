#include "fluid_2d_gpu_resources.h"

#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/shader_bytecode.h>
#include <cubey/vulkan/shader_module.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_FLUID_2D_SHADER_DIR
#error "CUBEY_FLUID_2D_SHADER_DIR must be defined by the fluid_2d CMake target"
#endif

namespace cubey::projects::fluid_2d {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FLUID_2D_SHADER_DIR) / filename;
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
        .size = sizeof(float) * 16U,
    };
}

void create_compute_layout(cubey::vulkan::Device& device, VkDescriptorSetLayout descriptor_layout,
                           std::optional<cubey::vulkan::PipelineLayout>& destination) {
    const VkPushConstantRange compute_push_constant = simulation_push_constant_range();
    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptor_layout};
    const std::array<VkPushConstantRange, 1> push_constants{compute_push_constant};
    const cubey::vulkan::PipelineLayoutInfo layout_info({
        .set_layouts = set_layouts,
        .push_constants = push_constants,
    });
    destination.emplace(device, layout_info.create_info());
}

void create_compute_pipeline_from_shader(
    cubey::vulkan::Device& device, const char* filename,
    const cubey::vulkan::PipelineLayout& pipeline_layout,
    std::optional<cubey::vulkan::ComputePipeline>& destination) {
    const std::vector<std::uint32_t> code = cubey::vulkan::read_spirv_file(shader_path(filename));
    cubey::vulkan::ShaderModule shader(device, code);
    const VkPipelineShaderStageCreateInfo stage =
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, shader.handle());
    const cubey::vulkan::ComputePipelineInfo info({
        .layout = pipeline_layout.handle(),
        .shader_stage = stage,
    });
    destination.emplace(device, info.create_info());
}

} // namespace

void Fluid2DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                            cubey::ProjectGpuServices& gpu,
                                                            const Fluid2DConfig& config) {
    config_ = config;
    if (field_a_.has_value()) {
        return;
    }

    create_field_buffers(gpu, config_);
    create_descriptor_resources(device);
    create_compute_pipelines(device);
}

void Fluid2DGpuResources::destroy_swapchain_resources() {
    render_pipeline_.reset();
    render_pipeline_layout_.reset();
}

void Fluid2DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    projection_pipeline_.reset();
    pressure_pipeline_.reset();
    divergence_pipeline_.reset();
    advect_pipeline_.reset();
    inject_pipeline_.reset();
    projection_pipeline_layout_.reset();
    pressure_pipeline_layout_.reset();
    divergence_pipeline_layout_.reset();
    compute_pipeline_layout_.reset();
    render_descriptors_.reset();
    projection_descriptor_pool_.reset();
    projection_descriptor_layout_.reset();
    pressure_descriptor_pool_.reset();
    pressure_descriptor_layout_.reset();
    divergence_descriptor_pool_.reset();
    divergence_descriptor_layout_.reset();
    compute_descriptor_pool_.reset();
    compute_descriptor_layout_.reset();
    pressure_b_.reset();
    pressure_a_.reset();
    divergence_.reset();
    field_b_.reset();
    field_a_.reset();
}

void Fluid2DGpuResources::create_field_buffers(cubey::ProjectGpuServices& gpu,
                                               const Fluid2DConfig& config) {
    const std::vector<FluidCellGpu> initial(field_cell_count(config));
    const VkDeviceSize byte_size = static_cast<VkDeviceSize>(field_byte_size(config));
    field_a_.emplace(upload_project_device_buffer(gpu, initial.data(), byte_size,
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  "fluid_2d field A upload"));
    field_b_.emplace(upload_project_device_buffer(gpu, initial.data(), byte_size,
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  "fluid_2d field B upload"));

    const std::vector<float> scalar_initial(field_cell_count(config), 0.0F);
    const VkDeviceSize scalar_byte_size = static_cast<VkDeviceSize>(scalar_field_byte_size(config));
    divergence_.emplace(upload_project_device_buffer(gpu, scalar_initial.data(), scalar_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "fluid_2d divergence upload"));
    pressure_a_.emplace(upload_project_device_buffer(gpu, scalar_initial.data(), scalar_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "fluid_2d pressure A upload"));
    pressure_b_.emplace(upload_project_device_buffer(gpu, scalar_initial.data(), scalar_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "fluid_2d pressure B upload"));
}

void Fluid2DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> compute_bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo compute_info(compute_bindings, 2);
    compute_descriptor_layout_.emplace(device, compute_info.layout_info());
    compute_descriptor_pool_.emplace(device, compute_info.pool_info());
    inject_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());
    advect_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> divergence_bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 3,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo divergence_info(divergence_bindings);
    divergence_descriptor_layout_.emplace(device, divergence_info.layout_info());
    divergence_descriptor_pool_.emplace(device, divergence_info.pool_info());
    divergence_descriptor_set_ =
        divergence_descriptor_pool().allocate(divergence_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> pressure_bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo pressure_info(pressure_bindings, 2);
    pressure_descriptor_layout_.emplace(device, pressure_info.layout_info());
    pressure_descriptor_pool_.emplace(device, pressure_info.pool_info());
    pressure_a_to_b_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());
    pressure_b_to_a_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> projection_bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo projection_info(projection_bindings, 2);
    projection_descriptor_layout_.emplace(device, projection_info.layout_info());
    projection_descriptor_pool_.emplace(device, projection_info.pool_info());
    projection_pressure_a_descriptor_set_ =
        projection_descriptor_pool().allocate(projection_descriptor_layout());
    projection_pressure_b_descriptor_set_ =
        projection_descriptor_pool().allocate(projection_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> render_bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 3,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo render_info(render_bindings);
    render_descriptors_.emplace(device, render_info);

    update_field_descriptors(device);
}

void Fluid2DGpuResources::update_field_descriptors(cubey::vulkan::Device& device) {
    const cubey::vulkan::DescriptorBufferWrite inject_source =
        cubey::vulkan::storage_buffer_descriptor(inject_descriptor_set_, 0, field_a().handle(),
                                                 field_a().size());
    const cubey::vulkan::DescriptorBufferWrite inject_destination =
        cubey::vulkan::storage_buffer_descriptor(inject_descriptor_set_, 1, field_b().handle(),
                                                 field_b().size());
    const cubey::vulkan::DescriptorBufferWrite advect_source =
        cubey::vulkan::storage_buffer_descriptor(advect_descriptor_set_, 0, field_b().handle(),
                                                 field_b().size());
    const cubey::vulkan::DescriptorBufferWrite advect_destination =
        cubey::vulkan::storage_buffer_descriptor(advect_descriptor_set_, 1, field_a().handle(),
                                                 field_a().size());
    const cubey::vulkan::DescriptorBufferWrite render_source =
        cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 0, field_a().handle(),
                                                 field_a().size());
    const cubey::vulkan::DescriptorBufferWrite render_divergence =
        cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 1,
                                                 divergence().handle(), divergence().size());
    const cubey::vulkan::DescriptorBufferWrite render_pressure_a =
        cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 2,
                                                 pressure_a().handle(), pressure_a().size());
    const cubey::vulkan::DescriptorBufferWrite render_pressure_b =
        cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 3,
                                                 pressure_b().handle(), pressure_b().size());
    const cubey::vulkan::DescriptorBufferWrite divergence_source =
        cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 0, field_a().handle(),
                                                 field_a().size());
    const cubey::vulkan::DescriptorBufferWrite divergence_destination =
        cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 1,
                                                 divergence().handle(), divergence().size());
    const cubey::vulkan::DescriptorBufferWrite divergence_pressure_a =
        cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 2,
                                                 pressure_a().handle(), pressure_a().size());
    const cubey::vulkan::DescriptorBufferWrite divergence_pressure_b =
        cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 3,
                                                 pressure_b().handle(), pressure_b().size());
    const cubey::vulkan::DescriptorBufferWrite pressure_a_to_b_divergence =
        cubey::vulkan::storage_buffer_descriptor(pressure_a_to_b_descriptor_set_, 0,
                                                 divergence().handle(), divergence().size());
    const cubey::vulkan::DescriptorBufferWrite pressure_a_to_b_source =
        cubey::vulkan::storage_buffer_descriptor(pressure_a_to_b_descriptor_set_, 1,
                                                 pressure_a().handle(), pressure_a().size());
    const cubey::vulkan::DescriptorBufferWrite pressure_a_to_b_destination =
        cubey::vulkan::storage_buffer_descriptor(pressure_a_to_b_descriptor_set_, 2,
                                                 pressure_b().handle(), pressure_b().size());
    const cubey::vulkan::DescriptorBufferWrite pressure_b_to_a_divergence =
        cubey::vulkan::storage_buffer_descriptor(pressure_b_to_a_descriptor_set_, 0,
                                                 divergence().handle(), divergence().size());
    const cubey::vulkan::DescriptorBufferWrite pressure_b_to_a_source =
        cubey::vulkan::storage_buffer_descriptor(pressure_b_to_a_descriptor_set_, 1,
                                                 pressure_b().handle(), pressure_b().size());
    const cubey::vulkan::DescriptorBufferWrite pressure_b_to_a_destination =
        cubey::vulkan::storage_buffer_descriptor(pressure_b_to_a_descriptor_set_, 2,
                                                 pressure_a().handle(), pressure_a().size());
    const cubey::vulkan::DescriptorBufferWrite projection_pressure_a_field =
        cubey::vulkan::storage_buffer_descriptor(projection_pressure_a_descriptor_set_, 0,
                                                 field_a().handle(), field_a().size());
    const cubey::vulkan::DescriptorBufferWrite projection_pressure_a_pressure =
        cubey::vulkan::storage_buffer_descriptor(projection_pressure_a_descriptor_set_, 1,
                                                 pressure_a().handle(), pressure_a().size());
    const cubey::vulkan::DescriptorBufferWrite projection_pressure_b_field =
        cubey::vulkan::storage_buffer_descriptor(projection_pressure_b_descriptor_set_, 0,
                                                 field_a().handle(), field_a().size());
    const cubey::vulkan::DescriptorBufferWrite projection_pressure_b_pressure =
        cubey::vulkan::storage_buffer_descriptor(projection_pressure_b_descriptor_set_, 1,
                                                 pressure_b().handle(), pressure_b().size());

    const std::array<cubey::vulkan::DescriptorBufferWrite, 22> buffer_writes{
        inject_source,
        inject_destination,
        advect_source,
        advect_destination,
        render_source,
        render_divergence,
        render_pressure_a,
        render_pressure_b,
        divergence_source,
        divergence_destination,
        divergence_pressure_a,
        divergence_pressure_b,
        pressure_a_to_b_divergence,
        pressure_a_to_b_source,
        pressure_a_to_b_destination,
        pressure_b_to_a_divergence,
        pressure_b_to_a_source,
        pressure_b_to_a_destination,
        projection_pressure_a_field,
        projection_pressure_a_pressure,
        projection_pressure_b_field,
        projection_pressure_b_pressure,
    };
    std::array<VkWriteDescriptorSet, buffer_writes.size()> writes{};
    for (std::size_t index = 0; index < buffer_writes.size(); ++index) {
        writes[index] = buffer_writes[index].descriptor_write();
    }
    cubey::vulkan::update_descriptor_sets(device, writes);
}

void Fluid2DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    create_compute_layout(device, compute_descriptor_layout(), compute_pipeline_layout_);
    create_compute_pipeline_from_shader(device, "fluid_2d_inject.comp.spv",
                                        compute_pipeline_layout(), inject_pipeline_);
    create_compute_pipeline_from_shader(device, "fluid_2d_advect.comp.spv",
                                        compute_pipeline_layout(), advect_pipeline_);

    create_compute_layout(device, divergence_descriptor_layout(), divergence_pipeline_layout_);
    create_compute_pipeline_from_shader(device, "fluid_2d_divergence.comp.spv",
                                        divergence_pipeline_layout(), divergence_pipeline_);

    create_compute_layout(device, pressure_descriptor_layout(), pressure_pipeline_layout_);
    create_compute_pipeline_from_shader(device, "fluid_2d_pressure.comp.spv",
                                        pressure_pipeline_layout(), pressure_pipeline_);

    create_compute_layout(device, projection_descriptor_layout(), projection_pipeline_layout_);
    create_compute_pipeline_from_shader(device, "fluid_2d_projection.comp.spv",
                                        projection_pipeline_layout(), projection_pipeline_);
}

void Fluid2DGpuResources::create_render_pipeline(cubey::vulkan::Device& device,
                                                 VkFormat color_format, VkExtent2D extent) {
    const std::vector<std::uint32_t> vertex_code =
        cubey::vulkan::read_spirv_file(shader_path("fluid_2d.vert.spv"));
    const std::vector<std::uint32_t> fragment_code =
        cubey::vulkan::read_spirv_file(shader_path("fluid_2d_render.frag.spv"));
    cubey::vulkan::ShaderModule vertex_shader(device, vertex_code);
    cubey::vulkan::ShaderModule fragment_shader(device, fragment_code);

    const VkPipelineShaderStageCreateInfo vertex_stage =
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle());
    const VkPipelineShaderStageCreateInfo fragment_stage =
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader.handle());
    const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
        vertex_stage,
        fragment_stage,
    };

    const VkPushConstantRange render_push_constant{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 4U,
    };
    const std::array<VkDescriptorSetLayout, 1> set_layouts{render_descriptors().layout()};
    const std::array<VkPushConstantRange, 1> push_constants{render_push_constant};
    const cubey::vulkan::PipelineLayoutInfo layout_info({
        .set_layouts = set_layouts,
        .push_constants = push_constants,
    });
    render_pipeline_layout_.emplace(device, layout_info.create_info());

    cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
    pipeline_config.layout = render_pipeline_layout().handle();
    pipeline_config.extent = extent;
    pipeline_config.color_format = color_format;
    pipeline_config.shader_stages = shader_stages;
    const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
    render_pipeline_.emplace(device, pipeline_info.create_info());
}

const cubey::vulkan::Buffer& Fluid2DGpuResources::field_a() const {
    if (!field_a_.has_value()) {
        throw std::runtime_error("fluid field A is not initialized");
    }
    return field_a_.value();
}

const cubey::vulkan::Buffer& Fluid2DGpuResources::field_b() const {
    if (!field_b_.has_value()) {
        throw std::runtime_error("fluid field B is not initialized");
    }
    return field_b_.value();
}

const cubey::vulkan::Buffer& Fluid2DGpuResources::divergence() const {
    if (!divergence_.has_value()) {
        throw std::runtime_error("fluid divergence field is not initialized");
    }
    return divergence_.value();
}

const cubey::vulkan::Buffer& Fluid2DGpuResources::pressure_a() const {
    if (!pressure_a_.has_value()) {
        throw std::runtime_error("fluid pressure field A is not initialized");
    }
    return pressure_a_.value();
}

const cubey::vulkan::Buffer& Fluid2DGpuResources::pressure_b() const {
    if (!pressure_b_.has_value()) {
        throw std::runtime_error("fluid pressure field B is not initialized");
    }
    return pressure_b_.value();
}

VkDescriptorSetLayout Fluid2DGpuResources::compute_descriptor_layout() const {
    if (!compute_descriptor_layout_.has_value()) {
        throw std::runtime_error("compute descriptor layout is not initialized");
    }
    return compute_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid2DGpuResources::compute_descriptor_pool() const {
    if (!compute_descriptor_pool_.has_value()) {
        throw std::runtime_error("compute descriptor pool is not initialized");
    }
    return compute_descriptor_pool_.value();
}

VkDescriptorSetLayout Fluid2DGpuResources::divergence_descriptor_layout() const {
    if (!divergence_descriptor_layout_.has_value()) {
        throw std::runtime_error("divergence descriptor layout is not initialized");
    }
    return divergence_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid2DGpuResources::divergence_descriptor_pool() const {
    if (!divergence_descriptor_pool_.has_value()) {
        throw std::runtime_error("divergence descriptor pool is not initialized");
    }
    return divergence_descriptor_pool_.value();
}

VkDescriptorSetLayout Fluid2DGpuResources::pressure_descriptor_layout() const {
    if (!pressure_descriptor_layout_.has_value()) {
        throw std::runtime_error("pressure descriptor layout is not initialized");
    }
    return pressure_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid2DGpuResources::pressure_descriptor_pool() const {
    if (!pressure_descriptor_pool_.has_value()) {
        throw std::runtime_error("pressure descriptor pool is not initialized");
    }
    return pressure_descriptor_pool_.value();
}

VkDescriptorSetLayout Fluid2DGpuResources::projection_descriptor_layout() const {
    if (!projection_descriptor_layout_.has_value()) {
        throw std::runtime_error("projection descriptor layout is not initialized");
    }
    return projection_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid2DGpuResources::projection_descriptor_pool() const {
    if (!projection_descriptor_pool_.has_value()) {
        throw std::runtime_error("projection descriptor pool is not initialized");
    }
    return projection_descriptor_pool_.value();
}

const cubey::vulkan::DescriptorSetBundle& Fluid2DGpuResources::render_descriptors() const {
    if (!render_descriptors_.has_value()) {
        throw std::runtime_error("fluid render descriptors are not initialized");
    }
    return render_descriptors_.value();
}

const cubey::vulkan::PipelineLayout& Fluid2DGpuResources::compute_pipeline_layout() const {
    if (!compute_pipeline_layout_.has_value()) {
        throw std::runtime_error("compute pipeline layout is not initialized");
    }
    return compute_pipeline_layout_.value();
}

const cubey::vulkan::PipelineLayout& Fluid2DGpuResources::divergence_pipeline_layout() const {
    if (!divergence_pipeline_layout_.has_value()) {
        throw std::runtime_error("divergence pipeline layout is not initialized");
    }
    return divergence_pipeline_layout_.value();
}

const cubey::vulkan::PipelineLayout& Fluid2DGpuResources::pressure_pipeline_layout() const {
    if (!pressure_pipeline_layout_.has_value()) {
        throw std::runtime_error("pressure pipeline layout is not initialized");
    }
    return pressure_pipeline_layout_.value();
}

const cubey::vulkan::PipelineLayout& Fluid2DGpuResources::projection_pipeline_layout() const {
    if (!projection_pipeline_layout_.has_value()) {
        throw std::runtime_error("projection pipeline layout is not initialized");
    }
    return projection_pipeline_layout_.value();
}

const cubey::vulkan::ComputePipeline& Fluid2DGpuResources::inject_pipeline() const {
    if (!inject_pipeline_.has_value()) {
        throw std::runtime_error("inject pipeline is not initialized");
    }
    return inject_pipeline_.value();
}

const cubey::vulkan::ComputePipeline& Fluid2DGpuResources::advect_pipeline() const {
    if (!advect_pipeline_.has_value()) {
        throw std::runtime_error("advect pipeline is not initialized");
    }
    return advect_pipeline_.value();
}

const cubey::vulkan::ComputePipeline& Fluid2DGpuResources::divergence_pipeline() const {
    if (!divergence_pipeline_.has_value()) {
        throw std::runtime_error("divergence pipeline is not initialized");
    }
    return divergence_pipeline_.value();
}

const cubey::vulkan::ComputePipeline& Fluid2DGpuResources::pressure_pipeline() const {
    if (!pressure_pipeline_.has_value()) {
        throw std::runtime_error("pressure pipeline is not initialized");
    }
    return pressure_pipeline_.value();
}

const cubey::vulkan::ComputePipeline& Fluid2DGpuResources::projection_pipeline() const {
    if (!projection_pipeline_.has_value()) {
        throw std::runtime_error("projection pipeline is not initialized");
    }
    return projection_pipeline_.value();
}

const cubey::vulkan::PipelineLayout& Fluid2DGpuResources::render_pipeline_layout() const {
    if (!render_pipeline_layout_.has_value()) {
        throw std::runtime_error("render pipeline layout is not initialized");
    }
    return render_pipeline_layout_.value();
}

const cubey::vulkan::GraphicsPipeline& Fluid2DGpuResources::render_pipeline() const {
    if (!render_pipeline_.has_value()) {
        throw std::runtime_error("render pipeline is not initialized");
    }
    return render_pipeline_.value();
}

} // namespace cubey::projects::fluid_2d
