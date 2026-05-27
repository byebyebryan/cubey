#include "water_2d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/memory_barriers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cubey::projects::fluid::water_2d {
namespace {

struct RenderPushConstants {
    std::array<float, 4> grid_debug{};
    std::array<float, 4> particle_options{};
    std::array<float, 4> surface_options{};
    std::array<float, 4> foam_options{};
    std::array<float, 4> style_options{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * kWater2DRenderPushConstantFloatCount);

using DispatchGroups = cubey::render::ComputeDispatchGroups;

enum class SurfaceTextureSource {
    Raw,
    A,
    B,
};

struct SurfaceTextureSlot {
    cubey::render::RenderGraphTextureHandle handle{};
    SurfaceTextureSource source = SurfaceTextureSource::Raw;
};

[[nodiscard]] DispatchGroups compute_dispatch_groups(std::uint32_t width, std::uint32_t height) {
    return cubey::render::ceil_dispatch_groups(width, height, kWater2DComputeGroupSize);
}

[[nodiscard]] DispatchGroups linear_dispatch_groups(std::size_t count,
                                                    std::uint32_t group_size = 64U) {
    return cubey::render::linear_dispatch_groups(count, group_size);
}

[[nodiscard]] DispatchGroups cell_dispatch_groups(const Water2DConfig& config) {
    return compute_dispatch_groups(config.grid_width, config.grid_height);
}

[[nodiscard]] DispatchGroups face_dispatch_groups(const Water2DConfig& config) {
    return compute_dispatch_groups(config.grid_width + 1U, config.grid_height + 1U);
}

[[nodiscard]] std::uint32_t particle_scan_count(const Water2DConfig& config,
                                                const Water2DRuntimeState& state) {
    return water_2d_runtime_particle_scan_count(config, state);
}

[[nodiscard]] DispatchGroups particle_scan_dispatch_groups(const Water2DConfig& config,
                                                           const Water2DRuntimeState& state) {
    return linear_dispatch_groups(particle_scan_count(config, state));
}

[[nodiscard]] DispatchGroups bin_dispatch_groups(const Water2DConfig& config) {
    return linear_dispatch_groups(std::max(cell_count(config), particle_bin_index_count(config)));
}

[[nodiscard]] DispatchGroups reset_dispatch_groups(const Water2DConfig& config) {
    return linear_dispatch_groups(
        std::max({static_cast<std::size_t>(config.particle_capacity), cell_count(config),
                  u_face_count(config), v_face_count(config), particle_bin_index_count(config)}));
}

[[nodiscard]] DispatchGroups
diagnostics_workload_dispatch_groups(const Water2DConfig& config,
                                     const Water2DRuntimeState& runtime_state) {
    return linear_dispatch_groups(std::max(
        cell_count(config), static_cast<std::size_t>(particle_scan_count(config, runtime_state))));
}

[[nodiscard]] bool should_record_diagnostics_for_frame(const Water2DConfig& config,
                                                       const ProjectFrame& frame) {
    if (!config.profile_diagnostics || config.profile_diagnostic_interval == 0U) {
        return false;
    }
    return (frame.frame_index % config.profile_diagnostic_interval) == 0U;
}

[[nodiscard]] float debug_view_push_value(Water2DDebugView view) {
    return static_cast<float>(static_cast<std::uint32_t>(view));
}

[[nodiscard]] float degrees_to_radians(float degrees) {
    constexpr float kPi = 3.14159265358979323846F;
    return degrees * (kPi / 180.0F);
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
surface_color_texture_desc(const char* label, VkExtent2D extent, VkFormat format) {
    return {
        .label = label,
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState
target_initial_state(Water2DRenderTargetMode target_mode) {
    return target_mode == Water2DRenderTargetMode::Present
               ? cubey::render::render_graph_undefined_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] cubey::render::RenderGraphTextureState
target_final_state(Water2DRenderTargetMode target_mode) {
    return target_mode == Water2DRenderTargetMode::Present
               ? cubey::render::render_graph_present_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] RenderPushConstants render_push_constants(const Water2DConfig& config,
                                                        const Water2DRuntimeState& runtime_state,
                                                        Water2DDebugView debug_view,
                                                        float smooth_axis = 0.0F,
                                                        float style_time = 0.0F) {
    return {
        .grid_debug =
            {
                water_2d_shader_count_float(config.grid_width,
                                            "water grid width exceeds exact shader integer range"),
                water_2d_shader_count_float(config.grid_height,
                                            "water grid height exceeds exact shader integer range"),
                debug_view_push_value(debug_view),
                runtime_state.pressure_read_b ? 1.0F : 0.0F,
            },
        .particle_options =
            {
                water_2d_shader_count_float(
                    particle_scan_count(config, runtime_state),
                    "water particle scan count exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    config.max_particles_per_cell,
                    "water max particles per cell exceeds exact shader integer range"),
                config.particle_radius,
                config.surface_splat_radius_scale,
            },
        .surface_options =
            {
                config.surface_threshold,
                config.edge_strength,
                config.foam_strength,
                config.surface_density_scale,
            },
        .foam_options =
            {
                config.foam_sharpness,
                config.foam_breakup,
                config.surface_smoothing_radius_px,
                smooth_axis,
            },
        .style_options =
            {
                config.surface_refraction_strength,
                config.surface_caustic_strength,
                config.surface_specular_strength,
                style_time,
            },
    };
}

[[nodiscard]] VkDescriptorSet surface_source_descriptor_set(const Water2DGpuResources& resources,
                                                            cubey::render::FrameSlot frame_slot,
                                                            SurfaceTextureSource source) {
    switch (source) {
    case SurfaceTextureSource::Raw:
        return resources.surface_source_raw_descriptor_set(frame_slot);
    case SurfaceTextureSource::A:
        return resources.surface_source_a_descriptor_set(frame_slot);
    case SurfaceTextureSource::B:
        return resources.surface_source_b_descriptor_set(frame_slot);
    }
    return resources.surface_source_raw_descriptor_set(frame_slot);
}

[[nodiscard]] Water2DSimulationUniforms simulation_uniforms(const Water2DConfig& config) {
    const float hose_angle = degrees_to_radians(config.hose.angle_degrees);
    const float hose_spread = degrees_to_radians(config.hose.spread_degrees);
    return {
        .grid_options =
            {
                water_2d_shader_count_float(config.grid_width,
                                            "water grid width exceeds exact shader integer range"),
                water_2d_shader_count_float(config.grid_height,
                                            "water grid height exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    config.active_particle_count,
                    "water active particle count exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    config.particle_capacity,
                    "water particle capacity exceeds exact shader integer range"),
            },
        .init_options =
            {
                config.initial_fill_height,
                config.initial_fill_width,
                config.gravity,
                static_cast<float>(static_cast<std::uint32_t>(config.scenario)),
            },
        .obstacle_options =
            {
                config.obstacle_center[0],
                config.obstacle_center[1],
                config.obstacle_radius,
                static_cast<float>(static_cast<std::uint32_t>(config.obstacle_shape)),
            },
        .obstacle_extents =
            {
                config.obstacle_half_size[0],
                config.obstacle_half_size[1],
                config.boundary_restitution,
                config.obstacle_friction,
            },
        .particle_options =
            {
                water_2d_shader_count_float(
                    config.active_particle_count,
                    "water active particle count exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    config.max_particles_per_cell,
                    "water max particles per cell exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    config.particles_per_cell,
                    "water particles per cell exceeds exact shader integer range"),
                config.flip_ratio,
            },
        .solve_options =
            {
                config.particle_separation_radius,
                config.velocity_limit,
                config.particle_damping,
                config.particle_separation_strength,
            },
        .lifecycle_options =
            {
                water_2d_shader_count_float(
                    config.particle_capacity,
                    "water particle capacity exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    hose_particle_start_for_config(config),
                    "water hose particle start exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    hose_particle_pool_capacity_for_config(config),
                    "water hose particle pool exceeds exact shader integer range"),
                static_cast<float>(static_cast<std::uint32_t>(config.transfer_mode)),
            },
        .hose_options0 =
            {
                config.hose.enabled ? 1.0F : 0.0F,
                config.hose.position[0],
                config.hose.position[1],
                config.hose.radius,
            },
        .hose_options1 =
            {
                std::cos(hose_angle),
                std::sin(hose_angle),
                config.hose.speed,
                hose_spread,
            },
        .hose_options2 =
            {
                config.hose.particles_per_second,
                config.particle_volume_strength,
                0.0F,
                0.0F,
            },
        .drain_options =
            {
                config.drain.enabled ? 1.0F : 0.0F,
                config.drain.center[0],
                config.drain.center[1],
                config.drain.pull_speed,
            },
        .drain_extents =
            {
                config.drain.half_size[0],
                config.drain.half_size[1],
                config.drain.pull_radius,
                0.0F,
            },
        .wave_options0 =
            {
                config.wave.enabled ? 1.0F : 0.0F,
                config.wave.center[0],
                config.wave.center[1],
                config.wave.amplitude,
            },
        .wave_options1 =
            {
                config.wave.half_size[0],
                config.wave.half_size[1],
                config.wave.frequency_hz,
                0.0F,
            },
    };
}

[[nodiscard]] Water2DDispatchPushConstants
dispatch_push_constants(const ProjectFrame& frame, float delta_seconds, float pressure_read_b,
                        std::uint32_t particle_scan_count, std::uint32_t emit_cursor = 0,
                        std::uint32_t emit_count = 0) {
    return {
        .dispatch_options =
            {
                delta_seconds,
                static_cast<float>(frame.elapsed_seconds),
                pressure_read_b,
                water_2d_shader_count_float(
                    particle_scan_count,
                    "water particle scan count exceeds exact shader integer range"),
            },
        .emit_options =
            {
                water_2d_shader_count_float(
                    emit_cursor, "water hose emit cursor exceeds exact shader integer range"),
                water_2d_shader_count_float(
                    emit_count, "water hose emit count exceeds exact shader integer range"),
                0.0F,
                0.0F,
            },
    };
}

[[nodiscard]] std::uint32_t next_hose_emit_count(const Water2DConfig& config,
                                                 Water2DRuntimeState& state, float delta_seconds) {
    const std::uint32_t hose_pool_capacity = hose_particle_pool_capacity_for_config(config);
    if (!config.hose.enabled || hose_pool_capacity == 0 ||
        config.hose.particles_per_second <= 0.0F) {
        state.hose_emit_accumulator = 0.0F;
        return 0;
    }
    state.hose_emit_accumulator += config.hose.particles_per_second * delta_seconds;
    const auto emit_count = static_cast<std::uint32_t>(std::floor(state.hose_emit_accumulator));
    const std::uint32_t clamped_count = std::min(emit_count, hose_pool_capacity);
    state.hose_emit_accumulator -= static_cast<float>(clamped_count);
    return clamped_count;
}

void note_hose_emission(Water2DRuntimeState& state, const Water2DConfig& config,
                        std::uint32_t emit_cursor, std::uint32_t emit_count) {
    const std::uint32_t hose_pool_capacity = hose_particle_pool_capacity_for_config(config);
    if (emit_count == 0 || hose_pool_capacity == 0) {
        return;
    }
    const std::uint32_t pool_start = hose_particle_start_for_config(config);
    const std::uint32_t touched_count = std::min(emit_count, hose_pool_capacity);
    const std::uint32_t end_cursor = emit_cursor + touched_count;
    const std::uint32_t touched_high =
        end_cursor <= hose_pool_capacity ? pool_start + end_cursor : config.particle_capacity;
    state.particle_scan_count = std::max(particle_scan_count(config, state),
                                         std::min(touched_high, config.particle_capacity));
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set, DispatchGroups groups,
                     const Water2DDispatchPushConstants& push_constants) {
    cubey::render::record_compute_pipeline_dispatch(
        recorder, cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set, groups),
        VK_SHADER_STAGE_COMPUTE_BIT, push_constants);
}

void record_compute_barrier(VkCommandBuffer command_buffer) {
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
}

void record_final_barrier(VkCommandBuffer command_buffer) {
    cubey::vulkan::record_compute_render_shader_write_barrier(command_buffer);
}

void record_refresh_bins(const cubey::vulkan::CommandRecorder& recorder,
                         VkCommandBuffer command_buffer, Water2DGpuResources& resources,
                         VkDescriptorSet descriptor_set, const Water2DConfig& config,
                         const Water2DRuntimeState& runtime_state,
                         const Water2DDispatchPushConstants& push_constants) {
    record_dispatch(recorder, resources.clear_bins_pipeline_resource(), descriptor_set,
                    bin_dispatch_groups(config), push_constants);
    record_compute_barrier(command_buffer);
    record_dispatch(recorder, resources.build_bins_pipeline_resource(), descriptor_set,
                    particle_scan_dispatch_groups(config, runtime_state), push_constants);
    record_compute_barrier(command_buffer);
}

void record_diagnostics_pass(const cubey::vulkan::CommandRecorder& recorder,
                             VkCommandBuffer command_buffer, Water2DGpuResources& resources,
                             VkDescriptorSet descriptor_set,
                             Water2DDispatchPushConstants push_constants, float mode,
                             DispatchGroups groups) {
    push_constants.dispatch_options[2] = mode;
    record_dispatch(recorder, resources.diagnostics_pipeline_resource(), descriptor_set, groups,
                    push_constants);
    record_compute_barrier(command_buffer);
}

} // namespace

void record_water_2d_compute(VkCommandBuffer command_buffer, Water2DGpuResources& resources,
                             const Water2DConfig& config, Water2DRuntimeState& runtime_state,
                             cubey::render::FrameSlot frame_slot, bool paused,
                             bool& reset_requested, const ProjectFrame& frame,
                             bool include_render_visibility_barrier) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    resources.upload_simulation_uniforms(frame_slot, simulation_uniforms(config));
    const VkDescriptorSet descriptor_set = resources.field_descriptor_set(frame_slot);
    const DispatchGroups cell_groups = cell_dispatch_groups(config);
    const DispatchGroups face_groups = face_dispatch_groups(config);
    const std::uint32_t substep_count = std::max(1U, config.substeps);
    const float frame_dt =
        std::min(static_cast<float>(frame.delta_seconds), config.fixed_delta_seconds);
    const float substep_dt = frame_dt / static_cast<float>(substep_count);
    Water2DDispatchPushConstants push_constants = dispatch_push_constants(
        frame, substep_dt, 0.0F, particle_scan_count(config, runtime_state));
    const bool record_diagnostics = should_record_diagnostics_for_frame(config, frame);

    if (reset_requested) {
        runtime_state = {};
        record_dispatch(recorder, resources.reset_pipeline_resource(), descriptor_set,
                        reset_dispatch_groups(config), push_constants);
        record_compute_barrier(command_buffer);
        runtime_state.particle_scan_count = config.active_particle_count;
        push_constants.dispatch_options[3] = water_2d_shader_count_float(
            particle_scan_count(config, runtime_state),
            "water particle scan count exceeds exact shader integer range");
        reset_requested = false;
    }
    if (paused) {
        record_refresh_bins(recorder, command_buffer, resources, descriptor_set, config,
                            runtime_state, push_constants);
        if (record_diagnostics) {
            record_diagnostics_pass(recorder, command_buffer, resources, descriptor_set,
                                    push_constants, kWater2DDiagnosticsModeClear,
                                    linear_dispatch_groups(kWater2DDiagnosticSlotCount));
            record_diagnostics_pass(recorder, command_buffer, resources, descriptor_set,
                                    push_constants, kWater2DDiagnosticsModeWorkload,
                                    diagnostics_workload_dispatch_groups(config, runtime_state));
        }
        if (include_render_visibility_barrier) {
            record_final_barrier(command_buffer);
        }
        return;
    }

    for (std::uint32_t substep = 0; substep < substep_count; ++substep) {
        const float substep_time =
            static_cast<float>(frame.elapsed_seconds) + (static_cast<float>(substep) * substep_dt);
        push_constants = dispatch_push_constants(frame, substep_dt, 0.0F,
                                                 particle_scan_count(config, runtime_state));
        push_constants.dispatch_options[1] = substep_time;

        record_dispatch(recorder, resources.clear_grid_pipeline_resource(), descriptor_set,
                        face_groups, push_constants);
        record_compute_barrier(command_buffer);

        const std::uint32_t emit_count = next_hose_emit_count(config, runtime_state, substep_dt);
        if (emit_count > 0) {
            const std::uint32_t emit_cursor = runtime_state.hose_cursor;
            const std::uint32_t hose_pool_capacity = hose_particle_pool_capacity_for_config(config);
            runtime_state.hose_cursor =
                (runtime_state.hose_cursor + emit_count) % hose_pool_capacity;
            note_hose_emission(runtime_state, config, emit_cursor, emit_count);
            Water2DDispatchPushConstants emit_push_constants = dispatch_push_constants(
                frame, substep_dt, 0.0F, particle_scan_count(config, runtime_state), emit_cursor,
                emit_count);
            emit_push_constants.dispatch_options[1] = substep_time;
            record_dispatch(recorder, resources.emit_pipeline_resource(), descriptor_set,
                            linear_dispatch_groups(emit_count), emit_push_constants);
            record_compute_barrier(command_buffer);
        }
        push_constants.dispatch_options[3] = water_2d_shader_count_float(
            particle_scan_count(config, runtime_state),
            "water particle scan count exceeds exact shader integer range");

        record_refresh_bins(recorder, command_buffer, resources, descriptor_set, config,
                            runtime_state, push_constants);

        record_dispatch(recorder, resources.particle_to_grid_pipeline_resource(), descriptor_set,
                        face_groups, push_constants);
        record_compute_barrier(command_buffer);

        record_dispatch(recorder, resources.force_pipeline_resource(), descriptor_set, face_groups,
                        push_constants);
        record_compute_barrier(command_buffer);

        record_dispatch(recorder, resources.divergence_pipeline_resource(), descriptor_set,
                        cell_groups, push_constants);
        record_compute_barrier(command_buffer);

        const cubey::render::ComputePipelineResource& pressure_pipeline =
            resources.pressure_pipeline_resource();
        for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
            push_constants.dispatch_options[2] = (iteration % 2U == 1U) ? 1.0F : 0.0F;
            record_dispatch(recorder, pressure_pipeline, descriptor_set, cell_groups,
                            push_constants);
            record_compute_barrier(command_buffer);
        }

        const bool final_pressure_is_b = (config.pressure_iterations % 2U) == 1U;
        runtime_state.pressure_read_b = final_pressure_is_b;
        push_constants.dispatch_options[2] = final_pressure_is_b ? 1.0F : 0.0F;
        record_dispatch(recorder, resources.projection_pipeline_resource(), descriptor_set,
                        face_groups, push_constants);
        record_compute_barrier(command_buffer);

        record_dispatch(recorder, resources.grid_to_particle_pipeline_resource(), descriptor_set,
                        particle_scan_dispatch_groups(config, runtime_state), push_constants);
        record_compute_barrier(command_buffer);

        record_dispatch(recorder, resources.advect_particles_pipeline_resource(), descriptor_set,
                        particle_scan_dispatch_groups(config, runtime_state), push_constants);
        record_compute_barrier(command_buffer);

        record_refresh_bins(recorder, command_buffer, resources, descriptor_set, config,
                            runtime_state, push_constants);
    }

