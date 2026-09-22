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

inline constexpr VkDeviceSize kSimulationPushConstantBytes = sizeof(float) * 12U;

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

[[nodiscard]] cubey::render::MaterialPassInfo quiver_pass_info() {
    cubey::render::MaterialPassInfo pass = terrain_pass_info();
    pass.label = "fluid_25d.catchment_quiver";
    pass.depth_write = false;
    pass.blend_enable = true;
    // Flow Inspection quiver arrows write premultiplied source-over color.
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
    const std::vector<Fluid25DTracerLedgerGpu> zero_tracer_ledger(cells);
    const std::vector<float> zero_tracer_q(cells);
    const std::vector<float> zero_presentation_cue(cells);
    const std::vector<Fluid25DFiniteVolumeStatusGpu> zero_presentation_status(1U);
    Fluid25DEndpointMarkersGpu endpoint_markers{};
    endpoint_markers.source_xy_outlet_xy.fill(-1.0F);
    if (fluid_25d_is_source_outlet_demo(config.scenario) &&
        scenario.source_cell != kFluid25DNoCell && scenario.sink_cell != kFluid25DNoCell) {
        const auto cell_xy = [width = config.grid_width](std::size_t index) {
            return std::array<float, 2>{static_cast<float>(index % width),
                                        static_cast<float>(index / width)};
        };
        const std::array<float, 2> source_xy = cell_xy(scenario.source_cell);
        const std::array<float, 2> outlet_xy = cell_xy(scenario.sink_cell);
        endpoint_markers.source_xy_outlet_xy = {source_xy[0], source_xy[1], outlet_xy[0],
                                                outlet_xy[1]};
    }
    std::vector<Fluid25DQuiverGpu> inactive_quiver(
        fluid_25d_quiver_count(config.grid_width, config.grid_height));
    for (Fluid25DQuiverGpu& arrow : inactive_quiver) {
        // A failed status-gated reset must remain visibly inert rather than
        // rendering the value-initialized origin as a stack of false marks.
        arrow.direction_xy_strength_opacity[3] = 0.0F;
    }
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
        finite_volume_candidate_tracer_ledger_delta_.emplace(
            upload(zero_tracer_ledger, static_buffer_usage(),
                   "fluid_25d finite-volume candidate tracer ledger delta upload"));
        finite_volume_status_.emplace(
            upload(zero_status, static_buffer_usage(), "fluid_25d finite-volume status upload"));
    }
    velocity_.emplace(upload(zero_velocity, static_buffer_usage(), "fluid_25d velocity upload"));
    ledger_.emplace(upload(zero_ledger, static_buffer_usage(), "fluid_25d ledger upload"));
    tracer_q_a_.emplace(upload(zero_tracer_q, static_buffer_usage(),
                               "fluid_25d tracer q A upload"));
    tracer_q_b_.emplace(upload(zero_tracer_q, static_buffer_usage(),
                               "fluid_25d tracer q B upload"));
    tracer_ledger_.emplace(upload(zero_tracer_ledger, static_buffer_usage(),
                                  "fluid_25d tracer ledger upload"));
    presentation_cue_a_.emplace(upload(zero_presentation_cue, static_buffer_usage(),
                                       "fluid_25d presentation cue A upload"));
    presentation_cue_b_.emplace(upload(zero_presentation_cue, static_buffer_usage(),
                                       "fluid_25d presentation cue B upload"));
    presentation_cue_virtual_status_.emplace(upload(zero_presentation_status, static_buffer_usage(),
                                                    "fluid_25d presentation cue status upload"));
    endpoint_markers_.emplace(upload(std::vector<Fluid25DEndpointMarkersGpu>{endpoint_markers},
                                     static_buffer_usage(),
                                     "fluid_25d source outlet markers"));
    quiver_.emplace(upload(inactive_quiver, static_buffer_usage(),
                           "fluid_25d flow inspection quiver"));
    current_depth_is_a_ = true;
    presentation_cue_parity_.reset();
}

