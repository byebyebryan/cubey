#include "pyro_3d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid::pyro_3d {
namespace {

using cubey::vulkan::ImageLayoutTransition;

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_time{};
    std::array<float, 4> decay_force{};
    std::array<float, 4> options{};
    std::array<float, 4> shadow_options{};
    std::array<float, 4> fire_options{};
    std::array<float, 4> shaping_options{};
    std::array<float, 4> obstacle_options{};
};

struct RenderPushConstants {
    std::array<float, 4> camera_position_steps{};
    std::array<float, 4> ray_right_tan{};
    std::array<float, 4> ray_up_aspect{};
    std::array<float, 4> ray_forward_debug{};
    std::array<float, 4> render_options{};
    std::array<float, 4> obstacle_options{};
};

static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 28U);
static_assert(sizeof(RenderPushConstants) == sizeof(float) * 24U);

struct DispatchGroups {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
};

struct ShaderWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

struct TransferWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

[[nodiscard]] DispatchGroups compute_dispatch_groups(VkExtent3D extent, std::uint32_t group_size) {
    if (group_size == 0) {
        throw std::runtime_error("pyro 3D compute group size must be positive");
    }
    return {
        .x = (extent.width + group_size - 1U) / group_size,
        .y = (extent.height + group_size - 1U) / group_size,
        .z = (extent.depth + group_size - 1U) / group_size,
    };
}

[[nodiscard]] VkExtent3D solver_extent(const Pyro3DConfig& config) {
    return {
        .width = config.grid_width,
        .height = config.grid_height,
        .depth = config.grid_depth,
    };
}

[[nodiscard]] VkExtent3D shadow_extent(const Pyro3DConfig& config) {
    return {
        .width = config.shadow_grid_width,
        .height = config.shadow_grid_height,
        .depth = config.shadow_grid_depth,
    };
}

[[nodiscard]] VkExtent3D max_extent(VkExtent3D lhs, VkExtent3D rhs) {
    return {
        .width = std::max(lhs.width, rhs.width),
        .height = std::max(lhs.height, rhs.height),
        .depth = std::max(lhs.depth, rhs.depth),
    };
}

void record_shader_write_barrier(VkCommandBuffer command_buffer, ShaderWriteBarrier config) {
    auto barrier = cubey::vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, config.dst_stage, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
}

void record_transfer_write_barrier(VkCommandBuffer command_buffer, TransferWriteBarrier config) {
    auto barrier = cubey::vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, config.dst_stage, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
}

void transition_volume_to_general(VkCommandBuffer command_buffer,
                                  const cubey::render::Texture3D& texture) {
    cubey::vulkan::transition_image_layout(
        command_buffer,
        ImageLayoutTransition{
            .image = texture.handle(),
            .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
            .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .new_layout = VK_IMAGE_LAYOUT_GENERAL,
            .src_access_mask = 0,
            .dst_access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        });
}

void record_initial_volume_layouts(VkCommandBuffer command_buffer,
                                   const Pyro3DGpuResources& resources) {
    transition_volume_to_general(command_buffer, resources.density_a());
    transition_volume_to_general(command_buffer, resources.density_b());
    transition_volume_to_general(command_buffer, resources.velocity_a());
    transition_volume_to_general(command_buffer, resources.velocity_b());
    transition_volume_to_general(command_buffer, resources.density_prediction());
    transition_volume_to_general(command_buffer, resources.velocity_prediction());
    transition_volume_to_general(command_buffer, resources.divergence());
    transition_volume_to_general(command_buffer, resources.pressure_a());
    transition_volume_to_general(command_buffer, resources.pressure_b());
    transition_volume_to_general(command_buffer, resources.shadow_volume());
}

void record_source_buffer_update(VkCommandBuffer command_buffer,
                                 const Pyro3DGpuResources& resources,
                                 std::span<const Pyro3DSourceGpu> sources) {
    if (sources.empty()) {
        return;
    }
    const VkDeviceSize byte_size =
        static_cast<VkDeviceSize>(sources.size() * sizeof(Pyro3DSourceGpu));
    if (byte_size > resources.sources().size()) {
        throw std::runtime_error("pyro 3D source update exceeds source buffer size");
    }
    vkCmdUpdateBuffer(command_buffer, resources.sources().handle(), 0, byte_size, sources.data());
    record_transfer_write_barrier(command_buffer,
                                  {
                                      .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      .dst_access = VK_ACCESS_SHADER_READ_BIT,
                                  });
}