    if (record_diagnostics) {
        record_diagnostics_pass(recorder, command_buffer, resources, descriptor_set, push_constants,
                                kWater2DDiagnosticsModeClear,
                                linear_dispatch_groups(kWater2DDiagnosticSlotCount));
        record_diagnostics_pass(recorder, command_buffer, resources, descriptor_set, push_constants,
                                kWater2DDiagnosticsModeWorkload,
                                diagnostics_workload_dispatch_groups(config, runtime_state));
    }

    if (include_render_visibility_barrier) {
        record_final_barrier(command_buffer);
    }
}

void record_water_2d_draw(VkCommandBuffer command_buffer, const Water2DGpuResources& resources,
                          const Water2DConfig& config, cubey::render::FrameSlot frame_slot,
                          const Water2DRuntimeState& runtime_state, Water2DDebugView debug_view,
                          cubey::render::ColorTargetView color_target) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const RenderPushConstants push_constants =
        render_push_constants(config, runtime_state, debug_view);

    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.009F, 0.014F, 1.0F),
        },
        [&resources, frame_slot,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &resources.render_pipeline_resource(),
                    .descriptor_set = resources.field_descriptor_set(frame_slot),
                },
                VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

void record_surface_density_pass(const cubey::vulkan::CommandRecorder& recorder,
                                 const Water2DGpuResources& resources, const Water2DConfig& config,
                                 cubey::render::FrameSlot frame_slot,
                                 const Water2DRuntimeState& runtime_state,
                                 cubey::render::ColorTargetView color_target) {
    const RenderPushConstants push_constants =
        render_push_constants(config, runtime_state, Water2DDebugView::Surface);
    const std::uint32_t instance_count = particle_scan_count(config, runtime_state);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 0.0F),
        },
        [&resources, frame_slot, instance_count,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline =
                resources.surface_density_pipeline_resource();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                                              resources.field_descriptor_set(frame_slot));
            pass_recorder.push_constants(pipeline.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, push_constants);
            pass_recorder.draw(6, std::max(1U, instance_count));
        });
}

