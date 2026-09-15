#include "fluid_25d_gpu_resources.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_FLUID_25D_SHADER_DIR
#error "CUBEY_FLUID_25D_SHADER_DIR must be defined by the fluid_25d CMake target"
#endif

namespace cubey::projects::fluid::fluid_25d {
namespace {

inline constexpr VkDeviceSize kSimulationPushConstantBytes = sizeof(float) * 8U;

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FLUID_25D_SHADER_DIR) / filename;
}

[[nodiscard]] VkPushConstantRange simulation_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = kSimulationPushConstantBytes,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo diagnostic_pass_info() {
    const VkPushConstantRange push_constant{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 4U,
    };
    return {
        .label = "fluid_25d.render",
        .push_constants = {push_constant},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_pass_info() {
    const VkPushConstantRange push_constant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 24U,
    };
    return {
        .label = "fluid_25d.catchment_terrain",
        .push_constants = {push_constant},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_pass_info() {
    cubey::render::MaterialPassInfo pass = terrain_pass_info();
    pass.label = "fluid_25d.catchment_water";
    pass.depth_write = false;
    pass.blend_enable = true;
    // The water fragment shader writes premultiplied color: straight lighting
    // is multiplied by alpha before source-over compositing.
    pass.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
    pass.dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    pass.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
    pass.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    return pass;
}

void emplace_compute_pipeline(std::optional<cubey::render::ComputePipelineResource>& destination,
                              cubey::vulkan::Device& device, const char* filename,
                              VkDescriptorSetLayout descriptor_layout) {
    const std::array<VkPushConstantRange, 1> push_constants{simulation_push_constant_range()};
    cubey::render::emplace_single_set_compute_pipeline_resource(
        destination, device, cubey::render::compute_shader_file(shader_path(filename)),
        descriptor_layout, push_constants);
}

[[nodiscard]] cubey::vulkan::DescriptorSetInfo storage_set_info(std::uint32_t binding_count,
                                                                VkShaderStageFlags stages) {
    std::vector<cubey::vulkan::DescriptorSetBindingConfig> bindings;
    bindings.reserve(binding_count);
    for (std::uint32_t binding = 0; binding < binding_count; ++binding) {
        bindings.push_back({
            .binding = binding,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = stages,
        });
    }
    return cubey::vulkan::DescriptorSetInfo(bindings);
}

[[nodiscard]] VkBufferUsageFlags static_buffer_usage() {
    return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
           VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
}

} // namespace

void Fluid25DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                             cubey::ProjectGpuServices& gpu,
                                                             const Fluid25DConfig& config,
                                                             const Fluid25DScenarioData& scenario) {
    if (terrain_.has_value()) {
        return;
    }
    create_buffers(gpu, config, scenario);
    create_descriptors(device);
    create_compute_pipelines(device);
}

void Fluid25DGpuResources::create_buffers(cubey::ProjectGpuServices& gpu,
                                          const Fluid25DConfig& config,
                                          const Fluid25DScenarioData& scenario) {
    const std::size_t cells = fluid_25d_cell_count(config);
    if (scenario.terrain_height_m.size() != cells ||
        scenario.initial_water_depth_m.size() != cells ||
        scenario.source_depth_rate_m_per_s.size() != cells ||
        scenario.sink_depth_rate_m_per_s.size() != cells) {
        throw std::runtime_error("fluid 2.5D GPU scenario field sizes do not match config");
    }

    const VkBufferUsageFlags scalar_usage = static_buffer_usage();
    const auto upload = [&gpu](const auto& values, VkBufferUsageFlags usage, std::string label) {
        return gpu.upload_device_buffer(
            values.data(), static_cast<VkDeviceSize>(values.size() * sizeof(values.front())), usage,
            std::move(label));
    };
    terrain_.emplace(upload(scenario.terrain_height_m, scalar_usage, "fluid_25d terrain upload"));
    source_rate_.emplace(
        upload(scenario.source_depth_rate_m_per_s, scalar_usage, "fluid_25d source rate upload"));
    sink_rate_.emplace(
        upload(scenario.sink_depth_rate_m_per_s, scalar_usage, "fluid_25d sink rate upload"));
    initial_depth_.emplace(
        upload(scenario.initial_water_depth_m, scalar_usage, "fluid_25d initial depth upload"));
    depth_a_.emplace(
        upload(scenario.initial_water_depth_m, scalar_usage, "fluid_25d depth A upload"));
    depth_b_.emplace(
        upload(scenario.initial_water_depth_m, scalar_usage, "fluid_25d depth B upload"));

    const std::vector<Fluid25DFluxGpu> zero_flux(cells);
    const std::vector<Fluid25DVelocityGpu> zero_velocity(cells);
    const std::vector<Fluid25DLedgerGpu> zero_ledger(cells);
    flux_.emplace(upload(zero_flux, static_buffer_usage(), "fluid_25d face flux upload"));
    velocity_.emplace(upload(zero_velocity, static_buffer_usage(), "fluid_25d velocity upload"));
    ledger_.emplace(upload(zero_ledger, static_buffer_usage(), "fluid_25d ledger upload"));
    current_depth_is_a_ = true;
}

void Fluid25DGpuResources::create_descriptors(cubey::vulkan::Device& device) {
    // reset: initial depth, depth A, depth B, flux, velocity, ledger
    const cubey::vulkan::DescriptorSetInfo reset_info =
        storage_set_info(6U, VK_SHADER_STAGE_COMPUTE_BIT);
    reset_descriptors_.emplace(device, reset_info);

    // flux: terrain, source rate, sink rate, source depth, flux, ledger
    const cubey::vulkan::DescriptorSetInfo flux_info =
        storage_set_info(6U, VK_SHADER_STAGE_COMPUTE_BIT);
    flux_a_descriptors_.emplace(device, flux_info);
    flux_b_descriptors_.emplace(device, flux_info);

    // depth: source rate, sink rate, source depth, face flux, destination depth, velocity
    const cubey::vulkan::DescriptorSetInfo depth_info =
        storage_set_info(6U, VK_SHADER_STAGE_COMPUTE_BIT);
    depth_a_to_b_descriptors_.emplace(device, depth_info);
    depth_b_to_a_descriptors_.emplace(device, depth_info);

    const cubey::vulkan::DescriptorSetInfo render_info =
        storage_set_info(3U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    render_a_descriptors_.emplace(device, render_info);
    render_b_descriptors_.emplace(device, render_info);

    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .storage_buffer(reset_descriptors_->set(), 0, initial_depth().handle(),
                        initial_depth().size())
        .storage_buffer(reset_descriptors_->set(), 1, depth_a().handle(), depth_a().size())
        .storage_buffer(reset_descriptors_->set(), 2, depth_b().handle(), depth_b().size())
        .storage_buffer(reset_descriptors_->set(), 3, flux().handle(), flux().size())
        .storage_buffer(reset_descriptors_->set(), 4, velocity().handle(), velocity().size())
        .storage_buffer(reset_descriptors_->set(), 5, ledger().handle(), ledger().size());

    const auto write_flux = [&writes, this](VkDescriptorSet set,
                                            const cubey::vulkan::Buffer& depth) {
        writes.storage_buffer(set, 0, terrain().handle(), terrain().size())
            .storage_buffer(set, 1, source_rate().handle(), source_rate().size())
            .storage_buffer(set, 2, sink_rate().handle(), sink_rate().size())
            .storage_buffer(set, 3, depth.handle(), depth.size())
            .storage_buffer(set, 4, flux().handle(), flux().size())
            .storage_buffer(set, 5, ledger().handle(), ledger().size());
    };
    write_flux(flux_a_descriptors_->set(), depth_a());
    write_flux(flux_b_descriptors_->set(), depth_b());

    const auto write_depth = [this, &writes](VkDescriptorSet set,
                                             const cubey::vulkan::Buffer& source_depth,
                                             const cubey::vulkan::Buffer& destination_depth) {
        writes.storage_buffer(set, 0, source_rate().handle(), source_rate().size())
            .storage_buffer(set, 1, sink_rate().handle(), sink_rate().size())
            .storage_buffer(set, 2, source_depth.handle(), source_depth.size())
            .storage_buffer(set, 3, flux().handle(), flux().size())
            .storage_buffer(set, 4, destination_depth.handle(), destination_depth.size())
            .storage_buffer(set, 5, velocity().handle(), velocity().size());
    };
    write_depth(depth_a_to_b_descriptors_->set(), depth_a(), depth_b());
    write_depth(depth_b_to_a_descriptors_->set(), depth_b(), depth_a());

    const auto write_render = [this, &writes](VkDescriptorSet set,
                                              const cubey::vulkan::Buffer& depth) {
        writes.storage_buffer(set, 0, terrain().handle(), terrain().size())
            .storage_buffer(set, 1, depth.handle(), depth.size())
            .storage_buffer(set, 2, velocity().handle(), velocity().size());
    };
    write_render(render_a_descriptors_->set(), depth_a());
    write_render(render_b_descriptors_->set(), depth_b());
    writes.update(device);
}

void Fluid25DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    emplace_compute_pipeline(reset_pipeline_, device, "fluid_25d_reset.comp.spv",
                             reset_descriptors_->layout());
    emplace_compute_pipeline(flux_pipeline_, device, "fluid_25d_flux.comp.spv",
                             flux_a_descriptors_->layout());
    emplace_compute_pipeline(depth_pipeline_, device, "fluid_25d_depth.comp.spv",
                             depth_a_to_b_descriptors_->layout());
}

void Fluid25DGpuResources::create_render_pipelines(cubey::vulkan::Device& device,
                                                   VkFormat color_format, VkFormat depth_format,
                                                   VkExtent2D extent) {
    const std::array<cubey::render::ShaderStageFile, 2> diagnostic_shader_stages{
        cubey::render::vertex_shader_file(shader_path("fluid_25d.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("fluid_25d_render.frag.spv")),
    };
    const std::array<VkDescriptorSetLayout, 1> layouts{render_a_descriptors_->layout()};
    diagnostic_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                             .extent = extent,
                                             .color_format = color_format,
                                             .shader_stage_files = diagnostic_shader_stages,
                                             .descriptor_set_layouts = layouts,
                                             .material_pass = diagnostic_pass_info(),
                                         });

    const std::array<cubey::render::ShaderStageFile, 2> terrain_shader_stages{
        cubey::render::vertex_shader_file(shader_path("fluid_25d_terrain.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("fluid_25d_terrain.frag.spv")),
    };
    terrain_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                          .extent = extent,
                                          .color_format = color_format,
                                          .depth_format = depth_format,
                                          .shader_stage_files = terrain_shader_stages,
                                          .descriptor_set_layouts = layouts,
                                          .material_pass = terrain_pass_info(),
                                      });

    const std::array<cubey::render::ShaderStageFile, 2> water_shader_stages{
        cubey::render::vertex_shader_file(shader_path("fluid_25d_water.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("fluid_25d_water.frag.spv")),
    };
    water_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                        .extent = extent,
                                        .color_format = color_format,
                                        .depth_format = depth_format,
                                        .shader_stage_files = water_shader_stages,
                                        .descriptor_set_layouts = layouts,
                                        .material_pass = water_pass_info(),
                                    });
}

void Fluid25DGpuResources::destroy_swapchain_resources() {
    water_pipeline_.reset();
    terrain_pipeline_.reset();
    diagnostic_pipeline_.reset();
}

void Fluid25DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    // Pipelines retain pipeline layouts that reference the descriptor layouts;
    // release them before descriptors, then release descriptor-referenced
    // buffers last.
    depth_pipeline_.reset();
    flux_pipeline_.reset();
    reset_pipeline_.reset();
    render_b_descriptors_.reset();
    render_a_descriptors_.reset();
    depth_b_to_a_descriptors_.reset();
    depth_a_to_b_descriptors_.reset();
    flux_b_descriptors_.reset();
    flux_a_descriptors_.reset();
    reset_descriptors_.reset();
    ledger_.reset();
    velocity_.reset();
    flux_.reset();
    depth_b_.reset();
    depth_a_.reset();
    initial_depth_.reset();
    sink_rate_.reset();
    source_rate_.reset();
    terrain_.reset();
    current_depth_is_a_ = true;
}