[[nodiscard]] SimulationPushConstants simulation_push_constants(const Pyro3DConfig& config,
                                                                const ProjectFrame& frame) {
    const float time = static_cast<float>(frame.elapsed_seconds);
    const float dt = std::min(static_cast<float>(frame.delta_seconds), config.fixed_delta_seconds);
    return {
        .grid_dt_time =
            {
                static_cast<float>(config.grid_width),
                static_cast<float>(config.grid_height),
                static_cast<float>(config.grid_depth),
                dt,
            },
        .decay_force =
            {
                config.density_decay_per_second,
                config.velocity_decay_per_second,
                config.source_velocity_strength,
                config.source_radius,
            },
        .options =
            {
                static_cast<float>(config.source_count),
                config.vorticity_strength,
                static_cast<float>(config.pressure_iterations),
                time,
            },
        .shadow_options =
            {
                config.shadow_absorption,
                static_cast<float>(config.shadow_steps),
                config.buoyancy_strength,
                static_cast<float>(static_cast<std::uint32_t>(config.mode)),
            },
        .fire_options =
            {
                config.fire_ignition_temperature,
                config.fire_burn_rate,
                config.fire_heat_output,
                config.fire_soot_yield,
            },
        .shaping_options =
            {
                config.fire_expansion,
                config.fire_flame_cooling,
                config.fire_shredding,
                config.fire_turbulence,
            },
        .obstacle_options =
            {
                config.obstacle_center_height,
                config.obstacle_radius,
                0.0F,
                0.0F,
            },
    };
}

[[nodiscard]] RenderPushConstants render_push_constants(const Pyro3DConfig& config,
                                                        Pyro3DDebugView debug_view,
                                                        const Pyro3DRenderCamera& camera,
                                                        VkExtent2D extent) {
    const float aspect = extent.height == 0
                             ? 1.0F
                             : static_cast<float>(extent.width) / static_cast<float>(extent.height);
    return {
        .camera_position_steps =
            {
                camera.position.x,
                camera.position.y,
                camera.position.z,
                static_cast<float>(config.raymarch_steps),
            },
        .ray_right_tan =
            {
                camera.right.x,
                camera.right.y,
                camera.right.z,
                std::tan(camera.fovy_radians * 0.5F),
            },
        .ray_up_aspect =
            {
                camera.up.x,
                camera.up.y,
                camera.up.z,
                aspect,
            },
        .ray_forward_debug =
            {
                camera.forward.x,
                camera.forward.y,
                camera.forward.z,
                static_cast<float>(static_cast<std::uint32_t>(debug_view)),
            },
        .render_options =
            {
                config.absorption,
                config.emission,
                config.shadow_absorption,
                config.ambient_light,
            },
        .obstacle_options =
            {
                config.obstacle_center_height,
                config.obstacle_radius,
                0.0F,
                0.0F,
            },
    };
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set, const DispatchGroups& groups,
                     const SimulationPushConstants& push_constants) {
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptor_set);
    recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, groups.z);
}

} // namespace