void record_surface_smooth_pass(const cubey::vulkan::CommandRecorder& recorder,
                                const Water2DGpuResources& resources, const Water2DConfig& config,
                                const Water2DRuntimeState& runtime_state,
                                cubey::render::FrameSlot frame_slot, SurfaceTextureSource source,
                                float smooth_axis, cubey::render::ColorTargetView color_target) {
    const RenderPushConstants push_constants =
        render_push_constants(config, runtime_state, Water2DDebugView::Surface, smooth_axis);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 0.0F),
        },
        [&resources, frame_slot, source,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &resources.surface_smooth_pipeline_resource(),
                    .descriptor_set = surface_source_descriptor_set(resources, frame_slot, source),
                },
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

void record_surface_composite_pass(const cubey::vulkan::CommandRecorder& recorder,
                                   const Water2DGpuResources& resources,
                                   const Water2DConfig& config, cubey::render::FrameSlot frame_slot,
                                   const Water2DRuntimeState& runtime_state, float elapsed_seconds,
                                   cubey::render::ColorTargetView color_target) {
    const RenderPushConstants push_constants = render_push_constants(
        config, runtime_state, Water2DDebugView::Surface, 0.0F, elapsed_seconds);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.009F, 0.014F, 1.0F),
        },
        [&resources, frame_slot,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline =
                resources.surface_composite_pipeline_resource();
            const std::array<VkDescriptorSet, 2> sets{
                resources.surface_composite_descriptor_set(frame_slot),
                resources.field_descriptor_set(frame_slot),
            };
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(),
                                               0, sets);
            pass_recorder.push_constants(pipeline.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, push_constants);
            cubey::render::record_fullscreen_triangle(pass_recorder);
        });
}

