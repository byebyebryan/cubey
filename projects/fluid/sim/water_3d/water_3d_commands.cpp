#include "water_3d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cubey::projects::fluid::water_3d {
namespace {

struct RenderPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    std::array<float, 4> camera_right_radius{};
    std::array<float, 4> camera_up_debug{};
    std::array<float, 4> grid_slice{};
    std::array<float, 4> color_options{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * kWater3DRenderPushConstantFloatCount);

struct DispatchGroups {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
};

struct ShaderWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

[[nodiscard]] DispatchGroups compute_dispatch_groups(std::uint32_t width, std::uint32_t height,
                                                     std::uint32_t depth) {
    return {
        .x = (width + kWater3DComputeGroupSize - 1U) / kWater3DComputeGroupSize,
        .y = (height + kWater3DComputeGroupSize - 1U) / kWater3DComputeGroupSize,
        .z = (depth + kWater3DComputeGroupSize - 1U) / kWater3DComputeGroupSize,
    };
}

[[nodiscard]] DispatchGroups linear_dispatch_groups(std::size_t count) {
    return {
        .x = static_cast<std::uint32_t>((count + kWater3DParticleGroupSize - 1U) /
                                        kWater3DParticleGroupSize),
        .y = 1U,
        .z = 1U,
    };
}

[[nodiscard]] DispatchGroups cell_dispatch_groups(const Water3DConfig& config) {
    return compute_dispatch_groups(config.grid_width, config.grid_height, config.grid_depth);
}

[[nodiscard]] DispatchGroups face_dispatch_groups(const Water3DConfig& config) {
    return compute_dispatch_groups(config.grid_width + 1U, config.grid_height + 1U,
                                   config.grid_depth + 1U);
}

[[nodiscard]] DispatchGroups particle_scan_dispatch_groups(const Water3DConfig& config,
                                                           const Water3DRuntimeState& state) {
    return linear_dispatch_groups(water_3d_runtime_particle_scan_count(config, state));
}

[[nodiscard]] DispatchGroups bin_dispatch_groups(const Water3DConfig& config) {
    return linear_dispatch_groups(std::max(cell_count(config), particle_bin_index_count(config)));
}

[[nodiscard]] DispatchGroups reset_dispatch_groups(const Water3DConfig& config) {
    return linear_dispatch_groups(
        std::max({static_cast<std::size_t>(config.particle_capacity), cell_count(config),
                  u_face_count(config), v_face_count(config), w_face_count(config),
                  particle_bin_index_count(config)}));
}

void record_shader_write_barrier(VkCommandBuffer command_buffer, ShaderWriteBarrier config) {
    auto barrier = cubey::vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, config.dst_stage, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
}

void record_compute_barrier(VkCommandBuffer command_buffer) {
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });
}

void record_final_barrier(VkCommandBuffer command_buffer) {
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });
}

[[nodiscard]] float debug_view_push_value(Water3DDebugView view) {
    return static_cast<float>(static_cast<std::uint32_t>(view));
}

[[nodiscard]] Water3DSimulationUniforms simulation_uniforms(const Water3DConfig& config) {
    return {
        .grid_options =
            {
                water_3d_shader_count_float(config.grid_width,
                                            "water 3D grid width exceeds exact shader integer range"),
                water_3d_shader_count_float(config.grid_height,
                                            "water 3D grid height exceeds exact shader integer range"),
                water_3d_shader_count_float(config.grid_depth,
                                            "water 3D grid depth exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.active_particle_count,
                    "water 3D active particle count exceeds exact shader integer range"),
            },
        .particle_options =
            {
                water_3d_shader_count_float(
                    config.particle_capacity,
                    "water 3D particle capacity exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.max_particles_per_cell,
                    "water 3D max particles per cell exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.particles_per_cell,
                    "water 3D particles per cell exceeds exact shader integer range"),
                config.flip_ratio,
            },
        .fill_options =
            {
                config.initial_fill_width,
                config.initial_fill_height,
                config.initial_fill_depth,
                config.gravity,
            },
        .solve_options =
            {
                config.velocity_limit,
                config.particle_damping,
                config.particle_volume_strength,
                static_cast<float>(static_cast<std::uint32_t>(config.transfer_mode)),
            },
        .lifecycle_options =
            {
                water_3d_shader_count_float(
                    config.active_particle_count,
                    "water 3D active particle count exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.particle_capacity,
                    "water 3D particle capacity exceeds exact shader integer range"),
                config.boundary_restitution,
                0.0F,
            },
        .render_options =
            {
                config.particle_radius,
                config.slice_depth,
                0.0F,
                0.0F,
            },
    };
}

[[nodiscard]] Water3DDispatchPushConstants
dispatch_push_constants(const ProjectFrame& frame, float delta_seconds, float pressure_read_b,
                        std::uint32_t particle_scan_count) {
    return {
        .dispatch_options =
            {
                delta_seconds,
                static_cast<float>(frame.elapsed_seconds),
                pressure_read_b,
                water_3d_shader_count_float(
                    particle_scan_count,
                    "water 3D particle scan count exceeds exact shader integer range"),
            },
    };
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set, DispatchGroups groups,
                     const Water3DDispatchPushConstants& push_constants) {
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptor_set);
    recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, groups.z);
}

