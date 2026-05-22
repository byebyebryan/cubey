#include "water_2d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::water_2d {
namespace {

struct RenderPushConstants {
    std::array<float, 4> grid_debug{};
};

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_time{};
    std::array<float, 4> init_options{};
    std::array<float, 4> obstacle_options{};
    std::array<float, 4> solve_options{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);
static_assert(sizeof(SimulationPushConstants) ==
              sizeof(float) * kWater2DSimulationPushConstantFloatCount);

struct DispatchGroups {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

struct ShaderWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

[[nodiscard]] DispatchGroups compute_dispatch_groups(std::uint32_t width, std::uint32_t height) {
    return {
        .x = (width + kWater2DComputeGroupSize - 1U) / kWater2DComputeGroupSize,
        .y = (height + kWater2DComputeGroupSize - 1U) / kWater2DComputeGroupSize,
    };
}

[[nodiscard]] DispatchGroups cell_dispatch_groups(const Water2DConfig& config) {
    return compute_dispatch_groups(config.grid_width, config.grid_height);
}

[[nodiscard]] DispatchGroups face_dispatch_groups(const Water2DConfig& config) {
    return compute_dispatch_groups(config.grid_width + 1U, config.grid_height + 1U);
}

void record_shader_write_barrier(VkCommandBuffer command_buffer, ShaderWriteBarrier config) {
    auto barrier = cubey::vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, config.dst_stage, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
}

[[nodiscard]] float debug_view_push_value(Water2DDebugView view) {
    return static_cast<float>(static_cast<std::uint32_t>(view));
}

[[nodiscard]] SimulationPushConstants simulation_push_constants(const Water2DConfig& config,
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
        .init_options =
            {
                config.initial_fill_height,
                config.initial_fill_width,
                config.gravity,
                config.obstacles_enabled ? 1.0F : 0.0F,
            },
        .obstacle_options =
            {
                config.obstacle_center[0],
                config.obstacle_center[1],
                config.obstacle_radius,
                0.0F,
            },
        .solve_options = {0.0F, 0.0F, 0.0F, 0.0F},
    };
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set, DispatchGroups groups,
                     const SimulationPushConstants& push_constants) {
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptor_set);
    recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, 1);
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
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });
}

} // namespace

void record_water_2d_compute(VkCommandBuffer command_buffer, Water2DGpuResources& resources,
                             const Water2DConfig& config, bool paused, bool& reset_requested,
                             const ProjectFrame& frame, bool include_render_visibility_barrier) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const VkDescriptorSet descriptor_set = resources.field_descriptor_set();
    const DispatchGroups cell_groups = cell_dispatch_groups(config);
    const DispatchGroups face_groups = face_dispatch_groups(config);
    SimulationPushConstants push_constants = simulation_push_constants(config, frame);

    if (reset_requested) {
        record_dispatch(recorder, resources.reset_pipeline_resource(), descriptor_set, face_groups,
                        push_constants);
        record_compute_barrier(command_buffer);
        reset_requested = false;
    }
    if (paused) {
        if (include_render_visibility_barrier) {
            record_final_barrier(command_buffer);
        }
        return;
    }

    record_dispatch(recorder, resources.force_pipeline_resource(), descriptor_set, face_groups,
                    push_constants);
    record_compute_barrier(command_buffer);

    record_dispatch(recorder, resources.advect_velocity_pipeline_resource(), descriptor_set,
                    face_groups, push_constants);
    record_compute_barrier(command_buffer);

    record_dispatch(recorder, resources.advect_phi_pipeline_resource(), descriptor_set, cell_groups,
                    push_constants);
    record_compute_barrier(command_buffer);

    bool current_phi_is_a = false;
    const std::uint32_t reinitialization_iterations =
        std::max(config.reinitialization_iterations, 1U);
    for (std::uint32_t iteration = 0; iteration < reinitialization_iterations; ++iteration) {
        push_constants.solve_options[0] = current_phi_is_a ? 1.0F : 0.0F;
        record_dispatch(recorder, resources.reinitialize_phi_pipeline_resource(), descriptor_set,
                        cell_groups, push_constants);
        record_compute_barrier(command_buffer);
        current_phi_is_a = !current_phi_is_a;
    }
    if (!current_phi_is_a) {
        push_constants.solve_options[0] = 0.0F;
        record_dispatch(recorder, resources.reinitialize_phi_pipeline_resource(), descriptor_set,
                        cell_groups, push_constants);
        record_compute_barrier(command_buffer);
    }

    record_dispatch(recorder, resources.divergence_pipeline_resource(), descriptor_set, cell_groups,
                    push_constants);
    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& pressure_pipeline =
        resources.pressure_pipeline_resource();
    for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
        push_constants.solve_options[1] = (iteration % 2U == 0U) ? 1.0F : 0.0F;
        record_dispatch(recorder, pressure_pipeline, descriptor_set, cell_groups, push_constants);
        record_compute_barrier(command_buffer);
    }

    const bool final_pressure_is_a = (config.pressure_iterations % 2U) == 0U;
    push_constants.solve_options[1] = final_pressure_is_a ? 1.0F : 0.0F;
    record_dispatch(recorder, resources.projection_pipeline_resource(), descriptor_set, face_groups,
                    push_constants);

    if (include_render_visibility_barrier) {
        record_final_barrier(command_buffer);
    }
}

