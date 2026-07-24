#include "pyro_3d_commands.h"

#include <cubey/engine/terrain_backdrop_runtime.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/memory_barriers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
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
    std::array<float, 4> style_options{};
    std::array<float, 4> color_options{};
};

static_assert(sizeof(SimulationPushConstants) ==
              sizeof(float) * kPyro3DSimulationPushConstantFloatCount);
static_assert(sizeof(RenderPushConstants) == sizeof(float) * kPyro3DRenderPushConstantFloatCount);

using DispatchGroups = cubey::render::ComputeDispatchGroups;

[[nodiscard]] DispatchGroups compute_dispatch_groups(VkExtent3D extent, std::uint32_t group_size) {
    return cubey::render::ceil_dispatch_groups(extent.width, extent.height, extent.depth,
                                               group_size);
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
    cubey::vulkan::record_transfer_write_barrier(
        command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
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
                static_cast<float>(static_cast<std::uint32_t>(config.mode)),
                0.0F,
            },
    };
}

[[nodiscard]] RenderPushConstants render_push_constants(const Pyro3DConfig& config,
                                                        Pyro3DDebugView debug_view,
                                                        const Pyro3DRenderCamera& camera,
                                                        VkExtent2D extent,
                                                        bool atmosphere_background_enabled) {
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
                static_cast<float>(static_cast<std::uint32_t>(config.mode)),
                camera.far_plane_m,
            },
        .style_options =
            {
                camera.near_plane_m,
                config.render_background_lift,
                config.render_rim_strength,
                config.render_scatter_strength,
            },
        .color_options =
            {
                config.render_smoke_warmth,
                config.render_flame_intensity,
                config.render_flame_core_strength,
                atmosphere_background_enabled ? 1.0F : 0.0F,
            },
    };
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set, const DispatchGroups& groups,
                     const SimulationPushConstants& push_constants) {
    cubey::render::record_compute_pipeline_dispatch(
        recorder, cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set, groups),
        VK_SHADER_STAGE_COMPUTE_BIT, push_constants);
}

void record_compute_barrier(VkCommandBuffer command_buffer) {
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
}

void record_render_visibility_barrier(VkCommandBuffer command_buffer) {
    cubey::vulkan::record_compute_render_shader_write_barrier(command_buffer);
}

} // namespace

