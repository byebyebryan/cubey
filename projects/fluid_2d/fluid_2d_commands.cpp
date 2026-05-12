#include "fluid_2d_commands.h"

#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cubey::projects::fluid_2d {
namespace {

using cubey::vulkan::vk_struct;

constexpr float kFallbackInjectionRadius = 0.08F;
constexpr float kPointerInjectionRadius = 0.065F;

struct RenderPushConstants {
    std::array<float, 4> grid_debug{};
};

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_time{};
    std::array<float, 4> injection_xy_radius_strength{};
    std::array<float, 4> injection_dye_active{};
    std::array<float, 4> force_decay{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);
static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 16U);

struct DispatchGroups {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

struct ShaderWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

struct TransferWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

[[nodiscard]] DispatchGroups compute_dispatch_groups(const Fluid2DConfig& config) {
    return {
        .x = (config.grid_width + config.compute_group_size - 1U) / config.compute_group_size,
        .y = (config.grid_height + config.compute_group_size - 1U) / config.compute_group_size,
    };
}

void record_shader_write_barrier(VkCommandBuffer command_buffer, ShaderWriteBarrier config) {
    auto barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, config.dst_stage, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
}

void record_transfer_write_barrier(VkCommandBuffer command_buffer, TransferWriteBarrier config) {
    auto barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, config.dst_stage, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
}

[[nodiscard]] float debug_view_push_value(FluidDebugView view) {
    return static_cast<float>(static_cast<std::uint32_t>(view));
}

[[nodiscard]] SimulationPushConstants simulation_push_constants(const Fluid2DConfig& config,
                                                                const FrameInjection& injection,
                                                                const ProjectFrame& frame) {
    const float time = static_cast<float>(frame.elapsed_seconds);
    const float dt = std::min(static_cast<float>(frame.delta_seconds), config.fixed_delta_seconds);
    const bool pointer_active = injection.active;
    const float injection_x =
        pointer_active ? injection.xy[0] : 0.5F + (std::cos(time * 0.73F) * 0.23F);
    const float injection_y =
        pointer_active ? injection.xy[1] : 0.5F + (std::sin(time * 0.91F) * 0.18F);
    const float injection_radius =
        pointer_active ? kPointerInjectionRadius : kFallbackInjectionRadius;
    const float injection_strength = pointer_active ? 11.0F : 8.0F;
    const std::array<float, 3> injection_dye =
        pointer_active
            ? std::array<float, 3>{0.98F, 0.32F, 0.13F}
            : std::array<float, 3>{0.12F + (0.18F * std::sin(time * 0.47F)), 0.46F, 0.92F};
    const float force_x = pointer_active ? injection.force[0] : -std::sin(time * 0.91F) * 1.8F;
    const float force_y = pointer_active ? injection.force[1] : std::cos(time * 0.73F) * 1.8F;

    return {
        .grid_dt_time =
            {
                static_cast<float>(config.grid_width),
                static_cast<float>(config.grid_height),
                dt,
                time,
            },
        .injection_xy_radius_strength =
            {
                injection_x,
                injection_y,
                injection_radius,
                injection_strength,
            },
        .injection_dye_active =
            {
                injection_dye[0],
                injection_dye[1],
                injection_dye[2],
                1.0F,
            },
        .force_decay =
            {
                force_x,
                force_y,
                config.dye_decay_per_second,
                config.velocity_decay_per_second,
            },
    };
}

void record_field_reset(VkCommandBuffer command_buffer, const Fluid2DGpuResources& resources) {
    vkCmdFillBuffer(command_buffer, resources.field_a().handle(), 0, resources.field_a().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.field_b().handle(), 0, resources.field_b().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.divergence().handle(), 0,
                    resources.divergence().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.pressure_a().handle(), 0,
                    resources.pressure_a().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.pressure_b().handle(), 0,
                    resources.pressure_b().size(), 0);
    record_transfer_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });
}

} // namespace