#define CUBEY_FLUID25D_RESOURCE_ACCESSOR(name, member, label)                                      \
    const cubey::vulkan::Buffer& Fluid25DGpuResources::name() const {                              \
        if (!member.has_value()) {                                                                 \
            throw std::runtime_error("fluid 2.5D " label " is not initialized");                   \
        }                                                                                          \
        return member.value();                                                                     \
    }

CUBEY_FLUID25D_RESOURCE_ACCESSOR(terrain, terrain_, "terrain buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(source_rate, source_rate_, "source-rate buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(sink_rate, sink_rate_, "sink-rate buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(initial_depth, initial_depth_, "initial-depth buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(depth_a, depth_a_, "depth A buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(depth_b, depth_b_, "depth B buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(flux, flux_, "face-flux buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(velocity, velocity_, "velocity buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(ledger, ledger_, "ledger buffer")

#undef CUBEY_FLUID25D_RESOURCE_ACCESSOR

#define CUBEY_FLUID25D_PIPELINE_ACCESSOR(name, member, label)                                      \
    const cubey::render::ComputePipelineResource& Fluid25DGpuResources::name() const {             \
        if (!member.has_value()) {                                                                 \
            throw std::runtime_error("fluid 2.5D " label " is not initialized");                   \
        }                                                                                          \
        return member.value();                                                                     \
    }

CUBEY_FLUID25D_PIPELINE_ACCESSOR(reset_pipeline, reset_pipeline_, "reset pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(flux_pipeline, flux_pipeline_, "flux pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(depth_pipeline, depth_pipeline_, "depth pipeline")

#undef CUBEY_FLUID25D_PIPELINE_ACCESSOR

const cubey::render::GraphicsPipelineResource& Fluid25DGpuResources::diagnostic_pipeline() const {
    if (!diagnostic_pipeline_.has_value()) {
        throw std::runtime_error("fluid 2.5D diagnostic pipeline is not initialized");
    }
    return diagnostic_pipeline_.value();
}

const cubey::render::GraphicsPipelineResource& Fluid25DGpuResources::terrain_pipeline() const {
    if (!terrain_pipeline_.has_value()) {
        throw std::runtime_error("fluid 2.5D terrain pipeline is not initialized");
    }
    return terrain_pipeline_.value();
}

const cubey::render::GraphicsPipelineResource& Fluid25DGpuResources::water_pipeline() const {
    if (!water_pipeline_.has_value()) {
        throw std::runtime_error("fluid 2.5D water pipeline is not initialized");
    }
    return water_pipeline_.value();
}

} // namespace cubey::projects::fluid::fluid_25d
