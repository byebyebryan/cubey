#include "fluid_3d_gpu_resources.h"

#include <cubey/vulkan/buffer.h>

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_FLUID_3D_SHADER_DIR
#error "CUBEY_FLUID_3D_SHADER_DIR must be defined by the fluid_3d CMake target"
#endif

namespace cubey::projects::fluid_3d {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FLUID_3D_SHADER_DIR) / filename;
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
        .size = sizeof(float) * 12U,
    };
}

[[nodiscard]] VkPushConstantRange render_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 20U,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo fluid_3d_render_pass_info() {
    return {
        .label = "fluid_3d.raymarch",
        .push_constants = {render_push_constant_range()},
    };
}

[[nodiscard]] cubey::render::Texture3DConfig volume_texture_config(const Fluid3DConfig& config,
                                                                   bool sampled) {
    return {
        .extent = {config.grid_width, config.grid_height, config.grid_depth},
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .create_sampler = sampled,
        .sampler =
            {
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            },
    };
}

[[nodiscard]] std::uint32_t advect_index(bool density_a_current, bool velocity_a_current) {
    return (density_a_current ? 0U : 2U) + (velocity_a_current ? 0U : 1U);
}

[[nodiscard]] std::uint32_t projection_index(bool velocity_a_current, bool pressure_a_current) {
    return (velocity_a_current ? 0U : 2U) + (pressure_a_current ? 0U : 1U);
}

void create_compute_pipeline_resource(
    cubey::vulkan::Device& device, const char* filename, VkDescriptorSetLayout descriptor_layout,
    std::optional<cubey::render::ComputePipelineResource>& destination) {
    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptor_layout};
    const std::array<VkPushConstantRange, 1> push_constants{simulation_push_constant_range()};
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

} // namespace

void Fluid3DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                            cubey::ProjectGpuServices& gpu,
                                                            const Fluid3DConfig& config) {
    config_ = config;
    if (density_a_.has_value()) {
        return;
    }

    create_volume_resources(device, gpu, config_);
    create_descriptor_resources(device);
    create_compute_pipelines(device);
}

void Fluid3DGpuResources::create_volume_resources(cubey::vulkan::Device& device,
                                                  cubey::ProjectGpuServices& gpu,
                                                  const Fluid3DConfig& config) {
    density_a_.emplace(device, volume_texture_config(config, true));
    density_b_.emplace(device, volume_texture_config(config, true));
    velocity_a_.emplace(device, volume_texture_config(config, true));
    velocity_b_.emplace(device, volume_texture_config(config, true));
    divergence_.emplace(device, volume_texture_config(config, false));
    pressure_a_.emplace(device, volume_texture_config(config, false));
    pressure_b_.emplace(device, volume_texture_config(config, false));

    const std::vector<Fluid3DInjectorGpu> empty(kMaxFluid3DInjectorCount);
    injectors_.emplace(upload_project_device_buffer(
        gpu, empty.data(), static_cast<VkDeviceSize>(fluid_3d_injector_capacity_byte_size()),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        "fluid_3d injector upload"));
}

void Fluid3DGpuResources::destroy_swapchain_resources() {
    render_pipeline_.reset();
}

void Fluid3DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    projection_pipeline_.reset();
    pressure_pipeline_.reset();
    divergence_pipeline_.reset();
    advect_pipeline_.reset();
    reset_pipeline_.reset();
    render_descriptors_.reset();
    projection_descriptor_pool_.reset();
    projection_descriptor_layout_.reset();
    pressure_descriptor_pool_.reset();
    pressure_descriptor_layout_.reset();
    divergence_descriptor_pool_.reset();
    divergence_descriptor_layout_.reset();
    advect_descriptor_pool_.reset();
    advect_descriptor_layout_.reset();
    reset_descriptor_pool_.reset();
    reset_descriptor_layout_.reset();
    injectors_.reset();
    pressure_b_.reset();
    pressure_a_.reset();
    divergence_.reset();
    velocity_b_.reset();
    velocity_a_.reset();
    density_b_.reset();
    density_a_.reset();
}