void record_water_2d_draw(VkCommandBuffer command_buffer, const Water2DGpuResources& resources,
                          const Water2DConfig& config, Water2DDebugView debug_view,
                          cubey::render::ColorTargetView color_target) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const RenderPushConstants push_constants{
        .grid_debug =
            {
                static_cast<float>(config.grid_width),
                static_cast<float>(config.grid_height),
                debug_view_push_value(debug_view),
                (config.pressure_iterations % 2U) == 0U ? 0.0F : 1.0F,
            },
    };

    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.009F, 0.014F, 1.0F),
        },
        [&resources, push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &resources.render_pipeline_resource(),
                    .descriptor_set = resources.field_descriptor_set(),
                },
                VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

[[nodiscard]] cubey::render::CompiledRenderGraph
build_water_2d_frame_graph(cubey::render::ColorTargetView color_target,
                           Water2DGpuResources& resources, const Water2DConfig& config,
                           Water2DDebugView debug_view, bool paused, bool& reset_requested,
                           const ProjectFrame& frame) {
    Water2DGpuResources* resource_ptr = &resources;
    const Water2DConfig* config_ptr = &config;
    bool* reset_requested_ptr = &reset_requested;
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle phi_a =
        graph.import_buffer({.label = "water phi A", .byte_size = resources.phi_a().size()},
                            resources.phi_a().handle());
    const cubey::render::RenderGraphBufferHandle phi_b =
        graph.import_buffer({.label = "water phi B", .byte_size = resources.phi_b().size()},
                            resources.phi_b().handle());
    const cubey::render::RenderGraphBufferHandle u_a = graph.import_buffer(
        {.label = "water U A", .byte_size = resources.u_a().size()}, resources.u_a().handle());
    const cubey::render::RenderGraphBufferHandle u_b = graph.import_buffer(
        {.label = "water U B", .byte_size = resources.u_b().size()}, resources.u_b().handle());
    const cubey::render::RenderGraphBufferHandle v_a = graph.import_buffer(
        {.label = "water V A", .byte_size = resources.v_a().size()}, resources.v_a().handle());
    const cubey::render::RenderGraphBufferHandle v_b = graph.import_buffer(
        {.label = "water V B", .byte_size = resources.v_b().size()}, resources.v_b().handle());
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
    const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "backbuffer", color_target, cubey::render::render_graph_undefined_texture_state(),
        cubey::render::render_graph_present_texture_state());

    graph.add_pass("water simulation", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(phi_a)
        .read_write_storage_buffer(phi_b)
        .read_write_storage_buffer(u_a)
        .read_write_storage_buffer(u_b)
        .read_write_storage_buffer(v_a)
        .read_write_storage_buffer(v_b)
        .read_write_storage_buffer(pressure_a)
        .read_write_storage_buffer(pressure_b)
        .read_write_storage_buffer(divergence)
        .read_write_storage_buffer(solid)
        .execute([resource_ptr, config_ptr, paused, reset_requested_ptr,
                  frame](const cubey::render::RenderGraphExecutionContext& context) {
            record_water_2d_compute(context.recorder().handle(), *resource_ptr, *config_ptr, paused,
                                    *reset_requested_ptr, frame, false);
        });
    graph.add_pass("water render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(phi_a)
        .read_storage_buffer(u_a)
        .read_storage_buffer(v_a)
        .read_storage_buffer(pressure_a)
        .read_storage_buffer(pressure_b)
        .read_storage_buffer(divergence)
        .read_storage_buffer(solid)
        .write_color(backbuffer)
        .execute([resource_ptr, config_ptr, debug_view, backbuffer,
                  color_target](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder& recorder = context.recorder();
            const cubey::render::RenderGraphResolvedTexture resolved =
                context.resolved_texture(backbuffer);
            record_water_2d_draw(recorder.handle(), *resource_ptr, *config_ptr, debug_view,
                                 cubey::render::color_target_view(color_target.extent,
                                                                  color_target.format,
                                                                  resolved.image, resolved.view));
        });

    return graph.compile();
}

} // namespace cubey::projects::fluid::water_2d
