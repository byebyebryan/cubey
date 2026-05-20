#include "smoke_2d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::projects::fluid::smoke_2d {
namespace {

using cubey::vulkan::vk_struct;

struct RenderPushConstants {
    std::array<float, 4> grid_debug{};
};

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_time{};
    std::array<float, 4> decay_options{};
    std::array<float, 4> solver_options{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);
static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 12U);

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

[[nodiscard]] DispatchGroups compute_dispatch_groups(const Smoke2DConfig& config) {
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
    record_transfer_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT,
                        });
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
    };
}

void record_field_reset(VkCommandBuffer command_buffer, const Smoke2DGpuResources& resources) {
    vkCmdFillBuffer(command_buffer, resources.field_a().handle(), 0, resources.field_a().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.field_b().handle(), 0, resources.field_b().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.field_temp().handle(), 0, resources.field_temp().size(),
                    0);
    vkCmdFillBuffer(command_buffer, resources.divergence().handle(), 0,
                    resources.divergence().size(), 0);
    vkCmdFillBuffer(command_buffer, resources.curl().handle(), 0, resources.curl().size(), 0);
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

void record_smoke_compute(VkCommandBuffer command_buffer, Smoke2DGpuResources& resources,
                          const Smoke2DConfig& config, bool paused,
                          bool& reset_requested, const ProjectFrame& frame,
                          std::span<const Smoke2DInjectorGpu> injectors,
                          bool include_render_visibility_barrier) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);

    if (reset_requested) {
        record_field_reset(command_buffer, resources);
        reset_requested = false;
    }
    if (paused) {
        return;
    }

    record_injector_buffer_update(command_buffer, resources, injectors);

    const SimulationPushConstants push_constants = simulation_push_constants(config, frame);
    const DispatchGroups groups = compute_dispatch_groups(config);

    const cubey::render::ComputePipelineResource& advect_pipeline =
        resources.advect_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, advect_pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, advect_pipeline.layout(), 0,
                                 resources.advect_descriptor_set());
    recorder.push_constants(advect_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    const cubey::render::ComputePipelineResource& advect_correct_pipeline =
        resources.advect_correct_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, advect_correct_pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, advect_correct_pipeline.layout(),
                                 0, resources.advect_correct_descriptor_set());
    recorder.push_constants(advect_correct_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    const cubey::render::ComputePipelineResource& inject_pipeline =
        resources.inject_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, inject_pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, inject_pipeline.layout(), 0,
                                 resources.inject_descriptor_set());
    recorder.push_constants(inject_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    const cubey::render::ComputePipelineResource& curl_pipeline = resources.curl_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, curl_pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, curl_pipeline.layout(), 0,
                                 resources.curl_descriptor_set());
    recorder.push_constants(curl_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    const cubey::render::ComputePipelineResource& vorticity_pipeline =
        resources.vorticity_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, vorticity_pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, vorticity_pipeline.layout(), 0,
                                 resources.vorticity_descriptor_set());
    recorder.push_constants(vorticity_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    const cubey::render::ComputePipelineResource& divergence_pipeline =
        resources.divergence_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, divergence_pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, divergence_pipeline.layout(), 0,
                                 resources.divergence_descriptor_set());
    recorder.push_constants(divergence_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });

    const cubey::render::ComputePipelineResource& pressure_pipeline =
        resources.pressure_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pressure_pipeline.pipeline());
    for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
        const VkDescriptorSet descriptor_set = (iteration % 2U == 0)
                                                   ? resources.pressure_a_to_b_descriptor_set()
                                                   : resources.pressure_b_to_a_descriptor_set();
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pressure_pipeline.layout(), 0,
                                     descriptor_set);
        recorder.push_constants(pressure_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                push_constants);
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
    const cubey::render::ComputePipelineResource& projection_pipeline =
        resources.projection_pipeline_resource();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, projection_pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, projection_pipeline.layout(), 0,
                                 projection_descriptor_set);
    recorder.push_constants(projection_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch(groups.x, groups.y, 1);

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

void record_fullscreen_draw(VkCommandBuffer command_buffer, const Smoke2DGpuResources& resources,
                            const Smoke2DConfig& config, Smoke2DDebugView debug_view,
                            cubey::render::ColorTargetView color_target) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const RenderPushConstants push_constants{
        .grid_debug =
            {
                static_cast<float>(config.grid_width),
                static_cast<float>(config.grid_height),
                debug_view_push_value(debug_view),
                (config.pressure_iterations % 2U) == 0 ? 0.0F : 1.0F,
            },
    };

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

[[nodiscard]] cubey::render::CompiledRenderGraph
build_smoke_frame_graph(cubey::render::ColorTargetView color_target, Smoke2DGpuResources& resources,
                        const Smoke2DConfig& config, Smoke2DDebugView debug_view,
                        bool paused, bool& reset_requested, const ProjectFrame& frame,
                        std::span<const Smoke2DInjectorGpu> injectors) {
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
    const cubey::render::RenderGraphBufferHandle field_temp =
        graph.import_buffer(
            {.label = "smoke field temp", .byte_size = resources.field_temp().size()},
            resources.field_temp().handle());
    const cubey::render::RenderGraphBufferHandle divergence = graph.import_buffer(
        {.label = "fluid divergence", .byte_size = resources.divergence().size()},
        resources.divergence().handle());
    const cubey::render::RenderGraphBufferHandle curl =
        graph.import_buffer({.label = "fluid curl", .byte_size = resources.curl().size()},
                            resources.curl().handle());
    const cubey::render::RenderGraphBufferHandle obstacle =
        graph.import_buffer({.label = "fluid obstacle", .byte_size = resources.obstacle().size()},
                            resources.obstacle().handle());
    const cubey::render::RenderGraphBufferHandle injector_buffer = graph.import_buffer(
        {.label = "smoke injectors", .byte_size = resources.injectors().size()},
        resources.injectors().handle());
    const cubey::render::RenderGraphBufferHandle pressure_a = graph.import_buffer(
        {.label = "fluid pressure A", .byte_size = resources.pressure_a().size()},
        resources.pressure_a().handle());
    const cubey::render::RenderGraphBufferHandle pressure_b = graph.import_buffer(
        {.label = "fluid pressure B", .byte_size = resources.pressure_b().size()},
        resources.pressure_b().handle());
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("backbuffer", color_target,
                                  cubey::render::render_graph_undefined_texture_state(),
                                  cubey::render::render_graph_present_texture_state());

    graph.add_pass("fluid simulation", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(field_a)
        .read_write_storage_buffer(field_b)
        .read_write_storage_buffer(field_temp)
        .read_write_storage_buffer(divergence)
        .read_write_storage_buffer(curl)
        .read_storage_buffer(obstacle)
        .read_write_storage_buffer(injector_buffer)
        .read_write_storage_buffer(pressure_a)
        .read_write_storage_buffer(pressure_b)
        .execute([resource_ptr, config_ptr, paused, reset_requested_ptr, frame,
                  injector_snapshot](const cubey::render::RenderGraphExecutionContext& context) {
            record_smoke_compute(context.recorder().handle(), *resource_ptr, *config_ptr, paused,
                                 *reset_requested_ptr, frame, injector_snapshot, false);
        });
    graph.add_pass("fluid render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(field_a)
        .read_storage_buffer(divergence)
        .read_storage_buffer(curl)
        .read_storage_buffer(obstacle)
        .read_storage_buffer(pressure_a)
        .read_storage_buffer(pressure_b)
        .write_color(backbuffer)
        .execute([resource_ptr, config_ptr, debug_view, backbuffer,
                  color_target](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder& recorder = context.recorder();
            const cubey::render::RenderGraphResolvedTexture resolved =
                context.resolved_texture(backbuffer);
            record_fullscreen_draw(recorder.handle(), *resource_ptr, *config_ptr, debug_view,
                                   cubey::render::color_target_view(color_target.extent,
                                                                    color_target.format,
                                                                    resolved.image, resolved.view));
        });

    return graph.compile();
}

} // namespace cubey::projects::fluid::smoke_2d