void Fluid3DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 5> reset_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 4,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo reset_info(reset_bindings);
    reset_descriptor_layout_.emplace(device, reset_info.layout_info());
    reset_descriptor_pool_.emplace(device, reset_info.pool_info());
    reset_descriptor_set_ = reset_descriptor_pool().allocate(reset_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 5> advect_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 4,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo advect_info(advect_bindings, 4);
    advect_descriptor_layout_.emplace(device, advect_info.layout_info());
    advect_descriptor_pool_.emplace(device, advect_info.pool_info());
    for (VkDescriptorSet& set : advect_descriptor_sets_) {
        set = advect_descriptor_pool().allocate(advect_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> divergence_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo divergence_info(divergence_bindings, 2);
    divergence_descriptor_layout_.emplace(device, divergence_info.layout_info());
    divergence_descriptor_pool_.emplace(device, divergence_info.pool_info());
    for (VkDescriptorSet& set : divergence_descriptor_sets_) {
        set = divergence_descriptor_pool().allocate(divergence_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> pressure_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo pressure_info(pressure_bindings, 2);
    pressure_descriptor_layout_.emplace(device, pressure_info.layout_info());
    pressure_descriptor_pool_.emplace(device, pressure_info.pool_info());
    pressure_a_to_b_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());
    pressure_b_to_a_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());

    const cubey::vulkan::DescriptorSetInfo projection_info(pressure_bindings, 4);
    projection_descriptor_layout_.emplace(device, projection_info.layout_info());
    projection_descriptor_pool_.emplace(device, projection_info.pool_info());
    for (VkDescriptorSet& set : projection_descriptor_sets_) {
        set = projection_descriptor_pool().allocate(projection_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> render_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo render_info(render_bindings, 4);
    render_descriptors_.emplace(device, render_info);

    update_descriptors(device);
}

void Fluid3DGpuResources::update_descriptors(cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_image(reset_descriptor_set_, 0, density_a().view())
        .storage_image(reset_descriptor_set_, 1, velocity_a().view())
        .storage_image(reset_descriptor_set_, 2, divergence().view())
        .storage_image(reset_descriptor_set_, 3, pressure_a().view())
        .storage_image(reset_descriptor_set_, 4, pressure_b().view());

    const std::array<const cubey::render::Texture3D*, 2> densities{&density_a(), &density_b()};
    const std::array<const cubey::render::Texture3D*, 2> velocities{&velocity_a(), &velocity_b()};
    for (std::uint32_t density_index = 0; density_index < 2; ++density_index) {
        for (std::uint32_t velocity_index = 0; velocity_index < 2; ++velocity_index) {
            const bool density_a_current = density_index == 0U;
            const bool velocity_a_current = velocity_index == 0U;
            const VkDescriptorSet set =
                advect_descriptor_set(density_a_current, velocity_a_current);
            writes.storage_image(set, 0, densities[density_index]->view())
                .storage_image(set, 1, velocities[velocity_index]->view())
                .storage_image(set, 2, densities[1U - density_index]->view())
                .storage_image(set, 3, velocities[1U - velocity_index]->view())
                .storage_buffer(set, 4, injectors().handle(), injectors().size());

            const VkDescriptorSet render_set =
                render_descriptor_set(density_a_current, velocity_a_current);
            writes
                .combined_image_sampler(render_set, 0, densities[density_index]->sampler().handle(),
                                        densities[density_index]->view(), VK_IMAGE_LAYOUT_GENERAL)
                .combined_image_sampler(
                    render_set, 1, velocities[velocity_index]->sampler().handle(),
                    velocities[velocity_index]->view(), VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    writes.storage_image(divergence_descriptor_set(true), 0, velocity_a().view())
        .storage_image(divergence_descriptor_set(true), 1, divergence().view())
        .storage_image(divergence_descriptor_set(false), 0, velocity_b().view())
        .storage_image(divergence_descriptor_set(false), 1, divergence().view())
        .storage_image(pressure_a_to_b_descriptor_set_, 0, divergence().view())
        .storage_image(pressure_a_to_b_descriptor_set_, 1, pressure_a().view())
        .storage_image(pressure_a_to_b_descriptor_set_, 2, pressure_b().view())
        .storage_image(pressure_b_to_a_descriptor_set_, 0, divergence().view())
        .storage_image(pressure_b_to_a_descriptor_set_, 1, pressure_b().view())
        .storage_image(pressure_b_to_a_descriptor_set_, 2, pressure_a().view());

    for (std::uint32_t velocity_index = 0; velocity_index < 2; ++velocity_index) {
        for (std::uint32_t pressure_index = 0; pressure_index < 2; ++pressure_index) {
            const bool velocity_a_current = velocity_index == 0U;
            const bool pressure_a_current = pressure_index == 0U;
            const VkDescriptorSet set =
                projection_descriptor_set(velocity_a_current, pressure_a_current);
            writes.storage_image(set, 0, velocities[velocity_index]->view())
                .storage_image(set, 1,
                               pressure_a_current ? pressure_a().view() : pressure_b().view())
                .storage_image(set, 2, velocities[1U - velocity_index]->view());
        }
    }

    writes.update(device);
}

void Fluid3DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    create_compute_pipeline_resource(device, "fluid_3d_reset.comp.spv", reset_descriptor_layout(),
                                     reset_pipeline_);
    create_compute_pipeline_resource(device, "fluid_3d_advect.comp.spv", advect_descriptor_layout(),
                                     advect_pipeline_);
    create_compute_pipeline_resource(device, "fluid_3d_divergence.comp.spv",
                                     divergence_descriptor_layout(), divergence_pipeline_);
    create_compute_pipeline_resource(device, "fluid_3d_pressure.comp.spv",
                                     pressure_descriptor_layout(), pressure_pipeline_);
    create_compute_pipeline_resource(device, "fluid_3d_projection.comp.spv",
                                     projection_descriptor_layout(), projection_pipeline_);
}

void Fluid3DGpuResources::create_render_pipeline(cubey::vulkan::Device& device,
                                                 VkFormat color_format, VkExtent2D extent) {
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("fluid_3d.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("fluid_3d_raymarch.frag.spv"),
        },
    };
    const std::array<VkDescriptorSetLayout, 1> set_layouts{render_descriptors().layout()};
    render_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                         .extent = extent,
                                         .color_format = color_format,
                                         .shader_stage_files = shader_stage_files,
                                         .descriptor_set_layouts = set_layouts,
                                         .material_pass = fluid_3d_render_pass_info(),
                                     });
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
VkDescriptorSet Fluid3DGpuResources::advect_descriptor_set(bool density_a_current,
                                                           bool velocity_a_current) const {
    return advect_descriptor_sets_.at(advect_index(density_a_current, velocity_a_current));
}

VkDescriptorSet Fluid3DGpuResources::divergence_descriptor_set(bool velocity_a_current) const {
    return divergence_descriptor_sets_.at(velocity_a_current ? 0U : 1U);
}

VkDescriptorSet Fluid3DGpuResources::projection_descriptor_set(bool velocity_a_current,
                                                               bool pressure_a_current) const {
    return projection_descriptor_sets_.at(projection_index(velocity_a_current, pressure_a_current));
}

VkDescriptorSet Fluid3DGpuResources::render_descriptor_set(bool density_a_current,
                                                           bool velocity_a_current) const {
    return render_descriptors().set(advect_index(density_a_current, velocity_a_current));
}

const cubey::render::Texture3D& Fluid3DGpuResources::density_a() const {
    if (!density_a_.has_value()) {
        throw std::runtime_error("fluid 3D density A is not initialized");
    }
    return density_a_.value();
}

const cubey::render::Texture3D& Fluid3DGpuResources::density_b() const {
    if (!density_b_.has_value()) {
        throw std::runtime_error("fluid 3D density B is not initialized");
    }
    return density_b_.value();
}

const cubey::render::Texture3D& Fluid3DGpuResources::velocity_a() const {
    if (!velocity_a_.has_value()) {
        throw std::runtime_error("fluid 3D velocity A is not initialized");
    }
    return velocity_a_.value();
}

const cubey::render::Texture3D& Fluid3DGpuResources::velocity_b() const {
    if (!velocity_b_.has_value()) {
        throw std::runtime_error("fluid 3D velocity B is not initialized");
    }
    return velocity_b_.value();
}

const cubey::render::Texture3D& Fluid3DGpuResources::divergence() const {
    if (!divergence_.has_value()) {
        throw std::runtime_error("fluid 3D divergence is not initialized");
    }
    return divergence_.value();
}

const cubey::render::Texture3D& Fluid3DGpuResources::pressure_a() const {
    if (!pressure_a_.has_value()) {
        throw std::runtime_error("fluid 3D pressure A is not initialized");
    }
    return pressure_a_.value();
}

const cubey::render::Texture3D& Fluid3DGpuResources::pressure_b() const {
    if (!pressure_b_.has_value()) {
        throw std::runtime_error("fluid 3D pressure B is not initialized");
    }
    return pressure_b_.value();
}

const cubey::vulkan::Buffer& Fluid3DGpuResources::injectors() const {
    if (!injectors_.has_value()) {
        throw std::runtime_error("fluid 3D injector buffer is not initialized");
    }
    return injectors_.value();
}

const cubey::render::ComputePipelineResource& Fluid3DGpuResources::reset_pipeline() const {
    if (!reset_pipeline_.has_value()) {
        throw std::runtime_error("fluid 3D reset pipeline is not initialized");
    }
    return reset_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Fluid3DGpuResources::advect_pipeline() const {
    if (!advect_pipeline_.has_value()) {
        throw std::runtime_error("fluid 3D advect pipeline is not initialized");
    }
    return advect_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Fluid3DGpuResources::divergence_pipeline() const {
    if (!divergence_pipeline_.has_value()) {
        throw std::runtime_error("fluid 3D divergence pipeline is not initialized");
    }
    return divergence_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Fluid3DGpuResources::pressure_pipeline() const {
    if (!pressure_pipeline_.has_value()) {
        throw std::runtime_error("fluid 3D pressure pipeline is not initialized");
    }
    return pressure_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Fluid3DGpuResources::projection_pipeline() const {
    if (!projection_pipeline_.has_value()) {
        throw std::runtime_error("fluid 3D projection pipeline is not initialized");
    }
    return projection_pipeline_.value();
}

const cubey::render::GraphicsPipelineResource& Fluid3DGpuResources::render_pipeline() const {
    if (!render_pipeline_.has_value()) {
        throw std::runtime_error("fluid 3D render pipeline is not initialized");
    }
    return render_pipeline_.value();
}

VkDescriptorSetLayout Fluid3DGpuResources::reset_descriptor_layout() const {
    if (!reset_descriptor_layout_.has_value()) {
        throw std::runtime_error("fluid 3D reset descriptor layout is not initialized");
    }
    return reset_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid3DGpuResources::reset_descriptor_pool() const {
    if (!reset_descriptor_pool_.has_value()) {
        throw std::runtime_error("fluid 3D reset descriptor pool is not initialized");
    }
    return reset_descriptor_pool_.value();
}

VkDescriptorSetLayout Fluid3DGpuResources::advect_descriptor_layout() const {
    if (!advect_descriptor_layout_.has_value()) {
        throw std::runtime_error("fluid 3D advect descriptor layout is not initialized");
    }
    return advect_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid3DGpuResources::advect_descriptor_pool() const {
    if (!advect_descriptor_pool_.has_value()) {
        throw std::runtime_error("fluid 3D advect descriptor pool is not initialized");
    }
    return advect_descriptor_pool_.value();
}

VkDescriptorSetLayout Fluid3DGpuResources::divergence_descriptor_layout() const {
    if (!divergence_descriptor_layout_.has_value()) {
        throw std::runtime_error("fluid 3D divergence descriptor layout is not initialized");
    }
    return divergence_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid3DGpuResources::divergence_descriptor_pool() const {
    if (!divergence_descriptor_pool_.has_value()) {
        throw std::runtime_error("fluid 3D divergence descriptor pool is not initialized");
    }
    return divergence_descriptor_pool_.value();
}

VkDescriptorSetLayout Fluid3DGpuResources::pressure_descriptor_layout() const {
    if (!pressure_descriptor_layout_.has_value()) {
        throw std::runtime_error("fluid 3D pressure descriptor layout is not initialized");
    }
    return pressure_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid3DGpuResources::pressure_descriptor_pool() const {
    if (!pressure_descriptor_pool_.has_value()) {
        throw std::runtime_error("fluid 3D pressure descriptor pool is not initialized");
    }
    return pressure_descriptor_pool_.value();
}

VkDescriptorSetLayout Fluid3DGpuResources::projection_descriptor_layout() const {
    if (!projection_descriptor_layout_.has_value()) {
        throw std::runtime_error("fluid 3D projection descriptor layout is not initialized");
    }
    return projection_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Fluid3DGpuResources::projection_descriptor_pool() const {
    if (!projection_descriptor_pool_.has_value()) {
        throw std::runtime_error("fluid 3D projection descriptor pool is not initialized");
    }
    return projection_descriptor_pool_.value();
}

const cubey::vulkan::DescriptorSetArray& Fluid3DGpuResources::render_descriptors() const {
    if (!render_descriptors_.has_value()) {
        throw std::runtime_error("fluid 3D render descriptors are not initialized");
    }
    return render_descriptors_.value();
}

} // namespace cubey::projects::fluid_3d
