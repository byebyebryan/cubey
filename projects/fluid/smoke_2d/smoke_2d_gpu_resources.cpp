#include "smoke_2d_gpu_resources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_SMOKE_2D_SHADER_DIR
#error "CUBEY_SMOKE_2D_SHADER_DIR must be defined by the smoke_2d CMake target"
#endif

namespace cubey::projects::fluid::smoke_2d {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_SMOKE_2D_SHADER_DIR) / filename;
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
        .size = sizeof(float) * 20U,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo smoke_render_pass_info() {
    const VkPushConstantRange render_push_constant{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 4U,
    };
    return cubey::render::MaterialPassInfo{
        .label = "smoke_2d.render",
        .push_constants = {render_push_constant},
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

[[nodiscard]] std::vector<float> create_obstacle_mask(const Smoke2DConfig& config) {
    std::vector<float> mask(field_cell_count(config), 0.0F);
    if (!config.obstacles_enabled) {
        return mask;
    }
    const float aspect =
        static_cast<float>(config.grid_width) / static_cast<float>(config.grid_height);
    for (std::uint32_t y = 0; y < config.grid_height; ++y) {
        for (std::uint32_t x = 0; x < config.grid_width; ++x) {
            const float uv_x = (static_cast<float>(x) + 0.5F) /
                               static_cast<float>(config.grid_width);
            const float uv_y = (static_cast<float>(y) + 0.5F) /
                               static_cast<float>(config.grid_height);
            const float left_circle_x = (uv_x - 0.34F) * aspect;
            const float left_circle_y = uv_y - 0.48F;
            const float right_circle_x = (uv_x - 0.66F) * aspect;
            const float right_circle_y = uv_y - 0.57F;
            const bool border = x < 2U || y < 2U || x + 3U > config.grid_width ||
                                y + 3U > config.grid_height;
            const bool left_circle =
                (left_circle_x * left_circle_x) + (left_circle_y * left_circle_y) < 0.0081F;
            const bool right_circle =
                (right_circle_x * right_circle_x) + (right_circle_y * right_circle_y) < 0.0064F;
            const bool vertical_bar =
                uv_x > 0.49F && uv_x < 0.53F && uv_y > 0.22F && uv_y < 0.42F;
            if (border || left_circle || right_circle || vertical_bar) {
                mask[(static_cast<std::size_t>(y) * config.grid_width) + x] = 1.0F;
            }
        }
    }
    return mask;
}

} // namespace

void Smoke2DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                            cubey::ProjectGpuServices& gpu,
                                                            const Smoke2DConfig& config) {
    config_ = config;
    if (field_a_.has_value()) {
        return;
    }

    create_field_buffers(gpu, config_);
    create_descriptor_resources(device);
    create_compute_pipelines(device);
}

void Smoke2DGpuResources::destroy_swapchain_resources() {
    render_pipeline_resource_.reset();
}

void Smoke2DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    projection_pipeline_resource_.reset();
    pressure_pipeline_resource_.reset();
    divergence_pipeline_resource_.reset();
    vorticity_pipeline_resource_.reset();
    curl_pipeline_resource_.reset();
    advect_correct_pipeline_resource_.reset();
    advect_pipeline_resource_.reset();
    inject_pipeline_resource_.reset();
    render_descriptors_.reset();
    projection_descriptor_pool_.reset();
    projection_descriptor_layout_.reset();
    pressure_descriptor_pool_.reset();
    pressure_descriptor_layout_.reset();
    divergence_descriptor_pool_.reset();
    divergence_descriptor_layout_.reset();
    advect_correct_descriptor_pool_.reset();
    advect_correct_descriptor_layout_.reset();
    compute_descriptor_pool_.reset();
    compute_descriptor_layout_.reset();
    pressure_b_.reset();
    pressure_a_.reset();
    injectors_.reset();
    obstacle_.reset();
    curl_.reset();
    divergence_.reset();
    field_temp_.reset();
    field_b_.reset();
    field_a_.reset();
}

