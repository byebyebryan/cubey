#include "smoke_2d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/memory_barriers.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::projects::fluid::smoke_2d {
namespace {

struct RenderPushConstants {
    std::array<float, 4> grid_debug{};
};

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_time{};
    std::array<float, 4> decay_options{};
    std::array<float, 4> solver_options{};
    std::array<float, 4> tuning_options{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);
static_assert(sizeof(SimulationPushConstants) ==
              sizeof(float) * kSmoke2DSimulationPushConstantFloatCount);

using DispatchGroups = cubey::render::ComputeDispatchGroups;

[[nodiscard]] DispatchGroups compute_dispatch_groups(const Smoke2DConfig& config) {
    return cubey::render::ceil_dispatch_groups(config.grid_width, config.grid_height,
                                               kSmoke2DComputeGroupSize);
}

void record_injector_buffer_update(VkCommandBuffer command_buffer,
                                   const Smoke2DGpuResources& resources,
                                   std::span<const Smoke2DInjectorGpu> injectors) {
    if (injectors.empty()) {
        return;
    }
    const VkDeviceSize byte_size =
        static_cast<VkDeviceSize>(injectors.size() * sizeof(Smoke2DInjectorGpu));
    if (byte_size > resources.injectors().size()) {
        throw std::runtime_error("smoke injector update exceeds injector buffer size");
    }
    vkCmdUpdateBuffer(command_buffer, resources.injectors().handle(), 0, byte_size,
                      injectors.data());
    cubey::vulkan::record_transfer_write_barrier(
        command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

[[nodiscard]] float debug_view_push_value(Smoke2DDebugView view) {
    return static_cast<float>(static_cast<std::uint32_t>(view));
}

[[nodiscard]] SimulationPushConstants simulation_push_constants(const Smoke2DConfig& config,
                                                                const ProjectFrame& frame) {
    const float time = static_cast<float>(frame.elapsed_seconds);
    const float dt = std::min(static_cast<float>(frame.delta_seconds), config.fixed_delta_seconds);

    return {
        .grid_dt_time =
            {
                static_cast<float>(config.grid_width),
                static_cast<float>(config.grid_height),
                dt,
                time,
            },
        .decay_options =
            {
                config.dye_decay_per_second,
                config.velocity_decay_per_second,
                0.0F,
                0.0F,
            },
        .solver_options =
            {
                config.vorticity_strength,
                config.injector_injection_radius,
                config.injector_injection_strength,
                static_cast<float>(config.procedural_injector_count),
            },
        .tuning_options =
            {
                config.advection_strength,
                config.low_energy_cleanup_strength,
                config.low_energy_cleanup_start,
                config.low_energy_cleanup_end,
            },
    };
}

void record_field_reset(VkCommandBuffer command_buffer, const Smoke2DGpuResources& resources) {
    vkCmdFillBuffer(command_buffer, resources.field_a().handle(), 0, resources.field_a().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.field_b().handle(), 0, resources.field_b().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.field_temp().handle(), 0,
                    resources.field_temp().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.divergence().handle(), 0,
                    resources.divergence().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.curl().handle(), 0, resources.curl().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.pressure_a().handle(), 0,
                    resources.pressure_a().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.pressure_b().handle(), 0,
                    resources.pressure_b().size(), 0);
    cubey::vulkan::record_transfer_write_barrier(
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set, const DispatchGroups& groups,
                     const SimulationPushConstants& push_constants) {
    cubey::render::record_compute_pipeline_dispatch(
        recorder, cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set, groups),
        VK_SHADER_STAGE_COMPUTE_BIT, push_constants);
}

void record_profiled_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                              const cubey::render::ComputePipelineResource& pipeline,
                              VkDescriptorSet descriptor_set, const DispatchGroups& groups,
                              const SimulationPushConstants& push_constants,
                              cubey::vulkan::GpuTimestampProfiler* profiler,
                              std::uint32_t frame_slot_index, const char* label) {
    cubey::render::record_profiled_compute_pipeline_dispatch(
        recorder, cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set, groups),
        VK_SHADER_STAGE_COMPUTE_BIT, push_constants, profiler, frame_slot_index, label);
}

void record_compute_barrier(VkCommandBuffer command_buffer) {
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
}

void record_render_visibility_barrier(VkCommandBuffer command_buffer) {
    cubey::vulkan::record_compute_render_shader_write_barrier(command_buffer);
}

} // namespace