void record_pyro_3d_compute(VkCommandBuffer command_buffer, Pyro3DGpuResources& resources,
                            const Pyro3DConfig& config, bool paused, bool& reset_requested,
                            const ProjectFrame& frame, std::span<const Pyro3DSourceGpu> sources,
                            Pyro3DFrameState& frame_state, bool include_render_visibility_barrier,
                            cubey::vulkan::GpuTimestampProfiler* profiler,
                            std::uint32_t frame_slot_index) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    if (!frame_state.volumes_initialized) {
        record_initial_volume_layouts(command_buffer, resources);
        frame_state.volumes_initialized = true;
        reset_requested = true;
    }

    const SimulationPushConstants push_constants = simulation_push_constants(config, frame);
    const DispatchGroups groups =
        compute_dispatch_groups(solver_extent(config), kPyro3DComputeGroupSize);
    const DispatchGroups shadow_groups =
        compute_dispatch_groups(shadow_extent(config), kPyro3DComputeGroupSize);
    const DispatchGroups reset_groups = compute_dispatch_groups(
        max_extent(solver_extent(config), shadow_extent(config)), kPyro3DComputeGroupSize);
    auto record_profiled_dispatch =
        [&](const char* label, const cubey::render::ComputePipelineResource& pipeline,
            VkDescriptorSet descriptor_set, const DispatchGroups& dispatch_groups) {
            cubey::render::record_profiled_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set,
                                                              dispatch_groups),
                VK_SHADER_STAGE_COMPUTE_BIT, push_constants, profiler, frame_slot_index, label);
        };

    if (reset_requested) {
        const cubey::render::ComputePipelineResource& reset_pipeline = resources.reset_pipeline();
        record_profiled_dispatch("reset", reset_pipeline, resources.reset_descriptor_set(),
                                 reset_groups);
        frame_state.density_a_current = true;
        frame_state.velocity_a_current = true;
        frame_state.shadow_initialized = false;
        frame_state.frames_since_shadow_update = 0;
        reset_requested = false;
        record_render_visibility_barrier(command_buffer);
    }

    if (paused) {
        return;
    }

    record_source_buffer_update(command_buffer, resources, sources);

    const cubey::render::ComputePipelineResource& advect_pipeline = resources.advect_pipeline();
    record_profiled_dispatch("advect_predict", advect_pipeline,
                             resources.advect_descriptor_set(frame_state.density_a_current,
                                                             frame_state.velocity_a_current),
                             groups);
    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& advect_correct_pipeline =
        resources.advect_correct_pipeline();
    record_profiled_dispatch("advect_correct", advect_correct_pipeline,
                             resources.advect_correct_descriptor_set(
                                 frame_state.density_a_current, frame_state.velocity_a_current),
                             groups);
    frame_state.density_a_current = !frame_state.density_a_current;
    frame_state.velocity_a_current = !frame_state.velocity_a_current;
    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& combustion_pipeline =
        resources.combustion_pipeline();
    record_profiled_dispatch("combustion", combustion_pipeline,
                             resources.combustion_descriptor_set(frame_state.density_a_current,
                                                                 frame_state.velocity_a_current),
                             groups);
    frame_state.density_a_current = !frame_state.density_a_current;
    frame_state.velocity_a_current = !frame_state.velocity_a_current;
    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& divergence_pipeline =
        resources.divergence_pipeline();
    record_profiled_dispatch("divergence", divergence_pipeline,
                             resources.divergence_descriptor_set(frame_state.density_a_current,
                                                                 frame_state.velocity_a_current),
                             groups);
    record_compute_barrier(command_buffer);

    const cubey::render::ComputePipelineResource& pressure_pipeline = resources.pressure_pipeline();
    cubey::vulkan::GpuTimestampScope pressure_scope(profiler, command_buffer, frame_slot_index,
                                                    "pressure");
    for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
        const VkDescriptorSet descriptor_set = (iteration % 2U == 0)
                                                   ? resources.pressure_a_to_b_descriptor_set()
                                                   : resources.pressure_b_to_a_descriptor_set();
        record_dispatch(recorder, pressure_pipeline, descriptor_set, groups, push_constants);
        record_compute_barrier(command_buffer);
    }
    pressure_scope.end();

    const bool pressure_a_current = (config.pressure_iterations % 2U) == 0;
    const cubey::render::ComputePipelineResource& projection_pipeline =
        resources.projection_pipeline();
    record_profiled_dispatch(
        "projection", projection_pipeline,
        resources.projection_descriptor_set(frame_state.velocity_a_current, pressure_a_current),
        groups);
    frame_state.velocity_a_current = !frame_state.velocity_a_current;

    const std::uint32_t shadow_interval = std::max(config.shadow_update_interval, 1U);
    const bool update_shadow = !frame_state.shadow_initialized ||
                               frame_state.frames_since_shadow_update >= shadow_interval - 1U;
    if (update_shadow) {
        const cubey::render::ComputePipelineResource& shadow_pipeline = resources.shadow_pipeline();
        cubey::vulkan::GpuTimestampScope shadow_scope(profiler, command_buffer, frame_slot_index,
                                                      "shadow");
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, shadow_pipeline.pipeline());
        const std::array<VkDescriptorSet, 2> sets{
            resources.shadow_descriptor_set(frame_state.density_a_current),
            resources.environment_descriptor_set(frame_slot_index),
        };
        recorder.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE, shadow_pipeline.layout(), 0,
                                      sets);
        recorder.push_constants(shadow_pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                push_constants);
        recorder.dispatch(shadow_groups.x, shadow_groups.y, shadow_groups.z);
        shadow_scope.end();
        frame_state.shadow_initialized = true;
        frame_state.frames_since_shadow_update = 0;
    } else {
        ++frame_state.frames_since_shadow_update;
    }

    if (include_render_visibility_barrier) {
        record_render_visibility_barrier(command_buffer);
    }
}