[[nodiscard]] Water2DFrameGraph
build_water_2d_frame_graph(cubey::render::ColorTargetView color_target,
                           Water2DGpuResources& resources, const Water2DConfig& config,
                           Water2DRuntimeState& runtime_state, cubey::render::FrameSlot frame_slot,
                           Water2DDebugView debug_view, bool paused, bool& reset_requested,
                           const ProjectFrame& frame, Water2DRenderTargetMode target_mode,
                           bool include_simulation) {
    Water2DGpuResources* resource_ptr = &resources;
    const Water2DConfig* config_ptr = &config;
    bool* reset_requested_ptr = &reset_requested;
    Water2DRuntimeState* runtime_state_ptr = &runtime_state;
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle simulation_uniforms =
        graph.import_buffer({.label = "water simulation uniforms",
                             .byte_size = resources.simulation_uniform_buffer(frame_slot).size()},
                            resources.simulation_uniform_buffer(frame_slot).handle());
    const cubey::render::RenderGraphBufferHandle particle_positions = graph.import_buffer(
        {.label = "water particle positions", .byte_size = resources.particle_positions().size()},
        resources.particle_positions().handle());
    const cubey::render::RenderGraphBufferHandle particle_velocities = graph.import_buffer(
        {.label = "water particle velocities", .byte_size = resources.particle_velocities().size()},
        resources.particle_velocities().handle());
    const cubey::render::RenderGraphBufferHandle particle_affine = graph.import_buffer(
        {.label = "water particle affine", .byte_size = resources.particle_affine().size()},
        resources.particle_affine().handle());
    const cubey::render::RenderGraphBufferHandle u = graph.import_buffer(
        {.label = "water U", .byte_size = resources.u().size()}, resources.u().handle());
    const cubey::render::RenderGraphBufferHandle u_previous = graph.import_buffer(
        {.label = "water previous U", .byte_size = resources.u_previous().size()},
        resources.u_previous().handle());
    const cubey::render::RenderGraphBufferHandle v = graph.import_buffer(
        {.label = "water V", .byte_size = resources.v().size()}, resources.v().handle());
    const cubey::render::RenderGraphBufferHandle v_previous = graph.import_buffer(
        {.label = "water previous V", .byte_size = resources.v_previous().size()},
        resources.v_previous().handle());
    const cubey::render::RenderGraphBufferHandle u_weight =
        graph.import_buffer({.label = "water U weight", .byte_size = resources.u_weight().size()},
                            resources.u_weight().handle());
    const cubey::render::RenderGraphBufferHandle v_weight =
        graph.import_buffer({.label = "water V weight", .byte_size = resources.v_weight().size()},
                            resources.v_weight().handle());
    const cubey::render::RenderGraphBufferHandle pressure_a = graph.import_buffer(
        {.label = "water pressure A", .byte_size = resources.pressure_a().size()},
        resources.pressure_a().handle());
    const cubey::render::RenderGraphBufferHandle pressure_b = graph.import_buffer(
        {.label = "water pressure B", .byte_size = resources.pressure_b().size()},
        resources.pressure_b().handle());
    const cubey::render::RenderGraphBufferHandle divergence = graph.import_buffer(
        {.label = "water divergence", .byte_size = resources.divergence().size()},
        resources.divergence().handle());
    const cubey::render::RenderGraphBufferHandle solid =
        graph.import_buffer({.label = "water solid", .byte_size = resources.solid().size()},
                            resources.solid().handle());
    const cubey::render::RenderGraphBufferHandle cell_counts = graph.import_buffer(
        {.label = "water cell counts", .byte_size = resources.cell_counts().size()},
        resources.cell_counts().handle());
    const cubey::render::RenderGraphBufferHandle cell_particle_indices =
        graph.import_buffer({.label = "water cell particle indices",
                             .byte_size = resources.cell_particle_indices().size()},
                            resources.cell_particle_indices().handle());
    const cubey::render::RenderGraphBufferHandle diagnostics = graph.import_buffer(
        {.label = "water diagnostics", .byte_size = resources.diagnostics().size()},
        resources.diagnostics().handle());
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("backbuffer", color_target, target_initial_state(target_mode),
                                  target_final_state(target_mode));

    if (include_simulation) {
        graph.add_pass("water simulation", cubey::render::RenderGraphQueueDomain::Compute)
            .read_write_storage_buffer(particle_positions)
            .read_uniform_buffer(simulation_uniforms)
            .read_write_storage_buffer(particle_velocities)
            .read_write_storage_buffer(particle_affine)
            .read_write_storage_buffer(u)
            .read_write_storage_buffer(u_previous)
            .read_write_storage_buffer(v)
            .read_write_storage_buffer(v_previous)
            .read_write_storage_buffer(u_weight)
            .read_write_storage_buffer(v_weight)
            .read_write_storage_buffer(pressure_a)
            .read_write_storage_buffer(pressure_b)
            .read_write_storage_buffer(divergence)
            .read_write_storage_buffer(solid)
            .read_write_storage_buffer(cell_counts)
            .read_write_storage_buffer(cell_particle_indices)
            .read_write_storage_buffer(diagnostics)
            .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, paused,
                      reset_requested_ptr,
                      frame](const cubey::render::RenderGraphExecutionContext& context) {
                record_water_2d_compute(context.recorder().handle(), *resource_ptr, *config_ptr,
                                        *runtime_state_ptr, frame_slot, paused,
                                        *reset_requested_ptr, frame, false);
            });
    }

    if (debug_view == Water2DDebugView::Surface) {
        const std::uint32_t smoothing_iterations =
            std::min(config.surface_smoothing_iterations, std::uint32_t{8});
        const cubey::render::RenderGraphTextureHandle raw_density =
            graph.create_texture(surface_color_texture_desc(
                "water surface raw density", color_target.extent, VK_FORMAT_R32_SFLOAT));
        const cubey::render::RenderGraphTextureHandle surface_a =
            smoothing_iterations > 0
                ? graph.create_texture(surface_color_texture_desc(
                      "water surface density A", color_target.extent, VK_FORMAT_R32_SFLOAT))
                : raw_density;
        const cubey::render::RenderGraphTextureHandle surface_b =
            smoothing_iterations > 0
                ? graph.create_texture(surface_color_texture_desc(
                      "water surface density B", color_target.extent, VK_FORMAT_R32_SFLOAT))
                : raw_density;

        graph.add_pass("water surface density", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_storage_buffer(particle_positions)
            .write_color(raw_density)
            .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot,
                      raw_density](const cubey::render::RenderGraphExecutionContext& context) {
                record_surface_density_pass(
                    context.recorder(), *resource_ptr, *config_ptr, frame_slot, *runtime_state_ptr,
                    cubey::render::resolved_color_target_view(context, raw_density));
            });

        SurfaceTextureSlot current_surface{
            .handle = raw_density,
            .source = SurfaceTextureSource::Raw,
        };
        SurfaceTextureSlot next_surface{
            .handle = surface_a,
            .source = SurfaceTextureSource::A,
        };
        for (std::uint32_t iteration = 0; iteration < smoothing_iterations; ++iteration) {
            graph
                .add_pass("water surface smooth x", cubey::render::RenderGraphQueueDomain::Graphics)
                .read_texture(current_surface.handle)
                .write_color(next_surface.handle)
                .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot,
                          source = current_surface, destination = next_surface](
                             const cubey::render::RenderGraphExecutionContext& context) {
                    record_surface_smooth_pass(
                        context.recorder(), *resource_ptr, *config_ptr, *runtime_state_ptr,
                        frame_slot, source.source, 0.0F,
                        cubey::render::resolved_color_target_view(context, destination.handle));
                });
            std::swap(current_surface, next_surface);
            next_surface =
                current_surface.source == SurfaceTextureSource::A
                    ? SurfaceTextureSlot{.handle = surface_b, .source = SurfaceTextureSource::B}
                    : SurfaceTextureSlot{.handle = surface_a, .source = SurfaceTextureSource::A};

            graph
                .add_pass("water surface smooth y", cubey::render::RenderGraphQueueDomain::Graphics)
                .read_texture(current_surface.handle)
                .write_color(next_surface.handle)
                .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot,
                          source = current_surface, destination = next_surface](
                             const cubey::render::RenderGraphExecutionContext& context) {
                    record_surface_smooth_pass(
                        context.recorder(), *resource_ptr, *config_ptr, *runtime_state_ptr,
                        frame_slot, source.source, 1.0F,
                        cubey::render::resolved_color_target_view(context, destination.handle));
                });
            std::swap(current_surface, next_surface);
            next_surface =
                current_surface.source == SurfaceTextureSource::A
                    ? SurfaceTextureSlot{.handle = surface_b, .source = SurfaceTextureSource::B}
                    : SurfaceTextureSlot{.handle = surface_a, .source = SurfaceTextureSource::A};
        }

        graph.add_pass("water surface composite", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(current_surface.handle)
            .read_storage_buffer(u)
            .read_storage_buffer(v)
            .read_storage_buffer(solid)
            .write_color(backbuffer)
            .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot,
                      elapsed_seconds = static_cast<float>(frame.elapsed_seconds),
                      backbuffer](const cubey::render::RenderGraphExecutionContext& context) {
                record_surface_composite_pass(
                    context.recorder(), *resource_ptr, *config_ptr, frame_slot, *runtime_state_ptr,
                    elapsed_seconds,
                    cubey::render::resolved_color_target_view(context, backbuffer));
            });

        return {
            .graph = graph.compile(),
            .uses_surface_textures = true,
            .raw_density = raw_density,
            .surface_a = surface_a,
            .surface_b = surface_b,
            .final_surface = current_surface.handle,
        };
    }

    graph.add_pass("water render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(particle_positions)
        .read_storage_buffer(u)
        .read_storage_buffer(v)
        .read_storage_buffer(pressure_a)
        .read_storage_buffer(pressure_b)
        .read_storage_buffer(divergence)
        .read_storage_buffer(solid)
        .read_storage_buffer(cell_counts)
        .read_storage_buffer(cell_particle_indices)
        .write_color(backbuffer)
        .execute([resource_ptr, config_ptr, runtime_state_ptr, debug_view, backbuffer, color_target,
                  frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder& recorder = context.recorder();
            const cubey::render::RenderGraphResolvedTexture resolved =
                context.resolved_texture(backbuffer);
            record_water_2d_draw(recorder.handle(), *resource_ptr, *config_ptr, frame_slot,
                                 *runtime_state_ptr, debug_view,
                                 cubey::render::color_target_view(color_target.extent,
                                                                  color_target.format,
                                                                  resolved.image, resolved.view));
        });

    return {.graph = graph.compile()};
}

} // namespace cubey::projects::fluid::water_2d
