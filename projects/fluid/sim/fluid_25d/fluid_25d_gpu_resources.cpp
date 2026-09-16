#include "fluid_25d_gpu_resources.h"

#include <array>
#include <cmath>
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
        .size = sizeof(float) * 28U,
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
                                                             const Fluid25DScenarioData& scenario,
                                                             std::uint32_t frame_slot_count) {
    if (terrain_.has_value()) {
        if (solver_ != config.solver) {
            throw std::runtime_error("fluid 2.5D GPU resources cannot change solver in place");
        }
        return;
    }
    if (frame_slot_count == 0U) {
        throw std::runtime_error("fluid 2.5D resources require at least one frame slot");
    }
    solver_ = config.solver;
    create_buffers(gpu, config, scenario);
    profiler_.emplace(device, frame_slot_count, kFluid25DGpuProfilerPassCapacity);
    create_descriptors(device);
    create_compute_pipelines(device);
}

void Fluid25DGpuResources::create_buffers(cubey::ProjectGpuServices& gpu,
                                          const Fluid25DConfig& config,
                                          const Fluid25DScenarioData& scenario) {
    // Keep GPU ingestion at the same boundary as the CPU finite-volume
    // oracle. GPU buffers are otherwise opaque after upload, so reject a
    // malformed configuration or field before it can become a sticky device
    // failure with no useful source context.
    validate_fluid_25d_config(config);
    if (scenario.width != config.grid_width || scenario.height != config.grid_height ||
        scenario.cell_size_m != config.cell_size_m) {
        throw std::runtime_error("fluid 2.5D GPU scenario does not match config");
    }
    const std::size_t cells = fluid_25d_cell_count(config);
    if (scenario.terrain_height_m.size() != cells ||
        scenario.initial_water_depth_m.size() != cells ||
        scenario.source_depth_rate_m_per_s.size() != cells ||
        scenario.sink_depth_rate_m_per_s.size() != cells ||
        scenario.boundary_outflow_face_mask.size() != cells) {
        throw std::runtime_error("fluid 2.5D GPU scenario field sizes do not match config");
    }
    validate_fluid_25d_boundary_outflow_face_mask(config.grid_width, config.grid_height,
                                                  scenario.boundary_outflow_face_mask);
    for (std::size_t index = 0; index < cells; ++index) {
        if (!std::isfinite(scenario.terrain_height_m[index]) ||
            !std::isfinite(scenario.initial_water_depth_m[index]) ||
            scenario.initial_water_depth_m[index] < 0.0F ||
            !std::isfinite(scenario.source_depth_rate_m_per_s[index]) ||
            scenario.source_depth_rate_m_per_s[index] < 0.0F ||
            !std::isfinite(scenario.sink_depth_rate_m_per_s[index]) ||
            scenario.sink_depth_rate_m_per_s[index] < 0.0F) {
            throw std::runtime_error("fluid 2.5D GPU scenario field is invalid");
        }
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
    boundary_outflow_mask_.emplace(upload(scenario.boundary_outflow_face_mask, scalar_usage,
                                          "fluid_25d boundary outflow mask upload"));
    initial_depth_.emplace(
        upload(scenario.initial_water_depth_m, scalar_usage, "fluid_25d initial depth upload"));
    depth_a_.emplace(
        upload(scenario.initial_water_depth_m, scalar_usage, "fluid_25d depth A upload"));
    depth_b_.emplace(
        upload(scenario.initial_water_depth_m, scalar_usage, "fluid_25d depth B upload"));

    const std::vector<Fluid25DVelocityGpu> zero_velocity(cells);
    const std::vector<Fluid25DLedgerGpu> zero_ledger(cells);
    if (config.solver == Fluid25DSolver::VirtualPipes) {
        const std::vector<Fluid25DFluxGpu> zero_flux(cells);
        flux_.emplace(upload(zero_flux, static_buffer_usage(), "fluid_25d face flux upload"));
    } else {
        const std::vector<Fluid25DMomentumGpu> zero_momentum(cells);
        const std::vector<Fluid25DFiniteVolumeStatusGpu> zero_status(1U);
        momentum_a_.emplace(upload(zero_momentum, static_buffer_usage(),
                                   "fluid_25d finite-volume momentum A upload"));
        momentum_b_.emplace(upload(zero_momentum, static_buffer_usage(),
                                   "fluid_25d finite-volume momentum B upload"));
        finite_volume_candidate_velocity_.emplace(
            upload(zero_velocity, static_buffer_usage(),
                   "fluid_25d finite-volume candidate velocity upload"));
        finite_volume_candidate_ledger_delta_.emplace(
            upload(zero_ledger, static_buffer_usage(),
                   "fluid_25d finite-volume candidate ledger delta upload"));
        finite_volume_status_.emplace(
            upload(zero_status, static_buffer_usage(), "fluid_25d finite-volume status upload"));
    }
    velocity_.emplace(upload(zero_velocity, static_buffer_usage(), "fluid_25d velocity upload"));
    ledger_.emplace(upload(zero_ledger, static_buffer_usage(), "fluid_25d ledger upload"));
    current_depth_is_a_ = true;
}

void Fluid25DGpuResources::create_descriptors(cubey::vulkan::Device& device) {
    const cubey::vulkan::DescriptorSetInfo render_info =
        storage_set_info(3U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    render_a_descriptors_.emplace(device, render_info);
    render_b_descriptors_.emplace(device, render_info);

    cubey::vulkan::DescriptorWriteBatch writes;
    if (solver_ == Fluid25DSolver::VirtualPipes) {
        // reset: initial depth, depth A, depth B, flux, velocity, ledger
        const cubey::vulkan::DescriptorSetInfo reset_info =
            storage_set_info(6U, VK_SHADER_STAGE_COMPUTE_BIT);
        reset_descriptors_.emplace(device, reset_info);
        // flux: terrain, source rate, sink rate, source depth, flux, ledger, boundary mask
        const cubey::vulkan::DescriptorSetInfo flux_info =
            storage_set_info(7U, VK_SHADER_STAGE_COMPUTE_BIT);
        flux_a_descriptors_.emplace(device, flux_info);
        flux_b_descriptors_.emplace(device, flux_info);
        // depth: source rate, sink rate, source depth, face flux, destination depth, velocity
        const cubey::vulkan::DescriptorSetInfo depth_info =
            storage_set_info(6U, VK_SHADER_STAGE_COMPUTE_BIT);
        depth_a_to_b_descriptors_.emplace(device, depth_info);
        depth_b_to_a_descriptors_.emplace(device, depth_info);

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
                .storage_buffer(set, 5, ledger().handle(), ledger().size())
                .storage_buffer(set, 6, boundary_outflow_mask().handle(),
                                boundary_outflow_mask().size());
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
    } else {
        // Finite-volume reset: initial depth, depth A/B, momentum A/B, velocity, ledger, status.
        finite_volume_reset_descriptors_.emplace(device,
                                                 storage_set_info(8U, VK_SHADER_STAGE_COMPUTE_BIT));
        // CFL: source depth/momentum, rates, and a persistent status bundle.
        finite_volume_cfl_a_descriptors_.emplace(device,
                                                 storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_cfl_b_descriptors_.emplace(device,
                                                 storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_cfl_finalize_descriptors_.emplace(
            device, storage_set_info(1U, VK_SHADER_STAGE_COMPUTE_BIT));
        // Candidate: terrain, source/sink, boundary, source h/hu/hv, candidate
        // h/hu/hv, candidate velocity and ledger delta, cumulative ledger, status.
        // It must not mutate the common velocity or cumulative ledger.
        finite_volume_candidate_a_to_b_descriptors_.emplace(
            device, storage_set_info(12U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_candidate_b_to_a_descriptors_.emplace(
            device, storage_set_info(12U, VK_SHADER_STAGE_COMPUTE_BIT));
        // Commit: source state, isolated candidate velocity/ledger delta, and
        // shared published state. Candidate h/hu/hv already occupy the inactive
        // ping-pong destination; commit leaves them in place on acceptance or
        // copies source h/hu/hv through after a global rejection.
        finite_volume_commit_a_to_b_descriptors_.emplace(
            device, storage_set_info(9U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_commit_b_to_a_descriptors_.emplace(
            device, storage_set_info(9U, VK_SHADER_STAGE_COMPUTE_BIT));

        writes
            .storage_buffer(finite_volume_reset_descriptors_->set(), 0, initial_depth().handle(),
                            initial_depth().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 1, depth_a().handle(),
                            depth_a().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 2, depth_b().handle(),
                            depth_b().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 3, momentum_a().handle(),
                            momentum_a().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 4, momentum_b().handle(),
                            momentum_b().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 5, velocity().handle(),
                            velocity().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 6, ledger().handle(),
                            ledger().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 7,
                            finite_volume_status().handle(), finite_volume_status().size());
        const auto write_cfl = [this, &writes](VkDescriptorSet set,
                                               const cubey::vulkan::Buffer& source_depth,
                                               const cubey::vulkan::Buffer& source_momentum) {
            writes.storage_buffer(set, 0, source_depth.handle(), source_depth.size())
                .storage_buffer(set, 1, source_momentum.handle(), source_momentum.size())
                .storage_buffer(set, 2, source_rate().handle(), source_rate().size())
                .storage_buffer(set, 3, sink_rate().handle(), sink_rate().size())
                .storage_buffer(set, 4, finite_volume_status().handle(),
                                finite_volume_status().size());
        };
        write_cfl(finite_volume_cfl_a_descriptors_->set(), depth_a(), momentum_a());
        write_cfl(finite_volume_cfl_b_descriptors_->set(), depth_b(), momentum_b());
        writes.storage_buffer(finite_volume_cfl_finalize_descriptors_->set(), 0,
                              finite_volume_status().handle(), finite_volume_status().size());
        const auto write_candidate =
            [this, &writes](VkDescriptorSet set, const cubey::vulkan::Buffer& source_depth,
                            const cubey::vulkan::Buffer& source_momentum,
                            const cubey::vulkan::Buffer& candidate_depth,
                            const cubey::vulkan::Buffer& candidate_momentum) {
                writes.storage_buffer(set, 0, terrain().handle(), terrain().size())
                    .storage_buffer(set, 1, source_rate().handle(), source_rate().size())
                    .storage_buffer(set, 2, sink_rate().handle(), sink_rate().size())
                    .storage_buffer(set, 3, boundary_outflow_mask().handle(),
                                    boundary_outflow_mask().size())
                    .storage_buffer(set, 4, source_depth.handle(), source_depth.size())
                    .storage_buffer(set, 5, source_momentum.handle(), source_momentum.size())
                    .storage_buffer(set, 6, candidate_depth.handle(), candidate_depth.size())
                    .storage_buffer(set, 7, candidate_momentum.handle(), candidate_momentum.size())
                    .storage_buffer(set, 8, finite_volume_candidate_velocity().handle(),
                                    finite_volume_candidate_velocity().size())
                    .storage_buffer(set, 9, ledger().handle(), ledger().size())
                    .storage_buffer(set, 10, finite_volume_candidate_ledger_delta().handle(),
                                    finite_volume_candidate_ledger_delta().size())
                    .storage_buffer(set, 11, finite_volume_status().handle(),
                                    finite_volume_status().size());
            };
        write_candidate(finite_volume_candidate_a_to_b_descriptors_->set(), depth_a(), momentum_a(),
                        depth_b(), momentum_b());
        write_candidate(finite_volume_candidate_b_to_a_descriptors_->set(), depth_b(), momentum_b(),
                        depth_a(), momentum_a());
        const auto write_commit = [this,
                                   &writes](VkDescriptorSet set,
                                            const cubey::vulkan::Buffer& source_depth,
                                            const cubey::vulkan::Buffer& source_momentum,
                                            const cubey::vulkan::Buffer& destination_depth,
                                            const cubey::vulkan::Buffer& destination_momentum) {
            writes.storage_buffer(set, 0, source_depth.handle(), source_depth.size())
                .storage_buffer(set, 1, source_momentum.handle(), source_momentum.size())
                .storage_buffer(set, 2, finite_volume_candidate_velocity().handle(),
                                finite_volume_candidate_velocity().size())
                .storage_buffer(set, 3, ledger().handle(), ledger().size())
                .storage_buffer(set, 4, finite_volume_candidate_ledger_delta().handle(),
                                finite_volume_candidate_ledger_delta().size())
                .storage_buffer(set, 5, finite_volume_status().handle(),
                                finite_volume_status().size())
                .storage_buffer(set, 6, destination_depth.handle(), destination_depth.size())
                .storage_buffer(set, 7, destination_momentum.handle(), destination_momentum.size())
                .storage_buffer(set, 8, velocity().handle(), velocity().size());
        };
        write_commit(finite_volume_commit_a_to_b_descriptors_->set(), depth_a(), momentum_a(),
                     depth_b(), momentum_b());
        write_commit(finite_volume_commit_b_to_a_descriptors_->set(), depth_b(), momentum_b(),
                     depth_a(), momentum_a());
    }

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
    if (solver_ == Fluid25DSolver::VirtualPipes) {
        emplace_compute_pipeline(reset_pipeline_, device, "fluid_25d_reset.comp.spv",
                                 reset_descriptors_->layout());
        emplace_compute_pipeline(flux_pipeline_, device, "fluid_25d_flux.comp.spv",
                                 flux_a_descriptors_->layout());
        emplace_compute_pipeline(depth_pipeline_, device, "fluid_25d_depth.comp.spv",
                                 depth_a_to_b_descriptors_->layout());
    } else {
        emplace_compute_pipeline(finite_volume_reset_pipeline_, device,
                                 "fluid_25d_fv_reset.comp.spv",
                                 finite_volume_reset_descriptors_->layout());
        emplace_compute_pipeline(finite_volume_cfl_pipeline_, device, "fluid_25d_fv_cfl.comp.spv",
                                 finite_volume_cfl_a_descriptors_->layout());
        emplace_compute_pipeline(finite_volume_cfl_finalize_pipeline_, device,
                                 "fluid_25d_fv_cfl_finalize.comp.spv",
                                 finite_volume_cfl_finalize_descriptors_->layout());
        emplace_compute_pipeline(finite_volume_candidate_pipeline_, device,
                                 "fluid_25d_fv_update.comp.spv",
                                 finite_volume_candidate_a_to_b_descriptors_->layout());
        emplace_compute_pipeline(finite_volume_commit_pipeline_, device,
                                 "fluid_25d_fv_commit.comp.spv",
                                 finite_volume_commit_a_to_b_descriptors_->layout());
    }
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
    finite_volume_commit_pipeline_.reset();
    finite_volume_candidate_pipeline_.reset();
    finite_volume_cfl_finalize_pipeline_.reset();
    finite_volume_cfl_pipeline_.reset();
    finite_volume_reset_pipeline_.reset();
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
    finite_volume_commit_b_to_a_descriptors_.reset();
    finite_volume_commit_a_to_b_descriptors_.reset();
    finite_volume_candidate_b_to_a_descriptors_.reset();
    finite_volume_candidate_a_to_b_descriptors_.reset();
    finite_volume_cfl_b_descriptors_.reset();
    finite_volume_cfl_a_descriptors_.reset();
    finite_volume_cfl_finalize_descriptors_.reset();
    finite_volume_reset_descriptors_.reset();
    profiler_.reset();
    ledger_.reset();
    velocity_.reset();
    flux_.reset();
    finite_volume_status_.reset();
    finite_volume_candidate_ledger_delta_.reset();
    finite_volume_candidate_velocity_.reset();
    momentum_b_.reset();
    momentum_a_.reset();
    depth_b_.reset();
    depth_a_.reset();
    initial_depth_.reset();
    boundary_outflow_mask_.reset();
    sink_rate_.reset();
    source_rate_.reset();
    terrain_.reset();
    current_depth_is_a_ = true;
    solver_ = Fluid25DSolver::VirtualPipes;
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
CUBEY_FLUID25D_RESOURCE_ACCESSOR(boundary_outflow_mask, boundary_outflow_mask_,
                                 "boundary-outflow-mask buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(initial_depth, initial_depth_, "initial-depth buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(depth_a, depth_a_, "depth A buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(depth_b, depth_b_, "depth B buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(flux, flux_, "face-flux buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(momentum_a, momentum_a_, "finite-volume momentum A buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(momentum_b, momentum_b_, "finite-volume momentum B buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(finite_volume_candidate_velocity,
                                 finite_volume_candidate_velocity_,
                                 "finite-volume candidate velocity buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(finite_volume_candidate_ledger_delta,
                                 finite_volume_candidate_ledger_delta_,
                                 "finite-volume candidate ledger delta buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(finite_volume_status, finite_volume_status_,
                                 "finite-volume status buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(velocity, velocity_, "velocity buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(ledger, ledger_, "ledger buffer")

#undef CUBEY_FLUID25D_RESOURCE_ACCESSOR

const std::vector<cubey::vulkan::GpuPassTiming>& Fluid25DGpuResources::latest_timings() const {
    static const std::vector<cubey::vulkan::GpuPassTiming> kEmptyTimings;
    if (!profiler_.has_value()) {
        return kEmptyTimings;
    }
    return profiler_->latest_timings();
}

#define CUBEY_FLUID25D_PIPELINE_ACCESSOR(name, member, label)                                      \
    const cubey::render::ComputePipelineResource& Fluid25DGpuResources::name() const {             \
        if (!member.has_value()) {                                                                 \
            throw std::runtime_error("fluid 2.5D " label " is not initialized");                   \
        }                                                                                          \
        return member.value();                                                                     \
    }

const cubey::render::ComputePipelineResource& Fluid25DGpuResources::reset_pipeline() const {
    const auto& selected =
        solver_ == Fluid25DSolver::FiniteVolume ? finite_volume_reset_pipeline_ : reset_pipeline_;
    if (!selected.has_value()) {
        throw std::runtime_error("fluid 2.5D reset pipeline is not initialized");
    }
    return selected.value();
}

CUBEY_FLUID25D_PIPELINE_ACCESSOR(flux_pipeline, flux_pipeline_, "flux pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(depth_pipeline, depth_pipeline_, "depth pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(finite_volume_cfl_pipeline, finite_volume_cfl_pipeline_,
                                 "finite-volume CFL pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(finite_volume_cfl_finalize_pipeline,
                                 finite_volume_cfl_finalize_pipeline_,
                                 "finite-volume CFL finalize pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(finite_volume_candidate_pipeline,
                                 finite_volume_candidate_pipeline_,
                                 "finite-volume candidate pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(finite_volume_commit_pipeline, finite_volume_commit_pipeline_,
                                 "finite-volume commit pipeline")

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