void Smoke2DGpuResources::create_field_buffers(cubey::ProjectGpuServices& gpu,
                                               const Smoke2DConfig& config) {
    const std::vector<SmokeCellGpu> initial(field_cell_count(config));
    const VkDeviceSize byte_size = static_cast<VkDeviceSize>(field_byte_size(config));
    field_a_.emplace(upload_project_device_buffer(gpu, initial.data(), byte_size,
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  "smoke_2d field A upload"));
    field_b_.emplace(upload_project_device_buffer(gpu, initial.data(), byte_size,
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  "smoke_2d field B upload"));
    field_temp_.emplace(upload_project_device_buffer(gpu, initial.data(), byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "smoke_2d field temp upload"));

    const std::vector<float> scalar_initial(field_cell_count(config), 0.0F);
    const VkDeviceSize scalar_byte_size = static_cast<VkDeviceSize>(scalar_field_byte_size(config));
    divergence_.emplace(upload_project_device_buffer(gpu, scalar_initial.data(), scalar_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "smoke_2d divergence upload"));
    curl_.emplace(upload_project_device_buffer(gpu, scalar_initial.data(), scalar_byte_size,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                               "smoke_2d curl upload"));
    const std::vector<float> obstacle_initial = create_obstacle_mask(config);
    obstacle_.emplace(upload_project_device_buffer(gpu, obstacle_initial.data(), scalar_byte_size,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                   "smoke_2d obstacle upload"));
    std::vector<Smoke2DInjectorGpu> injector_initial(kMaxProceduralInjectorCount);
    const std::vector<Smoke2DInjectorGpu> active_injectors =
        smoke_2d_injectors_to_gpu(create_smoke_2d_injectors(config), config);
    std::copy(active_injectors.begin(), active_injectors.end(), injector_initial.begin());
    injectors_.emplace(upload_project_device_buffer(
        gpu, injector_initial.data(),
        static_cast<VkDeviceSize>(smoke_2d_injector_capacity_byte_size()),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        "smoke_2d injector upload"));
    pressure_a_.emplace(upload_project_device_buffer(gpu, scalar_initial.data(), scalar_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "smoke_2d pressure A upload"));
    pressure_b_.emplace(upload_project_device_buffer(gpu, scalar_initial.data(), scalar_byte_size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     "smoke_2d pressure B upload"));
}