void record_refresh_bins(const cubey::vulkan::CommandRecorder& recorder,
                         VkCommandBuffer command_buffer, Water3DGpuResources& resources,
                         VkDescriptorSet descriptor_set, const Water3DConfig& config,
                         const Water3DRuntimeState& runtime_state,
                         const Water3DDispatchPushConstants& push_constants) {
    record_dispatch(recorder, resources.clear_bins_pipeline_resource(), descriptor_set,
                    bin_dispatch_groups(config), push_constants);
    record_compute_barrier(command_buffer);
    record_dispatch(recorder, resources.build_bins_pipeline_resource(), descriptor_set,
                    particle_scan_dispatch_groups(config, runtime_state), push_constants);
    record_compute_barrier(command_buffer);
}

} // namespace

void record_water_3d_compute(VkCommandBuffer command_buffer, Water3DGpuResources& resources,
                             const Water3DConfig& config, Water3DRuntimeState& runtime_state,
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
    Water3DDispatchPushConstants push_constants = dispatch_push_constants(
        frame, substep_dt, 0.0F, water_3d_runtime_particle_scan_count(config, runtime_state));

    if (reset_requested) {
        runtime_state = {};
        record_dispatch(recorder, resources.reset_pipeline_resource(), descriptor_set,
                        reset_dispatch_groups(config), push_constants);
        record_compute_barrier(command_buffer);
        runtime_state.particle_scan_count = config.active_particle_count;
        push_constants.dispatch_options[3] =
            water_3d_shader_count_float(
                water_3d_runtime_particle_scan_count(config, runtime_state),
                "water 3D particle scan count exceeds exact shader integer range");
        reset_requested = false;
    }
    if (paused) {
        record_refresh_bins(recorder, command_buffer, resources, descriptor_set, config,
                            runtime_state, push_constants);
        if (include_render_visibility_barrier) {
            record_final_barrier(command_buffer);
        }
        return;
    }

    for (std::uint32_t substep = 0; substep < substep_count; ++substep) {
        const float substep_time =
            static_cast<float>(frame.elapsed_seconds) + (static_cast<float>(substep) * substep_dt);
        push_constants = dispatch_push_constants(
            frame, substep_dt, 0.0F, water_3d_runtime_particle_scan_count(config, runtime_state));
        push_constants.dispatch_options[1] = substep_time;

        record_dispatch(recorder, resources.clear_grid_pipeline_resource(), descriptor_set,
                        face_groups, push_constants);
        record_compute_barrier(command_buffer);

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

    if (include_render_visibility_barrier) {
        record_final_barrier(command_buffer);
    }
}

void record_water_3d_draw(VkCommandBuffer command_buffer, const Water3DGpuResources& resources,
                          const Water3DConfig& config, cubey::render::FrameSlot frame_slot,
                          const Water3DRuntimeState& runtime_state, Water3DDebugView debug_view,
                          const Water3DRenderCamera& camera,
                          cubey::render::ColorTargetView color_target) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const std::uint32_t particle_scan_count =
        water_3d_runtime_particle_scan_count(config, runtime_state);
    const RenderPushConstants push_constants{
        .view_projection = camera.view_projection,
        .camera_right_radius =
            {
                camera.right.x,
                camera.right.y,
                camera.right.z,
                config.particle_radius,
            },
        .camera_up_debug =
            {
                camera.up.x,
                camera.up.y,
                camera.up.z,
                debug_view_push_value(debug_view),
            },
        .grid_slice =
            {
                water_3d_shader_count_float(config.grid_width,
                                            "water 3D grid width exceeds exact shader integer range"),
                water_3d_shader_count_float(config.grid_height,
                                            "water 3D grid height exceeds exact shader integer range"),
                water_3d_shader_count_float(config.grid_depth,
                                            "water 3D grid depth exceeds exact shader integer range"),
                config.slice_depth,
            },
        .color_options =
            {
                runtime_state.pressure_read_b ? 1.0F : 0.0F,
                water_3d_shader_count_float(particle_scan_count,
                                            "water 3D particle scan count exceeds exact shader integer range"),
                water_3d_shader_count_float(config.max_particles_per_cell,
                                            "water 3D max particles per cell exceeds exact shader integer range"),
                0.0F,
            },
    };

    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.008F, 0.013F, 1.0F),
        },
        [&resources, frame_slot, push_constants,
         debug_view](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline =
                resources.render_pipeline_resource();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                                              resources.field_descriptor_set(frame_slot));
            pass_recorder.push_constants(pipeline.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT |
                                             VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, push_constants);
            const std::uint32_t instance_count =
                debug_view == Water3DDebugView::Particles
                    ? static_cast<std::uint32_t>(push_constants.color_options[1])
                    : 1U;
            pass_recorder.draw(6, std::max(1U, instance_count));
        });
}

} // namespace cubey::projects::fluid::water_3d