void record_smoke_compute(VkCommandBuffer command_buffer, Smoke2DGpuResources& resources,
                          const Smoke2DConfig& config, bool paused, bool& reset_requested,
                          const ProjectFrame& frame, std::span<const Smoke2DInjectorGpu> injectors,
                          bool include_render_visibility_barrier,
                          cubey::vulkan::GpuTimestampProfiler* profiler,
                          std::uint32_t frame_slot_index) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);

    if (reset_requested) {
        cubey::vulkan::GpuTimestampScope profile_scope(profiler, command_buffer, frame_slot_index,
                                                       "reset");
        record_field_reset(command_buffer, resources);
        reset_requested = false;
    }
    if (paused) {
        return;
    }

    record_injector_buffer_update(command_buffer, resources, injectors);

    SimulationPushConstants push_constants = simulation_push_constants(config, frame);
    const DispatchGroups groups = compute_dispatch_groups(config);

    const cubey::render::ComputePipelineResource& advect_pipeline =
        resources.advect_pipeline_resource();
    record_profiled_dispatch(recorder, advect_pipeline, resources.advect_descriptor_set(), groups,
                             push_constants, profiler, frame_slot_index, "advect_predict");

    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& advect_correct_pipeline =
        resources.advect_correct_pipeline_resource();
    record_profiled_dispatch(recorder, advect_correct_pipeline,
                             resources.advect_correct_descriptor_set(), groups, push_constants,
                             profiler, frame_slot_index, "advect_correct");

    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& inject_pipeline =
        resources.inject_pipeline_resource();
    record_profiled_dispatch(recorder, inject_pipeline, resources.inject_descriptor_set(), groups,
                             push_constants, profiler, frame_slot_index, "inject");

    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& curl_pipeline =
        resources.curl_pipeline_resource();
    record_profiled_dispatch(recorder, curl_pipeline, resources.curl_descriptor_set(), groups,
                             push_constants, profiler, frame_slot_index, "curl");

    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& vorticity_pipeline =
        resources.vorticity_pipeline_resource();
    record_profiled_dispatch(recorder, vorticity_pipeline, resources.vorticity_descriptor_set(),
                             groups, push_constants, profiler, frame_slot_index, "vorticity");

    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& divergence_pipeline =
        resources.divergence_pipeline_resource();
    record_profiled_dispatch(recorder, divergence_pipeline, resources.divergence_descriptor_set(),
                             groups, push_constants, profiler, frame_slot_index, "divergence");

    record_compute_barrier(command_buffer);

    cubey::vulkan::GpuTimestampScope pressure_scope(profiler, command_buffer, frame_slot_index,
                                                    "pressure");
    if (config.pressure_solver == Smoke2DPressureSolver::RedBlackGaussSeidel) {
        const cubey::render::ComputePipelineResource& pressure_pipeline =
            resources.pressure_rbgs_pipeline_resource();
        for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
            for (std::uint32_t parity = 0; parity < 2U; ++parity) {
                push_constants.decay_options[2] = static_cast<float>(parity);
                record_dispatch(recorder, pressure_pipeline,
                                resources.pressure_rbgs_descriptor_set(), groups, push_constants);
                record_compute_barrier(command_buffer);
            }
        }
    } else {
        const cubey::render::ComputePipelineResource& pressure_pipeline =
            resources.pressure_pipeline_resource();
        for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
            const VkDescriptorSet descriptor_set = (iteration % 2U == 0)
                                                       ? resources.pressure_a_to_b_descriptor_set()
                                                       : resources.pressure_b_to_a_descriptor_set();
            record_dispatch(recorder, pressure_pipeline, descriptor_set, groups, push_constants);
            record_compute_barrier(command_buffer);
        }
    }
    pressure_scope.end();

    const bool final_pressure_is_a =
        config.pressure_solver == Smoke2DPressureSolver::RedBlackGaussSeidel ||
        (config.pressure_iterations % 2U) == 0;
    const VkDescriptorSet projection_descriptor_set =
        final_pressure_is_a ? resources.projection_pressure_a_descriptor_set()
                            : resources.projection_pressure_b_descriptor_set();
    const cubey::render::ComputePipelineResource& projection_pipeline =
        resources.projection_pipeline_resource();
    record_profiled_dispatch(recorder, projection_pipeline, projection_descriptor_set, groups,
                             push_constants, profiler, frame_slot_index, "projection");

    if (include_render_visibility_barrier) {
        record_render_visibility_barrier(command_buffer);
    }
}