void Fluid25DGpuResources::create_descriptors(cubey::vulkan::Device& device) {
    // Binding 6 reserves current-parity q=h*c for Transport Inspection. The
    // existing shaders deliberately do not consume it until that presentation
    // slice lands, but both solvers bind a valid buffer today.
    const cubey::vulkan::DescriptorSetInfo render_info =
        storage_set_info(7U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    render_a_descriptors_.emplace(device, render_info);
    render_b_descriptors_.emplace(device, render_info);

    presentation_cue_reset_descriptors_.emplace(device,
                                                storage_set_info(2U, VK_SHADER_STAGE_COMPUTE_BIT));
    presentation_cue_depth_a_a_to_b_descriptors_.emplace(
        device, storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
    presentation_cue_depth_a_b_to_a_descriptors_.emplace(
        device, storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
    presentation_cue_depth_b_a_to_b_descriptors_.emplace(
        device, storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
    presentation_cue_depth_b_b_to_a_descriptors_.emplace(
        device, storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
    // Reset/update sample the parity-selected published depth/velocity so
    // Flow Inspection can become useful while paused without a host readback
    // or a solver-state write.
    quiver_reset_a_descriptors_.emplace(device,
                                        storage_set_info(4U, VK_SHADER_STAGE_COMPUTE_BIT));
    quiver_reset_b_descriptors_.emplace(device,
                                        storage_set_info(4U, VK_SHADER_STAGE_COMPUTE_BIT));
    quiver_update_a_descriptors_.emplace(
        device, storage_set_info(4U, VK_SHADER_STAGE_COMPUTE_BIT));
    quiver_update_b_descriptors_.emplace(
        device, storage_set_info(4U, VK_SHADER_STAGE_COMPUTE_BIT));
    quiver_render_a_descriptors_.emplace(
        device, storage_set_info(4U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
    quiver_render_b_descriptors_.emplace(
        device, storage_set_info(4U, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));

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
        // Finite-volume reset: initial depth, depth A/B, momentum A/B,
        // velocity, water ledger, status, q A/B, and tracer ledger.
        finite_volume_reset_descriptors_.emplace(device,
                                                 storage_set_info(11U, VK_SHADER_STAGE_COMPUTE_BIT));
        // CFL: source depth/momentum, rates, and a persistent status bundle.
        finite_volume_cfl_a_descriptors_.emplace(device,
                                                 storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_cfl_b_descriptors_.emplace(device,
                                                 storage_set_info(5U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_cfl_finalize_descriptors_.emplace(
            device, storage_set_info(1U, VK_SHADER_STAGE_COMPUTE_BIT));
        // Candidate: terrain, source/sink, boundary, source h/hu/hv, candidate
        // h/hu/hv/q, candidate velocity and isolated water/tracer ledger
        // deltas, cumulative water/tracer ledgers, and status.
        // It must not mutate the common velocity or cumulative ledger.
        finite_volume_candidate_a_to_b_descriptors_.emplace(
            device, storage_set_info(16U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_candidate_b_to_a_descriptors_.emplace(
            device, storage_set_info(16U, VK_SHADER_STAGE_COMPUTE_BIT));
        // Commit: source state, isolated candidate velocity/ledger delta, and
        // shared published state. Candidate h/hu/hv already occupy the inactive
        // ping-pong destination; commit leaves them in place on acceptance or
        // copies source h/hu/hv through after a global rejection.
        finite_volume_commit_a_to_b_descriptors_.emplace(
            device, storage_set_info(13U, VK_SHADER_STAGE_COMPUTE_BIT));
        finite_volume_commit_b_to_a_descriptors_.emplace(
            device, storage_set_info(13U, VK_SHADER_STAGE_COMPUTE_BIT));

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
                            finite_volume_status().handle(), finite_volume_status().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 8, tracer_q_a().handle(),
                            tracer_q_a().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 9, tracer_q_b().handle(),
                            tracer_q_b().size())
            .storage_buffer(finite_volume_reset_descriptors_->set(), 10, tracer_ledger().handle(),
                            tracer_ledger().size());
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
                            const cubey::vulkan::Buffer& candidate_momentum,
                            const cubey::vulkan::Buffer& source_tracer_q,
                            const cubey::vulkan::Buffer& candidate_tracer_q) {
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
                                    finite_volume_status().size())
                    .storage_buffer(set, 12, source_tracer_q.handle(), source_tracer_q.size())
                    .storage_buffer(set, 13, candidate_tracer_q.handle(),
                                    candidate_tracer_q.size())
                    .storage_buffer(set, 14, tracer_ledger().handle(), tracer_ledger().size())
                    .storage_buffer(set, 15,
                                    finite_volume_candidate_tracer_ledger_delta().handle(),
                                    finite_volume_candidate_tracer_ledger_delta().size());
            };
        write_candidate(finite_volume_candidate_a_to_b_descriptors_->set(), depth_a(), momentum_a(),
                        depth_b(), momentum_b(), tracer_q_a(), tracer_q_b());
        write_candidate(finite_volume_candidate_b_to_a_descriptors_->set(), depth_b(), momentum_b(),
                        depth_a(), momentum_a(), tracer_q_b(), tracer_q_a());
        const auto write_commit = [this,
                                   &writes](VkDescriptorSet set,
                                            const cubey::vulkan::Buffer& source_depth,
                                            const cubey::vulkan::Buffer& source_momentum,
                                            const cubey::vulkan::Buffer& destination_depth,
                                            const cubey::vulkan::Buffer& destination_momentum,
                                            const cubey::vulkan::Buffer& source_tracer_q,
                                            const cubey::vulkan::Buffer& destination_tracer_q) {
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
                .storage_buffer(set, 8, velocity().handle(), velocity().size())
                .storage_buffer(set, 9, source_tracer_q.handle(), source_tracer_q.size())
                .storage_buffer(set, 10, destination_tracer_q.handle(),
                                destination_tracer_q.size())
                .storage_buffer(set, 11, tracer_ledger().handle(), tracer_ledger().size())
                .storage_buffer(set, 12,
                                finite_volume_candidate_tracer_ledger_delta().handle(),
                                finite_volume_candidate_tracer_ledger_delta().size());
        };
        write_commit(finite_volume_commit_a_to_b_descriptors_->set(), depth_a(), momentum_a(),
                     depth_b(), momentum_b(), tracer_q_a(), tracer_q_b());
        write_commit(finite_volume_commit_b_to_a_descriptors_->set(), depth_b(), momentum_b(),
                     depth_a(), momentum_a(), tracer_q_b(), tracer_q_a());
    }

    const auto write_render = [this, &writes](VkDescriptorSet set,
                                              const cubey::vulkan::Buffer& depth,
                                              const cubey::vulkan::Buffer& tracer_q) {
        writes.storage_buffer(set, 0, terrain().handle(), terrain().size())
            .storage_buffer(set, 1, depth.handle(), depth.size())
            .storage_buffer(set, 2, velocity().handle(), velocity().size())
            .storage_buffer(set, 3, presentation_cue_a().handle(), presentation_cue_a().size())
            .storage_buffer(set, 4, presentation_cue_b().handle(), presentation_cue_b().size())
            .storage_buffer(set, 5, endpoint_markers().handle(), endpoint_markers().size())
            .storage_buffer(set, 6, tracer_q.handle(), tracer_q.size());
    };
    write_render(render_a_descriptors_->set(), depth_a(), tracer_q_a());
    write_render(render_b_descriptors_->set(), depth_b(), tracer_q_b());

    writes
        .storage_buffer(presentation_cue_reset_descriptors_->set(), 0,
                        presentation_cue_a().handle(), presentation_cue_a().size())
        .storage_buffer(presentation_cue_reset_descriptors_->set(), 1,
                        presentation_cue_b().handle(), presentation_cue_b().size());
    const cubey::vulkan::Buffer& cue_status = presentation_cue_status();
    const auto write_presentation_cue_advection =
        [this, &writes, &cue_status](VkDescriptorSet set, const cubey::vulkan::Buffer& depth,
                                     const cubey::vulkan::Buffer& source_cue,
                                     const cubey::vulkan::Buffer& destination_cue) {
            writes.storage_buffer(set, 0, depth.handle(), depth.size())
                .storage_buffer(set, 1, velocity().handle(), velocity().size())
                .storage_buffer(set, 2, source_cue.handle(), source_cue.size())
                .storage_buffer(set, 3, destination_cue.handle(), destination_cue.size())
                .storage_buffer(set, 4, cue_status.handle(), cue_status.size());
        };
    write_presentation_cue_advection(presentation_cue_depth_a_a_to_b_descriptors_->set(), depth_a(),
                                     presentation_cue_a(), presentation_cue_b());
    write_presentation_cue_advection(presentation_cue_depth_a_b_to_a_descriptors_->set(), depth_a(),
                                     presentation_cue_b(), presentation_cue_a());
    write_presentation_cue_advection(presentation_cue_depth_b_a_to_b_descriptors_->set(), depth_b(),
                                     presentation_cue_a(), presentation_cue_b());
    write_presentation_cue_advection(presentation_cue_depth_b_b_to_a_descriptors_->set(), depth_b(),
                                     presentation_cue_b(), presentation_cue_a());

    const cubey::vulkan::Buffer& quiver_status = presentation_cue_status();
    const auto write_quiver_reset = [this, &writes, &quiver_status](VkDescriptorSet set,
                                                                    const cubey::vulkan::Buffer& depth) {
        writes.storage_buffer(set, 0, quiver().handle(), quiver().size())
            .storage_buffer(set, 1, quiver_status.handle(), quiver_status.size())
            .storage_buffer(set, 2, depth.handle(), depth.size())
            .storage_buffer(set, 3, velocity().handle(), velocity().size());
    };
    write_quiver_reset(quiver_reset_a_descriptors_->set(), depth_a());
    write_quiver_reset(quiver_reset_b_descriptors_->set(), depth_b());
    const auto write_quiver_update = [this, &writes, &quiver_status](
                                         VkDescriptorSet set,
                                         const cubey::vulkan::Buffer& depth) {
            writes.storage_buffer(set, 0, depth.handle(), depth.size())
                .storage_buffer(set, 1, velocity().handle(), velocity().size())
                .storage_buffer(set, 2, quiver_status.handle(), quiver_status.size())
                .storage_buffer(set, 3, quiver().handle(), quiver().size());
        };
    write_quiver_update(quiver_update_a_descriptors_->set(), depth_a());
    write_quiver_update(quiver_update_b_descriptors_->set(), depth_b());
    const auto write_quiver_render =
        [this, &writes](VkDescriptorSet set, const cubey::vulkan::Buffer& depth) {
            writes.storage_buffer(set, 0, terrain().handle(), terrain().size())
                .storage_buffer(set, 1, depth.handle(), depth.size())
                .storage_buffer(set, 2, velocity().handle(), velocity().size())
                .storage_buffer(set, 3, quiver().handle(), quiver().size());
        };
    write_quiver_render(quiver_render_a_descriptors_->set(), depth_a());
    write_quiver_render(quiver_render_b_descriptors_->set(), depth_b());
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
    emplace_compute_pipeline(presentation_cue_reset_pipeline_, device,
                             "fluid_25d_presentation_cue_reset.comp.spv",
                             presentation_cue_reset_descriptors_->layout());
    emplace_compute_pipeline(presentation_cue_advection_pipeline_, device,
                             "fluid_25d_presentation_cue_advect.comp.spv",
                             presentation_cue_depth_a_a_to_b_descriptors_->layout());
    emplace_compute_pipeline(quiver_reset_pipeline_, device, "fluid_25d_quiver_reset.comp.spv",
                             quiver_reset_a_descriptors_->layout());
    emplace_compute_pipeline(quiver_update_pipeline_, device,
                             "fluid_25d_quiver_update.comp.spv",
                             quiver_update_a_descriptors_->layout());
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

    const std::array<cubey::render::ShaderStageFile, 2> quiver_shader_stages{
        cubey::render::vertex_shader_file(shader_path("fluid_25d_quiver.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("fluid_25d_quiver.frag.spv")),
    };
    const std::array<VkDescriptorSetLayout, 1> quiver_layouts{quiver_render_a_descriptors_->layout()};
    quiver_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                            .extent = extent,
                                            .color_format = color_format,
                                            .depth_format = depth_format,
                                            .shader_stage_files = quiver_shader_stages,
                                            .descriptor_set_layouts = quiver_layouts,
                                            .material_pass = quiver_pass_info(),
                                        });
}

void Fluid25DGpuResources::destroy_swapchain_resources() {
    quiver_pipeline_.reset();
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
    quiver_update_pipeline_.reset();
    quiver_reset_pipeline_.reset();
    presentation_cue_advection_pipeline_.reset();
    presentation_cue_reset_pipeline_.reset();
    depth_pipeline_.reset();
    flux_pipeline_.reset();
    reset_pipeline_.reset();
    presentation_cue_depth_b_b_to_a_descriptors_.reset();
    presentation_cue_depth_b_a_to_b_descriptors_.reset();
    presentation_cue_depth_a_b_to_a_descriptors_.reset();
    presentation_cue_depth_a_a_to_b_descriptors_.reset();
    presentation_cue_reset_descriptors_.reset();
    quiver_render_b_descriptors_.reset();
    quiver_render_a_descriptors_.reset();
    quiver_update_b_descriptors_.reset();
    quiver_update_a_descriptors_.reset();
    quiver_reset_b_descriptors_.reset();
    quiver_reset_a_descriptors_.reset();
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
    presentation_cue_virtual_status_.reset();
    presentation_cue_b_.reset();
    presentation_cue_a_.reset();
    endpoint_markers_.reset();
    quiver_.reset();
    tracer_ledger_.reset();
    ledger_.reset();
    velocity_.reset();
    flux_.reset();
    finite_volume_status_.reset();
    finite_volume_candidate_tracer_ledger_delta_.reset();
    finite_volume_candidate_ledger_delta_.reset();
    finite_volume_candidate_velocity_.reset();
    momentum_b_.reset();
    momentum_a_.reset();
    depth_b_.reset();
    depth_a_.reset();
    tracer_q_b_.reset();
    tracer_q_a_.reset();
    initial_depth_.reset();
    boundary_outflow_mask_.reset();
    sink_rate_.reset();
    source_rate_.reset();
    terrain_.reset();
    current_depth_is_a_ = true;
    presentation_cue_parity_.reset();
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
CUBEY_FLUID25D_RESOURCE_ACCESSOR(tracer_q_a, tracer_q_a_, "tracer q A buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(tracer_q_b, tracer_q_b_, "tracer q B buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(flux, flux_, "face-flux buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(momentum_a, momentum_a_, "finite-volume momentum A buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(momentum_b, momentum_b_, "finite-volume momentum B buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(finite_volume_candidate_velocity,
                                 finite_volume_candidate_velocity_,
                                 "finite-volume candidate velocity buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(finite_volume_candidate_ledger_delta,
                                 finite_volume_candidate_ledger_delta_,
                                 "finite-volume candidate ledger delta buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(finite_volume_candidate_tracer_ledger_delta,
                                 finite_volume_candidate_tracer_ledger_delta_,
                                 "finite-volume candidate tracer ledger delta buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(finite_volume_status, finite_volume_status_,
                                 "finite-volume status buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(velocity, velocity_, "velocity buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(ledger, ledger_, "ledger buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(tracer_ledger, tracer_ledger_, "tracer ledger buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(presentation_cue_a, presentation_cue_a_,
                                 "presentation cue A buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(presentation_cue_b, presentation_cue_b_,
                                 "presentation cue B buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(endpoint_markers, endpoint_markers_, "endpoint marker buffer")
CUBEY_FLUID25D_RESOURCE_ACCESSOR(quiver, quiver_, "flow inspection quiver buffer")

#undef CUBEY_FLUID25D_RESOURCE_ACCESSOR

const cubey::vulkan::Buffer& Fluid25DGpuResources::presentation_cue_status() const {
    if (solver_ == Fluid25DSolver::FiniteVolume) {
        return finite_volume_status();
    }
    if (!presentation_cue_virtual_status_.has_value()) {
        throw std::runtime_error("fluid 2.5D presentation cue status buffer is not initialized");
    }
    return presentation_cue_virtual_status_.value();
}

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
CUBEY_FLUID25D_PIPELINE_ACCESSOR(presentation_cue_reset_pipeline, presentation_cue_reset_pipeline_,
                                 "presentation cue reset pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(presentation_cue_advection_pipeline,
                                 presentation_cue_advection_pipeline_,
                                 "presentation cue advection pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(quiver_reset_pipeline, quiver_reset_pipeline_,
                                 "flow inspection quiver reset pipeline")
CUBEY_FLUID25D_PIPELINE_ACCESSOR(quiver_update_pipeline, quiver_update_pipeline_,
                                 "flow inspection quiver update pipeline")

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

const cubey::render::GraphicsPipelineResource& Fluid25DGpuResources::quiver_pipeline() const {
    if (!quiver_pipeline_.has_value()) {
        throw std::runtime_error("fluid 2.5D flow inspection quiver pipeline is not initialized");
    }
    return quiver_pipeline_.value();
}

} // namespace cubey::projects::fluid::fluid_25d
