#include "water_2d_gpu_resources.h"

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
inline constexpr VkDeviceSize kWater2DRenderPushConstantBytes =
    sizeof(float) * kWater2DRenderPushConstantFloatCount;

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
    if (particle_positions_.has_value()) {
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
    advect_particles_pipeline_resource_.reset();
    grid_to_particle_pipeline_resource_.reset();
    projection_pipeline_resource_.reset();
    pressure_pipeline_resource_.reset();
    divergence_pipeline_resource_.reset();
    force_pipeline_resource_.reset();
    particle_to_grid_pipeline_resource_.reset();
    build_bins_pipeline_resource_.reset();
    clear_bins_pipeline_resource_.reset();
    clear_grid_pipeline_resource_.reset();
    reset_pipeline_resource_.reset();
    field_descriptor_pool_.reset();
    field_descriptor_layout_.reset();
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
    particle_velocities_.reset();
    particle_positions_.reset();
}

void Water2DGpuResources::create_field_buffers(cubey::ProjectGpuServices& gpu,
                                               const Water2DConfig& config) {
    const std::vector<float> particle_initial(particle_value_count(config), 0.0F);
    const std::vector<float> cell_initial(cell_count(config), 0.0F);
    const std::vector<float> u_initial(u_face_count(config), 0.0F);
    const std::vector<float> v_initial(v_face_count(config), 0.0F);
    const std::vector<std::uint32_t> cell_count_initial(cell_count(config), 0U);
    const std::vector<std::uint32_t> bin_initial(particle_bin_index_count(config), 0U);
    const VkDeviceSize particle_byte_size =
        static_cast<VkDeviceSize>(particle_buffer_byte_size(config));
    const VkDeviceSize cell_byte_size = static_cast<VkDeviceSize>(scalar_field_byte_size(config));
    const VkDeviceSize u_byte_size = static_cast<VkDeviceSize>(u_face_byte_size(config));
    const VkDeviceSize v_byte_size = static_cast<VkDeviceSize>(v_face_byte_size(config));
    const VkDeviceSize cell_uint_byte_size =
        static_cast<VkDeviceSize>(cell_uint_field_byte_size(config));
    const VkDeviceSize bin_byte_size =
        static_cast<VkDeviceSize>(particle_bin_index_byte_size(config));

    particle_positions_.emplace(upload_project_device_buffer(
        gpu, particle_initial.data(), particle_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_2d particle position upload"));
    particle_velocities_.emplace(upload_project_device_buffer(
        gpu, particle_initial.data(), particle_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_2d particle velocity upload"));
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
}

void Water2DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    constexpr VkShaderStageFlags kStages =
        VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 14> field_bindings{{
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
    }};
    const cubey::vulkan::DescriptorSetInfo field_info(field_bindings);
    field_descriptor_layout_.emplace(device, field_info.layout_info());
    field_descriptor_pool_.emplace(device, field_info.pool_info());
    field_descriptor_set_ = field_descriptor_pool().allocate(field_descriptor_layout());

    update_field_descriptors(device);
}

void Water2DGpuResources::update_field_descriptors(cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;
    descriptor_writes
        .storage_buffer(field_descriptor_set_, 0, particle_positions().handle(),
                        particle_positions().size())
        .storage_buffer(field_descriptor_set_, 1, particle_velocities().handle(),
                        particle_velocities().size())
        .storage_buffer(field_descriptor_set_, 2, u().handle(), u().size())
        .storage_buffer(field_descriptor_set_, 3, u_previous().handle(), u_previous().size())
        .storage_buffer(field_descriptor_set_, 4, v().handle(), v().size())
        .storage_buffer(field_descriptor_set_, 5, v_previous().handle(), v_previous().size())
        .storage_buffer(field_descriptor_set_, 6, u_weight().handle(), u_weight().size())
        .storage_buffer(field_descriptor_set_, 7, v_weight().handle(), v_weight().size())
        .storage_buffer(field_descriptor_set_, 8, pressure_a().handle(), pressure_a().size())
        .storage_buffer(field_descriptor_set_, 9, pressure_b().handle(), pressure_b().size())
        .storage_buffer(field_descriptor_set_, 10, divergence().handle(), divergence().size())
        .storage_buffer(field_descriptor_set_, 11, solid().handle(), solid().size())
        .storage_buffer(field_descriptor_set_, 12, cell_counts().handle(), cell_counts().size())
        .storage_buffer(field_descriptor_set_, 13, cell_particle_indices().handle(),
                        cell_particle_indices().size());
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

const cubey::vulkan::Buffer& Water2DGpuResources::particle_positions() const {
    return require_initialized(particle_positions_, "water particle positions are not initialized");
}

const cubey::vulkan::Buffer& Water2DGpuResources::particle_velocities() const {
    return require_initialized(particle_velocities_,
                               "water particle velocities are not initialized");
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

const cubey::render::GraphicsPipelineResource&
Water2DGpuResources::render_pipeline_resource() const {
    return require_initialized(render_pipeline_resource_,
                               "water render pipeline is not initialized");
}

} // namespace cubey::projects::fluid::water_2d