void record_pyro_3d_compute(VkCommandBuffer command_buffer, Pyro3DGpuResources& resources,
                            const Pyro3DConfig& config, bool paused, bool& reset_requested,
                            const ProjectFrame& frame, std::span<const Pyro3DSourceGpu> sources,
                            Pyro3DFrameState& frame_state, bool include_render_visibility_barrier,
                            cubey::vulkan::GpuTimestampProfiler* profiler,
                            std::uint32_t frame_slot_index) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    auto begin_pass = [&](const char* label) {
        if (profiler != nullptr) {
            profiler->begin_pass(command_buffer, frame_slot_index, label);
        }
    };
    auto end_pass = [&]() {
        if (profiler != nullptr) {
            profiler->end_pass(command_buffer, frame_slot_index);
        }
    };
    if (!frame_state.volumes_initialized) {
        record_initial_volume_layouts(command_buffer, resources);
        frame_state.volumes_initialized = true;
        reset_requested = true;
    }

    const SimulationPushConstants push_constants = simulation_push_constants(config, frame);
    const DispatchGroups groups =
        compute_dispatch_groups(solver_extent(config), config.compute_group_size);
    const DispatchGroups shadow_groups =
        compute_dispatch_groups(shadow_extent(config), config.compute_group_size);
    const DispatchGroups reset_groups = compute_dispatch_groups(
        max_extent(solver_extent(config), shadow_extent(config)), config.compute_group_size);

    if (reset_requested) {
        begin_pass("reset");
        const cubey::render::ComputePipelineResource& reset_pipeline = resources.reset_pipeline();
        record_dispatch(recorder, reset_pipeline, resources.reset_descriptor_set(), reset_groups,
                        push_constants);
        end_pass();
        frame_state.density_a_current = true;
        frame_state.velocity_a_current = true;
        frame_state.shadow_initialized = false;
        frame_state.frames_since_shadow_update = 0;
        reset_requested = false;
        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage =
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });
    }

    if (paused) {
        return;
    }

    record_source_buffer_update(command_buffer, resources, sources);

    begin_pass("advect_predict");
    const cubey::render::ComputePipelineResource& advect_pipeline = resources.advect_pipeline();
    record_dispatch(recorder, advect_pipeline,
                    resources.advect_descriptor_set(frame_state.density_a_current,
                                                    frame_state.velocity_a_current),
                    groups, push_constants);
    end_pass();
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    begin_pass("advect_correct");
    const cubey::render::ComputePipelineResource& advect_correct_pipeline =
        resources.advect_correct_pipeline();
    record_dispatch(recorder, advect_correct_pipeline,
                    resources.advect_correct_descriptor_set(frame_state.density_a_current,
                                                            frame_state.velocity_a_current),
                    groups, push_constants);
    end_pass();
    frame_state.density_a_current = !frame_state.density_a_current;
    frame_state.velocity_a_current = !frame_state.velocity_a_current;
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    begin_pass("combustion");
    const cubey::render::ComputePipelineResource& combustion_pipeline =
        resources.combustion_pipeline();
    record_dispatch(recorder, combustion_pipeline,
                    resources.combustion_descriptor_set(frame_state.density_a_current,
                                                        frame_state.velocity_a_current),
                    groups, push_constants);
    end_pass();
    frame_state.density_a_current = !frame_state.density_a_current;
    frame_state.velocity_a_current = !frame_state.velocity_a_current;
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    begin_pass("divergence");
    const cubey::render::ComputePipelineResource& divergence_pipeline =
        resources.divergence_pipeline();
    record_dispatch(recorder, divergence_pipeline,
                    resources.divergence_descriptor_set(frame_state.density_a_current,
                                                        frame_state.velocity_a_current),
                    groups, push_constants);
    end_pass();
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    begin_pass("pressure");
    const cubey::render::ComputePipelineResource& pressure_pipeline = resources.pressure_pipeline();
    for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
        const VkDescriptorSet descriptor_set = (iteration % 2U == 0)
                                                   ? resources.pressure_a_to_b_descriptor_set()
                                                   : resources.pressure_b_to_a_descriptor_set();
        record_dispatch(recorder, pressure_pipeline, descriptor_set, groups, push_constants);
        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });
    }
    end_pass();

    const bool pressure_a_current = (config.pressure_iterations % 2U) == 0;
    begin_pass("projection");
    const cubey::render::ComputePipelineResource& projection_pipeline =
        resources.projection_pipeline();
    record_dispatch(
        recorder, projection_pipeline,
        resources.projection_descriptor_set(frame_state.velocity_a_current, pressure_a_current),
        groups, push_constants);
    end_pass();
    frame_state.velocity_a_current = !frame_state.velocity_a_current;

    const std::uint32_t shadow_interval = std::max(config.shadow_update_interval, 1U);
    const bool update_shadow = !frame_state.shadow_initialized ||
                               frame_state.frames_since_shadow_update >= shadow_interval - 1U;
    if (update_shadow) {
        begin_pass("shadow");
        const cubey::render::ComputePipelineResource& shadow_pipeline = resources.shadow_pipeline();
        record_dispatch(recorder, shadow_pipeline,
                        resources.shadow_descriptor_set(frame_state.density_a_current),
                        shadow_groups, push_constants);
        end_pass();
        frame_state.shadow_initialized = true;
        frame_state.frames_since_shadow_update = 0;
    } else {
        ++frame_state.frames_since_shadow_update;
    }

    if (include_render_visibility_barrier) {
        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage =
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });
    }
}

void record_pyro_3d_draw(VkCommandBuffer command_buffer, const Pyro3DGpuResources& resources,
                         const Pyro3DConfig& config, Pyro3DDebugView debug_view,
                         const Pyro3DRenderCamera& camera,
                         cubey::render::ColorTargetView color_target,
                         const Pyro3DFrameState& frame_state,
                         cubey::vulkan::GpuTimestampProfiler* profiler,
                         std::uint32_t frame_slot_index) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const RenderPushConstants push_constants =
        render_push_constants(config, debug_view, camera, color_target.extent);

    if (profiler != nullptr) {
        profiler->begin_pass(command_buffer, frame_slot_index, "raymarch");
    }
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.008F, 0.012F, 1.0F),
        },
        [&resources, &frame_state,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &resources.render_pipeline(),
                    .descriptor_set = resources.render_descriptor_set(
                        frame_state.density_a_current, frame_state.velocity_a_current),
                },
                VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
    if (profiler != nullptr) {
        profiler->end_pass(command_buffer, frame_slot_index);
    }
}

} // namespace cubey::projects::fluid::pyro_3d