namespace {

[[nodiscard]] cubey::render::RenderGraphTextureDesc pyro_scene_color_desc(VkExtent2D extent) {
    return {
        .label = "pyro scene color",
        .extent = {extent.width, extent.height, 1U},
        .format = kPyro3DSceneColorFormat,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc pyro_scene_depth_desc(VkExtent2D extent) {
    return {
        .label = "pyro scene depth",
        .extent = {extent.width, extent.height, 1U},
        .format = kPyro3DSceneDepthFormat,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

void record_pyro_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                            Pyro3DGpuResources& resources, cubey::render::FrameSlot frame_slot,
                            cubey::render::ColorTargetView color_target,
                            cubey::render::DepthTargetView depth_target,
                            bool atmosphere_background_enabled, bool moon_body_enabled,
                            cubey::TerrainBackdropRuntime* terrain) {
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target, depth_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.008F, 0.012F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [&resources, frame_slot, atmosphere_background_enabled, moon_body_enabled,
         terrain](const cubey::vulkan::CommandRecorder& pass_recorder) {
            if (atmosphere_background_enabled) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder,
                    cubey::render::FullscreenPipelineDrawInfo{
                        .pipeline = &resources.atmosphere_background().pipeline(),
                        .descriptor_set =
                            resources.atmosphere_background_descriptor_set(frame_slot.index),
                    });
            }
            if (moon_body_enabled) {
                resources.moon_body_frame().record_draw(pass_recorder, frame_slot,
                                                        resources.moon_mesh());
            }
            if (terrain != nullptr) {
                terrain->record_surface_draws(pass_recorder, frame_slot);
            }
        });
}

void record_pyro_raymarch_pass(const cubey::vulkan::CommandRecorder& recorder,
                               Pyro3DGpuResources& resources, const Pyro3DConfig& config,
                               Pyro3DDebugView debug_view, const Pyro3DRenderCamera& camera,
                               cubey::render::ColorTargetView color_target,
                               const Pyro3DFrameState& frame_state,
                               cubey::render::FrameSlot frame_slot,
                               bool atmosphere_background_enabled) {
    const RenderPushConstants push_constants = render_push_constants(
        config, debug_view, camera, color_target.extent, atmosphere_background_enabled);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.008F, 0.012F, 1.0F),
        },
        [&resources, &frame_state, frame_slot,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline = resources.render_pipeline();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            const std::array<VkDescriptorSet, 3> sets{
                resources.render_descriptor_set(frame_state.density_a_current,
                                                frame_state.velocity_a_current),
                resources.environment_descriptor_set(frame_slot.index),
                resources.scene_descriptor_set(frame_slot.index),
            };
            pass_recorder.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(),
                                               0, sets);
            pass_recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                         push_constants);
            cubey::render::record_fullscreen_triangle(pass_recorder);
        });
}

struct Pyro3DRenderGraph {
    cubey::render::CompiledRenderGraph graph;
    cubey::render::RenderGraphTextureHandle scene_color{};
    cubey::render::RenderGraphTextureHandle scene_depth{};
};

