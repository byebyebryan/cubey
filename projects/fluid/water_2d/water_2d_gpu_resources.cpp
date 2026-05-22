#include "water_2d_gpu_resources.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
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
        .size = sizeof(float) * 4U,
    };
    return cubey::render::MaterialPassInfo{
        .label = "water_2d.render",
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

template <typename Value>
[[nodiscard]] const Value& require_initialized(const std::optional<Value>& value,
                                               const char* message) {
    if (!value.has_value()) {
        throw std::runtime_error(message);
    }
    return value.value();
}

} // namespace

void Water2DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                            cubey::ProjectGpuServices& gpu,
                                                            const Water2DConfig& config) {
    if (phi_a_.has_value()) {
        return;
    }

    create_field_buffers(gpu, config);
    create_descriptor_resources(device);
    create_compute_pipelines(device);
}

void Water2DGpuResources::destroy_swapchain_resources() {
    render_pipeline_resource_.reset();
}

void Water2DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    projection_pipeline_resource_.reset();
    pressure_pipeline_resource_.reset();
    divergence_pipeline_resource_.reset();
    reinitialize_phi_pipeline_resource_.reset();
    advect_phi_pipeline_resource_.reset();
    advect_velocity_pipeline_resource_.reset();
    force_pipeline_resource_.reset();
    reset_pipeline_resource_.reset();
    field_descriptor_pool_.reset();
    field_descriptor_layout_.reset();
    solid_.reset();
    divergence_.reset();
    pressure_b_.reset();
    pressure_a_.reset();
    v_b_.reset();
    v_a_.reset();
    u_b_.reset();
    u_a_.reset();
    phi_b_.reset();
    phi_a_.reset();
}

void Water2DGpuResources::create_field_buffers(cubey::ProjectGpuServices& gpu,
                                               const Water2DConfig& config) {
    const std::vector<float> cell_initial(cell_count(config), 0.0F);
    const std::vector<float> u_initial(u_face_count(config), 0.0F);
    const std::vector<float> v_initial(v_face_count(config), 0.0F);
    const VkDeviceSize cell_byte_size = static_cast<VkDeviceSize>(scalar_field_byte_size(config));
    const VkDeviceSize u_byte_size = static_cast<VkDeviceSize>(u_face_byte_size(config));
    const VkDeviceSize v_byte_size = static_cast<VkDeviceSize>(v_face_byte_size(config));

    phi_a_.emplace(upload_project_device_buffer(gpu, cell_initial.data(), cell_byte_size,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                "water_2d phi A upload"));
    phi_b_.emplace(upload_project_device_buffer(gpu, cell_initial.data(), cell_byte_size,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                "water_2d phi B upload"));
    u_a_.emplace(upload_project_device_buffer(gpu, u_initial.data(), u_byte_size,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              "water_2d U A upload"));
    u_b_.emplace(upload_project_device_buffer(gpu, u_initial.data(), u_byte_size,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              "water_2d U B upload"));
    v_a_.emplace(upload_project_device_buffer(gpu, v_initial.data(), v_byte_size,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              "water_2d V A upload"));
    v_b_.emplace(upload_project_device_buffer(gpu, v_initial.data(), v_byte_size,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              "water_2d V B upload"));
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
}

void Water2DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    constexpr VkShaderStageFlags kStages =
        VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 10> field_bindings{{
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
    }};
    const cubey::vulkan::DescriptorSetInfo field_info(field_bindings);
    field_descriptor_layout_.emplace(device, field_info.layout_info());
    field_descriptor_pool_.emplace(device, field_info.pool_info());
    field_descriptor_set_ = field_descriptor_pool().allocate(field_descriptor_layout());

    update_field_descriptors(device);
}

void Water2DGpuResources::update_field_descriptors(cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;
    descriptor_writes.storage_buffer(field_descriptor_set_, 0, phi_a().handle(), phi_a().size())
        .storage_buffer(field_descriptor_set_, 1, phi_b().handle(), phi_b().size())
        .storage_buffer(field_descriptor_set_, 2, u_a().handle(), u_a().size())
        .storage_buffer(field_descriptor_set_, 3, u_b().handle(), u_b().size())
        .storage_buffer(field_descriptor_set_, 4, v_a().handle(), v_a().size())
        .storage_buffer(field_descriptor_set_, 5, v_b().handle(), v_b().size())
        .storage_buffer(field_descriptor_set_, 6, pressure_a().handle(), pressure_a().size())
        .storage_buffer(field_descriptor_set_, 7, pressure_b().handle(), pressure_b().size())
        .storage_buffer(field_descriptor_set_, 8, divergence().handle(), divergence().size())
        .storage_buffer(field_descriptor_set_, 9, solid().handle(), solid().size());
    descriptor_writes.update(device);
}

void Water2DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    create_compute_pipeline_resource(device, "water_2d_reset.comp.spv", field_descriptor_layout(),
                                     reset_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_force.comp.spv", field_descriptor_layout(),
                                     force_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_advect_velocity.comp.spv",
                                     field_descriptor_layout(), advect_velocity_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_advect_phi.comp.spv",
                                     field_descriptor_layout(), advect_phi_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_reinitialize_phi.comp.spv",
                                     field_descriptor_layout(),
                                     reinitialize_phi_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_divergence.comp.spv",
                                     field_descriptor_layout(), divergence_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_pressure.comp.spv",
                                     field_descriptor_layout(), pressure_pipeline_resource_);
    create_compute_pipeline_resource(device, "water_2d_projection.comp.spv",
                                     field_descriptor_layout(), projection_pipeline_resource_);
}

void Water2DGpuResources::create_render_pipeline(cubey::vulkan::Device& device,
                                                 VkFormat color_format, VkExtent2D extent) {
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

    const std::array<VkDescriptorSetLayout, 1> set_layouts{field_descriptor_layout()};
    const cubey::render::MaterialPassInfo material_pass = water_render_pass_info();
    render_pipeline_resource_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                                  .extent = extent,
                                                  .color_format = color_format,
                                                  .shader_stage_files = shader_stage_files,
                                                  .descriptor_set_layouts = set_layouts,
                                                  .material_pass = material_pass,
                                              });
}

const cubey::vulkan::Buffer& Water2DGpuResources::phi_a() const {
    return require_initialized(phi_a_, "water phi A field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::phi_b() const {
    return require_initialized(phi_b_, "water phi B field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::u_a() const {
    return require_initialized(u_a_, "water U A field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::u_b() const {
    return require_initialized(u_b_, "water U B field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::v_a() const {
    return require_initialized(v_a_, "water V A field is not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::v_b() const {
    return require_initialized(v_b_, "water V B field is not initialized");
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

const cubey::render::ComputePipelineResource& Water2DGpuResources::force_pipeline_resource() const {
    return require_initialized(force_pipeline_resource_, "water force pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::advect_velocity_pipeline_resource() const {
    return require_initialized(advect_velocity_pipeline_resource_,
                               "water velocity advection pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::advect_phi_pipeline_resource() const {
    return require_initialized(advect_phi_pipeline_resource_,
                               "water phi advection pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water2DGpuResources::reinitialize_phi_pipeline_resource() const {
    return require_initialized(reinitialize_phi_pipeline_resource_,
                               "water phi reinitialization pipeline is not initialized");
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

const cubey::render::GraphicsPipelineResource&
Water2DGpuResources::render_pipeline_resource() const {
    return require_initialized(render_pipeline_resource_,
                               "water render pipeline is not initialized");
}

} // namespace cubey::projects::fluid::water_2d