void Smoke2DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> compute_bindings{{
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
    const cubey::vulkan::DescriptorSetInfo compute_info(compute_bindings, 4);
    compute_descriptor_layout_.emplace(device, compute_info.layout_info());
    compute_descriptor_pool_.emplace(device, compute_info.pool_info());
    inject_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());
    advect_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());
    curl_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());
    vorticity_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> advect_correct_bindings{{
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
    const cubey::vulkan::DescriptorSetInfo advect_correct_info(advect_correct_bindings);
    advect_correct_descriptor_layout_.emplace(device, advect_correct_info.layout_info());
    advect_correct_descriptor_pool_.emplace(device, advect_correct_info.pool_info());
    advect_correct_descriptor_set_ =
        advect_correct_descriptor_pool().allocate(advect_correct_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 5> divergence_bindings{{
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
        {
            .binding = 4,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo divergence_info(divergence_bindings);
    divergence_descriptor_layout_.emplace(device, divergence_info.layout_info());
    divergence_descriptor_pool_.emplace(device, divergence_info.pool_info());
    divergence_descriptor_set_ =
        divergence_descriptor_pool().allocate(divergence_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> pressure_bindings{{
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
    const cubey::vulkan::DescriptorSetInfo pressure_info(pressure_bindings, 2);
    pressure_descriptor_layout_.emplace(device, pressure_info.layout_info());
    pressure_descriptor_pool_.emplace(device, pressure_info.pool_info());
    pressure_a_to_b_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());
    pressure_b_to_a_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> projection_bindings{{
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
    const cubey::vulkan::DescriptorSetInfo projection_info(projection_bindings, 2);
    projection_descriptor_layout_.emplace(device, projection_info.layout_info());
    projection_descriptor_pool_.emplace(device, projection_info.pool_info());
    projection_pressure_a_descriptor_set_ =
        projection_descriptor_pool().allocate(projection_descriptor_layout());
    projection_pressure_b_descriptor_set_ =
        projection_descriptor_pool().allocate(projection_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 6> render_bindings{{
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
        {
            .binding = 4,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 5,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo render_info(render_bindings);
    render_descriptors_.emplace(device, render_info);

    update_field_descriptors(device);
}

void Smoke2DGpuResources::update_field_descriptors(cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;

    descriptor_writes
        .storage_buffer(advect_descriptor_set_, 0, field_a().handle(), field_a().size())
        .storage_buffer(advect_descriptor_set_, 1, field_temp().handle(), field_temp().size())
        .storage_buffer(advect_descriptor_set_, 2, obstacle().handle(), obstacle().size())
        .storage_buffer(advect_descriptor_set_, 3, injectors().handle(), injectors().size())
        .storage_buffer(advect_correct_descriptor_set_, 0, field_a().handle(), field_a().size())
        .storage_buffer(advect_correct_descriptor_set_, 1, field_temp().handle(),
                        field_temp().size())
        .storage_buffer(advect_correct_descriptor_set_, 2, field_b().handle(), field_b().size())
        .storage_buffer(advect_correct_descriptor_set_, 3, obstacle().handle(), obstacle().size())
        .storage_buffer(inject_descriptor_set_, 0, field_b().handle(), field_b().size())
        .storage_buffer(inject_descriptor_set_, 1, field_a().handle(), field_a().size())
        .storage_buffer(inject_descriptor_set_, 2, obstacle().handle(), obstacle().size())
        .storage_buffer(inject_descriptor_set_, 3, injectors().handle(), injectors().size())
        .storage_buffer(curl_descriptor_set_, 0, field_a().handle(), field_a().size())
        .storage_buffer(curl_descriptor_set_, 1, curl().handle(), curl().size())
        .storage_buffer(curl_descriptor_set_, 2, obstacle().handle(), obstacle().size())
        .storage_buffer(curl_descriptor_set_, 3, injectors().handle(), injectors().size())
        .storage_buffer(vorticity_descriptor_set_, 0, field_a().handle(), field_a().size())
        .storage_buffer(vorticity_descriptor_set_, 1, curl().handle(), curl().size())
        .storage_buffer(vorticity_descriptor_set_, 2, obstacle().handle(), obstacle().size())
        .storage_buffer(vorticity_descriptor_set_, 3, injectors().handle(), injectors().size());

    descriptor_writes
        .storage_buffer(render_descriptors().set(), 0, field_a().handle(), field_a().size())
        .storage_buffer(render_descriptors().set(), 1, divergence().handle(), divergence().size())
        .storage_buffer(render_descriptors().set(), 2, pressure_a().handle(), pressure_a().size())
        .storage_buffer(render_descriptors().set(), 3, pressure_b().handle(), pressure_b().size())
        .storage_buffer(render_descriptors().set(), 4, curl().handle(), curl().size())
        .storage_buffer(render_descriptors().set(), 5, obstacle().handle(), obstacle().size());

    descriptor_writes
        .storage_buffer(divergence_descriptor_set_, 0, field_a().handle(), field_a().size())
        .storage_buffer(divergence_descriptor_set_, 1, divergence().handle(), divergence().size())
        .storage_buffer(divergence_descriptor_set_, 2, pressure_a().handle(), pressure_a().size())
        .storage_buffer(divergence_descriptor_set_, 3, pressure_b().handle(), pressure_b().size())
        .storage_buffer(divergence_descriptor_set_, 4, obstacle().handle(), obstacle().size());

    descriptor_writes
        .storage_buffer(pressure_a_to_b_descriptor_set_, 0, divergence().handle(),
                        divergence().size())
        .storage_buffer(pressure_a_to_b_descriptor_set_, 1, pressure_a().handle(),
                        pressure_a().size())
        .storage_buffer(pressure_a_to_b_descriptor_set_, 2, pressure_b().handle(),
                        pressure_b().size())
        .storage_buffer(pressure_a_to_b_descriptor_set_, 3, obstacle().handle(), obstacle().size())
        .storage_buffer(pressure_b_to_a_descriptor_set_, 0, divergence().handle(),
                        divergence().size())
        .storage_buffer(pressure_b_to_a_descriptor_set_, 1, pressure_b().handle(),
                        pressure_b().size())
        .storage_buffer(pressure_b_to_a_descriptor_set_, 2, pressure_a().handle(),
                        pressure_a().size())
        .storage_buffer(pressure_b_to_a_descriptor_set_, 3, obstacle().handle(), obstacle().size());

    descriptor_writes
        .storage_buffer(projection_pressure_a_descriptor_set_, 0, field_a().handle(),
                        field_a().size())
        .storage_buffer(projection_pressure_a_descriptor_set_, 1, pressure_a().handle(),
                        pressure_a().size())
        .storage_buffer(projection_pressure_a_descriptor_set_, 2, obstacle().handle(),
                        obstacle().size())
        .storage_buffer(projection_pressure_b_descriptor_set_, 0, field_a().handle(),
                        field_a().size())
        .storage_buffer(projection_pressure_b_descriptor_set_, 1, pressure_b().handle(),
                        pressure_b().size())
        .storage_buffer(projection_pressure_b_descriptor_set_, 2, obstacle().handle(),
                        obstacle().size());

    descriptor_writes.update(device);
}

void Smoke2DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    create_compute_pipeline_resource(device, "smoke_2d_inject.comp.spv",
                                     compute_descriptor_layout(), inject_pipeline_resource_);
    create_compute_pipeline_resource(device, "smoke_2d_advect_predict.comp.spv",
                                     compute_descriptor_layout(), advect_pipeline_resource_);
    create_compute_pipeline_resource(device, "smoke_2d_advect_correct.comp.spv",
                                     advect_correct_descriptor_layout(),
                                     advect_correct_pipeline_resource_);
    create_compute_pipeline_resource(device, "smoke_2d_curl.comp.spv", compute_descriptor_layout(),
                                     curl_pipeline_resource_);
    create_compute_pipeline_resource(device, "smoke_2d_vorticity.comp.spv",
                                     compute_descriptor_layout(), vorticity_pipeline_resource_);
    create_compute_pipeline_resource(device, "smoke_2d_divergence.comp.spv",
                                     divergence_descriptor_layout(), divergence_pipeline_resource_);
    create_compute_pipeline_resource(device, "smoke_2d_pressure.comp.spv",
                                     pressure_descriptor_layout(), pressure_pipeline_resource_);
    create_compute_pipeline_resource(device, "smoke_2d_projection.comp.spv",
                                     projection_descriptor_layout(), projection_pipeline_resource_);
}

void Smoke2DGpuResources::create_render_pipeline(cubey::vulkan::Device& device,
                                                 VkFormat color_format, VkExtent2D extent) {
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("smoke_2d.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("smoke_2d_render.frag.spv"),
        },
    };

    const std::array<VkDescriptorSetLayout, 1> set_layouts{render_descriptors().layout()};
    const cubey::render::MaterialPassInfo material_pass = smoke_render_pass_info();
    render_pipeline_resource_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                                  .extent = extent,
                                                  .color_format = color_format,
                                                  .shader_stage_files = shader_stage_files,
                                                  .descriptor_set_layouts = set_layouts,
                                                  .material_pass = material_pass,
                                              });
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::field_a() const {
    if (!field_a_.has_value()) {
        throw std::runtime_error("smoke field A is not initialized");
    }
    return field_a_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::field_b() const {
    if (!field_b_.has_value()) {
        throw std::runtime_error("smoke field B is not initialized");
    }
    return field_b_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::field_temp() const {
    if (!field_temp_.has_value()) {
        throw std::runtime_error("fluid temp field is not initialized");
    }
    return field_temp_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::divergence() const {
    if (!divergence_.has_value()) {
        throw std::runtime_error("fluid divergence field is not initialized");
    }
    return divergence_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::curl() const {
    if (!curl_.has_value()) {
        throw std::runtime_error("fluid curl field is not initialized");
    }
    return curl_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::obstacle() const {
    if (!obstacle_.has_value()) {
        throw std::runtime_error("fluid obstacle field is not initialized");
    }
    return obstacle_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::injectors() const {
    if (!injectors_.has_value()) {
        throw std::runtime_error("smoke injector buffer is not initialized");
    }
    return injectors_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::pressure_a() const {
    if (!pressure_a_.has_value()) {
        throw std::runtime_error("fluid pressure field A is not initialized");
    }
    return pressure_a_.value();
}

const cubey::vulkan::Buffer& Smoke2DGpuResources::pressure_b() const {
    if (!pressure_b_.has_value()) {
        throw std::runtime_error("fluid pressure field B is not initialized");
    }
    return pressure_b_.value();
}

VkDescriptorSetLayout Smoke2DGpuResources::compute_descriptor_layout() const {
    if (!compute_descriptor_layout_.has_value()) {
        throw std::runtime_error("compute descriptor layout is not initialized");
    }
    return compute_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Smoke2DGpuResources::compute_descriptor_pool() const {
    if (!compute_descriptor_pool_.has_value()) {
        throw std::runtime_error("compute descriptor pool is not initialized");
    }
    return compute_descriptor_pool_.value();
}

VkDescriptorSetLayout Smoke2DGpuResources::advect_correct_descriptor_layout() const {
    if (!advect_correct_descriptor_layout_.has_value()) {
        throw std::runtime_error("advect correct descriptor layout is not initialized");
    }
    return advect_correct_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool&
Smoke2DGpuResources::advect_correct_descriptor_pool() const {
    if (!advect_correct_descriptor_pool_.has_value()) {
        throw std::runtime_error("advect correct descriptor pool is not initialized");
    }
    return advect_correct_descriptor_pool_.value();
}

VkDescriptorSetLayout Smoke2DGpuResources::divergence_descriptor_layout() const {
    if (!divergence_descriptor_layout_.has_value()) {
        throw std::runtime_error("divergence descriptor layout is not initialized");
    }
    return divergence_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Smoke2DGpuResources::divergence_descriptor_pool() const {
    if (!divergence_descriptor_pool_.has_value()) {
        throw std::runtime_error("divergence descriptor pool is not initialized");
    }
    return divergence_descriptor_pool_.value();
}

VkDescriptorSetLayout Smoke2DGpuResources::pressure_descriptor_layout() const {
    if (!pressure_descriptor_layout_.has_value()) {
        throw std::runtime_error("pressure descriptor layout is not initialized");
    }
    return pressure_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Smoke2DGpuResources::pressure_descriptor_pool() const {
    if (!pressure_descriptor_pool_.has_value()) {
        throw std::runtime_error("pressure descriptor pool is not initialized");
    }
    return pressure_descriptor_pool_.value();
}

VkDescriptorSetLayout Smoke2DGpuResources::projection_descriptor_layout() const {
    if (!projection_descriptor_layout_.has_value()) {
        throw std::runtime_error("projection descriptor layout is not initialized");
    }
    return projection_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Smoke2DGpuResources::projection_descriptor_pool() const {
    if (!projection_descriptor_pool_.has_value()) {
        throw std::runtime_error("projection descriptor pool is not initialized");
    }
    return projection_descriptor_pool_.value();
}

const cubey::vulkan::DescriptorSetBundle& Smoke2DGpuResources::render_descriptors() const {
    if (!render_descriptors_.has_value()) {
        throw std::runtime_error("fluid render descriptors are not initialized");
    }
    return render_descriptors_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::inject_pipeline_resource() const {
    if (!inject_pipeline_resource_.has_value()) {
        throw std::runtime_error("inject pipeline resource is not initialized");
    }
    return inject_pipeline_resource_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::advect_pipeline_resource() const {
    if (!advect_pipeline_resource_.has_value()) {
        throw std::runtime_error("advect pipeline resource is not initialized");
    }
    return advect_pipeline_resource_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::advect_correct_pipeline_resource() const {
    if (!advect_correct_pipeline_resource_.has_value()) {
        throw std::runtime_error("advect correct pipeline resource is not initialized");
    }
    return advect_correct_pipeline_resource_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::curl_pipeline_resource() const {
    if (!curl_pipeline_resource_.has_value()) {
        throw std::runtime_error("curl pipeline resource is not initialized");
    }
    return curl_pipeline_resource_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::vorticity_pipeline_resource() const {
    if (!vorticity_pipeline_resource_.has_value()) {
        throw std::runtime_error("vorticity pipeline resource is not initialized");
    }
    return vorticity_pipeline_resource_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::divergence_pipeline_resource() const {
    if (!divergence_pipeline_resource_.has_value()) {
        throw std::runtime_error("divergence pipeline resource is not initialized");
    }
    return divergence_pipeline_resource_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::pressure_pipeline_resource() const {
    if (!pressure_pipeline_resource_.has_value()) {
        throw std::runtime_error("pressure pipeline resource is not initialized");
    }
    return pressure_pipeline_resource_.value();
}

const cubey::render::ComputePipelineResource&
Smoke2DGpuResources::projection_pipeline_resource() const {
    if (!projection_pipeline_resource_.has_value()) {
        throw std::runtime_error("projection pipeline resource is not initialized");
    }
    return projection_pipeline_resource_.value();
}

const cubey::render::GraphicsPipelineResource&
Smoke2DGpuResources::render_pipeline_resource() const {
    if (!render_pipeline_resource_.has_value()) {
        throw std::runtime_error("render pipeline resource is not initialized");
    }
    return render_pipeline_resource_.value();
}

} // namespace cubey::projects::fluid::smoke_2d