[[nodiscard]] Pyro3DRenderGraph
build_pyro_3d_render_graph(cubey::render::ColorTargetView color_target,
                           Pyro3DGpuResources& resources, const Pyro3DConfig& config,
                           Pyro3DDebugView debug_view, const Pyro3DRenderCamera& camera,
                           Pyro3DRenderTargetMode target_mode, const Pyro3DFrameState& frame_state,
                           cubey::render::FrameSlot frame_slot, bool atmosphere_background_enabled,
                           bool moon_body_enabled, cubey::TerrainBackdropRuntime* terrain) {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureState initial_state =
        target_mode == Pyro3DRenderTargetMode::Present
            ? cubey::render::render_graph_undefined_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureState final_state =
        target_mode == Pyro3DRenderTargetMode::Present
            ? cubey::render::render_graph_present_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("pyro backbuffer", color_target, initial_state, final_state);
    const cubey::render::RenderGraphTextureHandle scene_color =
        graph.create_texture(pyro_scene_color_desc(color_target.extent));
    const cubey::render::RenderGraphTextureHandle scene_depth =
        graph.create_texture(pyro_scene_depth_desc(color_target.extent));

    cubey::render::RenderGraphTextureHandle terrain_shadow_depth{};
    if (terrain != nullptr) {
        const std::optional<cubey::render::RenderGraphTextureState> terrain_shadow_initial_state =
            terrain->shadow_depth_is_sampled()
                ? cubey::render::render_graph_sampled_depth_texture_state()
                : cubey::render::render_graph_undefined_texture_state();
        terrain_shadow_depth =
            graph.import_depth_target("pyro terrain shadow depth", terrain->shadow_depth_target(),
                                      terrain_shadow_initial_state);
        if (terrain->shadow_update_this_frame()) {
            graph.add_pass("pyro terrain shadow", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_depth(terrain_shadow_depth)
                .material_pass(terrain->shadow_material_pass())
                .execute([terrain](const cubey::render::RenderGraphExecutionContext& context) {
                    terrain->record_shadow_pass(context.recorder());
                });
        }
    }

    auto scene_pass = graph.add_pass("pyro scene", cubey::render::RenderGraphQueueDomain::Graphics)
                          .write_color(scene_color)
                          .write_depth(scene_depth);
    if (terrain != nullptr) {
        scene_pass.read_texture(terrain_shadow_depth);
    }
    scene_pass.execute([&resources, frame_slot, scene_color, scene_depth,
                        atmosphere_background_enabled, moon_body_enabled,
                        terrain](const cubey::render::RenderGraphExecutionContext& context) {
        record_pyro_scene_pass(context.recorder(), resources, frame_slot,
                               cubey::render::resolved_color_target_view(context, scene_color),
                               cubey::render::resolved_depth_target_view(context, scene_depth),
                               atmosphere_background_enabled, moon_body_enabled, terrain);
    });

    graph.add_pass("pyro raymarch", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(scene_color)
        .read_texture(scene_depth)
        .write_color(backbuffer)
        .execute([&resources, &config, debug_view, camera, frame_state, frame_slot, backbuffer,
                  atmosphere_background_enabled](
                     const cubey::render::RenderGraphExecutionContext& context) {
            record_pyro_raymarch_pass(
                context.recorder(), resources, config, debug_view, camera,
                cubey::render::resolved_color_target_view(context, backbuffer), frame_state,
                frame_slot, atmosphere_background_enabled);
        });

    return {
        .graph = graph.compile(),
        .scene_color = scene_color,
        .scene_depth = scene_depth,
    };
}

} // namespace

void record_pyro_3d_draw(VkCommandBuffer command_buffer, const cubey::vulkan::Device& device,
                         cubey::render::RenderGraphFrameExecutor& graph_executor,
                         Pyro3DGpuResources& resources, const Pyro3DConfig& config,
                         Pyro3DDebugView debug_view, const Pyro3DRenderCamera& camera,
                         cubey::render::ColorTargetView color_target,
                         Pyro3DRenderTargetMode target_mode, const Pyro3DFrameState& frame_state,
                         cubey::TerrainBackdropRuntime* terrain,
                         cubey::vulkan::GpuTimestampProfiler* profiler,
                         std::uint32_t frame_slot_index, bool atmosphere_background_enabled,
                         bool moon_body_enabled) {
    const cubey::render::FrameSlot frame_slot{
        .index = frame_slot_index,
        .count = resources.frame_slot_count(),
    };
    const Pyro3DRenderGraph render_graph = build_pyro_3d_render_graph(
        color_target, resources, config, debug_view, camera, target_mode, frame_state, frame_slot,
        atmosphere_background_enabled, moon_body_enabled, terrain);
    graph_executor.record(
        cubey::render::RenderGraphFrameRecordInfo{
            .device = &device,
            .command_buffer = command_buffer,
            .frame_slot = frame_slot,
            .label = "vkEndCommandBuffer pyro_3d render graph",
            .command_buffer_mode = cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            .profiler = profiler,
        },
        render_graph.graph,
        [&resources, &device, frame_slot,
         &render_graph](const cubey::render::RenderGraphResourceSet& graph_resources) {
            resources.update_scene_descriptors(
                device, frame_slot.index,
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.scene_color),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.scene_depth));
        });
    if (terrain != nullptr) {
        terrain->complete_frame();
    }
}

} // namespace cubey::projects::fluid::pyro_3d
