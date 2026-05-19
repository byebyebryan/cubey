#include "fluid_3d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid_3d {
namespace {

using cubey::vulkan::ImageLayoutTransition;

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_time{};
    std::array<float, 4> decay_force{};
    std::array<float, 4> options{};
};

struct RenderPushConstants {
    std::array<float, 4> camera_position_steps{};
    std::array<float, 4> ray_right_tan{};
    std::array<float, 4> ray_up_aspect{};
    std::array<float, 4> ray_forward_debug{};
    std::array<float, 4> render_options{};
};

static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 12U);
static_assert(sizeof(RenderPushConstants) == sizeof(float) * 20U);

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

[[nodiscard]] DispatchGroups compute_dispatch_groups(const Fluid3DConfig& config) {
    if (config.compute_group_size == 0) {
        throw std::runtime_error("fluid 3D compute group size must be positive");
    }
    return {
        .x = (config.grid_width + config.compute_group_size - 1U) / config.compute_group_size,
        .y = (config.grid_height + config.compute_group_size - 1U) / config.compute_group_size,
        .z = (config.grid_depth + config.compute_group_size - 1U) / config.compute_group_size,
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
                                   const Fluid3DGpuResources& resources) {
    transition_volume_to_general(command_buffer, resources.density_a());
    transition_volume_to_general(command_buffer, resources.density_b());
    transition_volume_to_general(command_buffer, resources.velocity_a());
    transition_volume_to_general(command_buffer, resources.velocity_b());
    transition_volume_to_general(command_buffer, resources.divergence());
    transition_volume_to_general(command_buffer, resources.pressure_a());
    transition_volume_to_general(command_buffer, resources.pressure_b());
}

void record_injector_buffer_update(VkCommandBuffer command_buffer,
                                   const Fluid3DGpuResources& resources,
                                   std::span<const Fluid3DInjectorGpu> injectors) {
    if (injectors.empty()) {
        return;
    }
    const VkDeviceSize byte_size =
        static_cast<VkDeviceSize>(injectors.size() * sizeof(Fluid3DInjectorGpu));
    if (byte_size > resources.injectors().size()) {
        throw std::runtime_error("fluid 3D injector update exceeds injector buffer size");
    }
    vkCmdUpdateBuffer(command_buffer, resources.injectors().handle(), 0, byte_size,
                      injectors.data());
    record_transfer_write_barrier(command_buffer,
                                  {
                                      .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      .dst_access = VK_ACCESS_SHADER_READ_BIT,
                                  });
}

[[nodiscard]] SimulationPushConstants simulation_push_constants(const Fluid3DConfig& config,
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
                config.injector_strength,
                config.injector_radius,
            },
        .options =
            {
                static_cast<float>(config.injector_count),
                config.vorticity_strength,
                static_cast<float>(config.pressure_iterations),
                time,
            },
    };
}

[[nodiscard]] RenderPushConstants render_push_constants(const Fluid3DConfig& config,
                                                        Fluid3DDebugView debug_view,
                                                        const Fluid3DRenderCamera& camera,
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

void record_fluid_3d_compute(VkCommandBuffer command_buffer, Fluid3DGpuResources& resources,
                             const Fluid3DConfig& config, bool paused, bool& reset_requested,
                             const ProjectFrame& frame,
                             std::span<const Fluid3DInjectorGpu> injectors,
                             Fluid3DFrameState& frame_state,
                             bool include_render_visibility_barrier) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    if (!frame_state.volumes_initialized) {
        record_initial_volume_layouts(command_buffer, resources);
        frame_state.volumes_initialized = true;
        reset_requested = true;
    }

    const SimulationPushConstants push_constants = simulation_push_constants(config, frame);
    const DispatchGroups groups = compute_dispatch_groups(config);

    if (reset_requested) {
        const cubey::render::ComputePipelineResource& reset_pipeline = resources.reset_pipeline();
        record_dispatch(recorder, reset_pipeline, resources.reset_descriptor_set(), groups,
                        push_constants);
        frame_state.density_a_current = true;
        frame_state.velocity_a_current = true;
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

    record_injector_buffer_update(command_buffer, resources, injectors);

    const cubey::render::ComputePipelineResource& advect_pipeline = resources.advect_pipeline();
    record_dispatch(recorder, advect_pipeline,
                    resources.advect_descriptor_set(frame_state.density_a_current,
                                                    frame_state.velocity_a_current),
                    groups, push_constants);
    frame_state.density_a_current = !frame_state.density_a_current;
    frame_state.velocity_a_current = !frame_state.velocity_a_current;
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    const cubey::render::ComputePipelineResource& divergence_pipeline =
        resources.divergence_pipeline();
    record_dispatch(recorder, divergence_pipeline,
                    resources.divergence_descriptor_set(frame_state.velocity_a_current), groups,
                    push_constants);
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

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

    const bool pressure_a_current = (config.pressure_iterations % 2U) == 0;
    const cubey::render::ComputePipelineResource& projection_pipeline =
        resources.projection_pipeline();
    record_dispatch(
        recorder, projection_pipeline,
        resources.projection_descriptor_set(frame_state.velocity_a_current, pressure_a_current),
        groups, push_constants);
    frame_state.velocity_a_current = !frame_state.velocity_a_current;

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

void record_fluid_3d_draw(VkCommandBuffer command_buffer, const Fluid3DGpuResources& resources,
                          const Fluid3DConfig& config, Fluid3DDebugView debug_view,
                          const Fluid3DRenderCamera& camera,
                          cubey::render::ColorTargetView color_target,
                          const Fluid3DFrameState& frame_state) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const RenderPushConstants push_constants =
        render_push_constants(config, debug_view, camera, color_target.extent);

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
}

} // namespace cubey::projects::fluid_3d