void record_fullscreen_draw(VkCommandBuffer command_buffer, const Smoke2DGpuResources& resources,
                            const Smoke2DConfig& config, Smoke2DDebugView debug_view,
                            cubey::render::ColorTargetView color_target,
                            cubey::vulkan::GpuTimestampProfiler* profiler,
                            std::uint32_t frame_slot_index) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const RenderPushConstants push_constants{
        .grid_debug =
            {
                static_cast<float>(config.grid_width),
                static_cast<float>(config.grid_height),
                debug_view_push_value(debug_view),
                config.pressure_solver == Smoke2DPressureSolver::RedBlackGaussSeidel ||
                        (config.pressure_iterations % 2U) == 0
                    ? 0.0F
                    : 1.0F,
            },
    };

    cubey::vulkan::GpuTimestampScope profile_scope(profiler, command_buffer, frame_slot_index,
                                                   "render");
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.008F, 0.014F, 1.0F),
        },
        [&resources, push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &resources.render_pipeline_resource(),
                    .descriptor_set = resources.render_descriptors().set(),
                },
                VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

[[nodiscard]] cubey::render::CompiledRenderGraph build_smoke_frame_graph(
    cubey::render::ColorTargetView color_target, Smoke2DGpuResources& resources,
    const Smoke2DConfig& config, Smoke2DDebugView debug_view, bool paused, bool& reset_requested,
    const ProjectFrame& frame, std::span<const Smoke2DInjectorGpu> injectors,
    cubey::vulkan::GpuTimestampProfiler* profiler, std::uint32_t frame_slot_index) {
    Smoke2DGpuResources* resource_ptr = &resources;
    const Smoke2DConfig* config_ptr = &config;
    bool* reset_requested_ptr = &reset_requested;
    std::vector<Smoke2DInjectorGpu> injector_snapshot(injectors.begin(), injectors.end());
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle field_a =
        graph.import_buffer({.label = "smoke field A", .byte_size = resources.field_a().size()},
                            resources.field_a().handle());
    const cubey::render::RenderGraphBufferHandle field_b =
        graph.import_buffer({.label = "smoke field B", .byte_size = resources.field_b().size()},
                            resources.field_b().handle());
    const cubey::render::RenderGraphBufferHandle field_temp = graph.import_buffer(
        {.label = "smoke field temp", .byte_size = resources.field_temp().size()},
        resources.field_temp().handle());
    const cubey::render::RenderGraphBufferHandle divergence = graph.import_buffer(
        {.label = "fluid divergence", .byte_size = resources.divergence().size()},
        resources.divergence().handle());
    const cubey::render::RenderGraphBufferHandle curl = graph.import_buffer(
        {.label = "fluid curl", .byte_size = resources.curl().size()}, resources.curl().handle());
    const cubey::render::RenderGraphBufferHandle injector_buffer =
        graph.import_buffer({.label = "smoke injectors", .byte_size = resources.injectors().size()},
                            resources.injectors().handle());
    const cubey::render::RenderGraphBufferHandle pressure_a = graph.import_buffer(
        {.label = "fluid pressure A", .byte_size = resources.pressure_a().size()},
        resources.pressure_a().handle());
    const cubey::render::RenderGraphBufferHandle pressure_b = graph.import_buffer(
        {.label = "fluid pressure B", .byte_size = resources.pressure_b().size()},
        resources.pressure_b().handle());
    const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "backbuffer", color_target, cubey::render::render_graph_undefined_texture_state(),
        cubey::render::render_graph_present_texture_state());

    graph.add_pass("fluid simulation", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(field_a)
        .read_write_storage_buffer(field_b)
        .read_write_storage_buffer(field_temp)
        .read_write_storage_buffer(divergence)
        .read_write_storage_buffer(curl)
        .read_write_storage_buffer(injector_buffer)
        .read_write_storage_buffer(pressure_a)
        .read_write_storage_buffer(pressure_b)
        .execute([resource_ptr, config_ptr, paused, reset_requested_ptr, frame, profiler,
                  frame_slot_index,
                  injector_snapshot](const cubey::render::RenderGraphExecutionContext& context) {
            record_smoke_compute(context.recorder().handle(), *resource_ptr, *config_ptr, paused,
                                 *reset_requested_ptr, frame, injector_snapshot, false, profiler,
                                 frame_slot_index);
        });
    graph.add_pass("fluid render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(field_a)
        .read_storage_buffer(divergence)
        .read_storage_buffer(curl)
        .read_storage_buffer(pressure_a)
        .read_storage_buffer(pressure_b)
        .write_color(backbuffer)
        .execute([resource_ptr, config_ptr, debug_view, backbuffer, profiler, frame_slot_index,
                  color_target](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder& recorder = context.recorder();
            const cubey::render::RenderGraphResolvedTexture resolved =
                context.resolved_texture(backbuffer);
            record_fullscreen_draw(recorder.handle(), *resource_ptr, *config_ptr, debug_view,
                                   cubey::render::color_target_view(color_target.extent,
                                                                    color_target.format,
                                                                    resolved.image, resolved.view),
                                   profiler, frame_slot_index);
        });

    return graph.compile();
}

} // namespace cubey::projects::fluid::smoke_2d