void record_fluid_compute(VkCommandBuffer command_buffer, Fluid2DGpuResources& resources,
                          const Fluid2DConfig& config, const FrameInjection& injection, bool paused,
                          bool& reset_requested, const ProjectFrame& frame,
                          bool include_render_visibility_barrier) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);

    if (reset_requested) {
        record_field_reset(command_buffer, resources);
        reset_requested = false;
    }
    if (paused) {
        return;
    }

    const SimulationPushConstants push_constants =
        simulation_push_constants(config, injection, frame);
    const DispatchGroups groups = compute_dispatch_groups(config);

    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, resources.inject_pipeline().handle());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                 resources.compute_pipeline_layout().handle(), 0,
                                 resources.inject_descriptor_set());
    recorder.push_constants(resources.compute_pipeline_layout().handle(),
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, resources.advect_pipeline().handle());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                 resources.compute_pipeline_layout().handle(), 0,
                                 resources.advect_descriptor_set());
    recorder.push_constants(resources.compute_pipeline_layout().handle(),
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                           resources.divergence_pipeline().handle());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                 resources.divergence_pipeline_layout().handle(), 0,
                                 resources.divergence_descriptor_set());
    recorder.push_constants(resources.divergence_pipeline_layout().handle(),
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, resources.pressure_pipeline().handle());
    for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
        const VkDescriptorSet descriptor_set = (iteration % 2U == 0)
                                                   ? resources.pressure_a_to_b_descriptor_set()
                                                   : resources.pressure_b_to_a_descriptor_set();
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                     resources.pressure_pipeline_layout().handle(), 0,
                                     descriptor_set);
        recorder.push_constants(resources.pressure_pipeline_layout().handle(),
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
        recorder.dispatch(groups.x, groups.y, 1);
        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });
    }

    const bool final_pressure_is_a = (config.pressure_iterations % 2U) == 0;
    const VkDescriptorSet projection_descriptor_set =
        final_pressure_is_a ? resources.projection_pressure_a_descriptor_set()
                            : resources.projection_pressure_b_descriptor_set();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                           resources.projection_pipeline().handle());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                 resources.projection_pipeline_layout().handle(), 0,
                                 projection_descriptor_set);
    recorder.push_constants(resources.projection_pipeline_layout().handle(),
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    if (include_render_visibility_barrier) {
        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });
    }
}

void record_fullscreen_draw(VkCommandBuffer command_buffer, const Fluid2DGpuResources& resources,
                            const Fluid2DConfig& config, FluidDebugView debug_view,
                            VkImageView image_view, VkExtent2D extent) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);

    VkClearValue clear{};
    clear.color = {{0.006F, 0.008F, 0.014F, 1.0F}};
    const VkRenderingAttachmentInfo color_attachment =
        cubey::vulkan::color_rendering_attachment(image_view, clear);

    auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea.offset = {0, 0};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_attachment;

    const RenderPushConstants push_constants{
        .grid_debug =
            {
                static_cast<float>(config.grid_width),
                static_cast<float>(config.grid_height),
                debug_view_push_value(debug_view),
                (config.pressure_iterations % 2U) == 0 ? 0.0F : 1.0F,
            },
    };

    recorder.begin_rendering(rendering);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, resources.render_pipeline().handle());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 resources.render_pipeline_layout().handle(), 0,
                                 resources.render_descriptors().set());
    recorder.push_constants(resources.render_pipeline_layout().handle(),
                            VK_SHADER_STAGE_FRAGMENT_BIT, 0, push_constants);
    recorder.draw(3);
    recorder.end_rendering();
}

void record_fluid_frame(const cubey::host::WindowedRenderFrame& render_frame,
                        Fluid2DGpuResources& resources, const Fluid2DConfig& config,
                        FluidDebugView debug_view, const FrameInjection& injection, bool paused,
                        bool& reset_requested, const ProjectFrame& frame) {
    const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
    recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle field =
        graph.import_buffer({.label = "fluid field",
                             .byte_size = resources.field_a().size()},
                            resources.field_a().handle());
    const cubey::render::RenderGraphBufferHandle divergence =
        graph.import_buffer({.label = "fluid divergence",
                             .byte_size = resources.divergence().size()},
                            resources.divergence().handle());
    const cubey::render::RenderGraphBufferHandle pressure_a =
        graph.import_buffer({.label = "fluid pressure A",
                             .byte_size = resources.pressure_a().size()},
                            resources.pressure_a().handle());
    const cubey::render::RenderGraphBufferHandle pressure_b =
        graph.import_buffer({.label = "fluid pressure B",
                             .byte_size = resources.pressure_b().size()},
                            resources.pressure_b().handle());
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("backbuffer", render_frame.color_target);

    graph.add_pass("fluid simulation", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(field)
        .read_write_storage_buffer(divergence)
        .read_write_storage_buffer(pressure_a)
        .read_write_storage_buffer(pressure_b)
        .execute([&](const cubey::render::RenderGraphExecutionContext&) {
            record_fluid_compute(recorder.handle(), resources, config, injection, paused,
                                 reset_requested, frame, false);
        });
    graph.add_pass("fluid render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(field)
        .read_storage_buffer(divergence)
        .read_storage_buffer(pressure_a)
        .read_storage_buffer(pressure_b)
        .write_color(backbuffer)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            cubey::render::record_render_graph_barriers(recorder, context);
            recorder.transition_image_layout(
                cubey::vulkan::begin_color_attachment_transition(render_frame.color_target.image));
            record_fullscreen_draw(recorder.handle(), resources, config, debug_view,
                                   render_frame.color_target.view,
                                   render_frame.color_target.extent);
            recorder.transition_image_layout(
                cubey::vulkan::finish_color_attachment_for_present_transition(
                    render_frame.color_target.image));
        });

    const cubey::render::CompiledRenderGraph frame_graph = graph.compile();
    frame_graph.execute();

    recorder.end("vkEndCommandBuffer fluid_2d");
}

} // namespace cubey::projects::fluid_2d
