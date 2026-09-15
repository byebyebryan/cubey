#include "fluid_25d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/render_graph_resolve.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/memory_barriers.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {

inline constexpr std::uint32_t kFluid25DComputeGroupSize = 8U;

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_cell{};
    std::array<float, 4> physics{};
};
struct RenderPushConstants {
    std::array<float, 4> grid_debug{};
};
struct CatchmentPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 grid_cell{};
    cubey::math::Vec4 camera_wet{};
};

static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);
static_assert(sizeof(CatchmentPushConstants) == sizeof(float) * 24U);

[[nodiscard]] SimulationPushConstants simulation_push_constants(const Fluid25DConfig& config) {
    const float substep_delta =
        config.fixed_delta_seconds / static_cast<float>(config.simulation_substeps);
    return {
        .grid_dt_cell = {static_cast<float>(config.grid_width),
                         static_cast<float>(config.grid_height), substep_delta, config.cell_size_m},
        .physics = {config.gravity_m_per_s2, config.flow_damping_per_second,
                    config.minimum_wet_depth_m, 0.0F},
    };
}

[[nodiscard]] cubey::render::ComputeDispatchGroups dispatch_groups(const Fluid25DConfig& config) {
    return cubey::render::ceil_dispatch_groups(config.grid_width, config.grid_height,
                                               kFluid25DComputeGroupSize);
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set,
                     const cubey::render::ComputeDispatchGroups& groups,
                     const SimulationPushConstants& push_constants) {
    cubey::render::record_compute_pipeline_dispatch(
        recorder, cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set, groups),
        VK_SHADER_STAGE_COMPUTE_BIT, push_constants);
}

void record_reset(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                  const cubey::render::ComputeDispatchGroups& groups,
                  const SimulationPushConstants& push_constants) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    record_dispatch(recorder, resources.reset_pipeline(), resources.reset_descriptor_set(), groups,
                    push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
    resources.reset_depth_parity();
}

[[nodiscard]] CatchmentPushConstants catchment_push_constants(const Fluid25DConfig& config,
                                                              const Fluid25DRenderCamera& camera) {
    return {
        .view_projection = camera.view_projection,
        .grid_cell = {static_cast<float>(config.grid_width), static_cast<float>(config.grid_height),
                      config.cell_size_m, kFluid25DCatchmentHeightScale},
        .camera_wet = {camera.position.x, camera.position.y, camera.position.z,
                       config.minimum_wet_depth_m},
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc catchment_depth_desc(VkExtent2D extent) {
    return {
        .label = "fluid 2.5D catchment depth",
        .extent = {extent.width, extent.height, 1U},
        .format = VK_FORMAT_D32_SFLOAT,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

} // namespace

void record_fluid_25d_compute(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                              const Fluid25DConfig& config, bool paused, bool& reset_requested,
                              bool include_render_visibility_barrier,
                              cubey::vulkan::GpuTimestampProfiler* profiler,
                              std::uint32_t frame_slot_index) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    // Keep every fixed substep inside one aggregate solve span. This is the
    // product budget used by both windowed and headless evidence lanes, rather
    // than a per-dispatch or per-substep trace.
    cubey::vulkan::GpuTimestampScope profile_scope(profiler, command_buffer, frame_slot_index,
                                                   "fluid_25d solver");
    const SimulationPushConstants push_constants = simulation_push_constants(config);
    const cubey::render::ComputeDispatchGroups groups = dispatch_groups(config);

    if (reset_requested) {
        record_reset(command_buffer, resources, groups, push_constants);
        reset_requested = false;
    }
    if (paused) {
        return;
    }

    for (std::uint32_t substep = 0; substep < config.simulation_substeps; ++substep) {
        const bool source_is_a = resources.current_depth_is_a();
        record_dispatch(recorder, resources.flux_pipeline(),
                        resources.flux_descriptor_set(source_is_a), groups, push_constants);
        // The flux solve writes directed faces and the source/sink ledger;
        // the next dispatch gathers those faces without scatter writes.
        cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
        record_dispatch(recorder, resources.depth_pipeline(),
                        resources.depth_descriptor_set(source_is_a), groups, push_constants);
        cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
        resources.advance_depth_parity();
    }
    if (include_render_visibility_barrier) {
        cubey::vulkan::record_compute_render_shader_write_barrier(command_buffer);
    }
}

void record_fluid_25d_fullscreen_draw(VkCommandBuffer command_buffer,
                                      const Fluid25DGpuResources& resources,
                                      const Fluid25DConfig& config, Fluid25DDebugView debug_view,
                                      cubey::render::ColorTargetView color_target) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const RenderPushConstants push_constants{
        .grid_debug = {static_cast<float>(config.grid_width),
                       static_cast<float>(config.grid_height),
                       static_cast<float>(static_cast<std::uint32_t>(debug_view)),
                       config.minimum_wet_depth_m},
    };
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.009F, 0.014F, 0.022F, 1.0F),
        },
        [&resources, push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &resources.diagnostic_pipeline(),
                    .descriptor_set = resources.render_descriptor_set(),
                },
                VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

void record_fluid_25d_catchment_draw(VkCommandBuffer command_buffer,
                                     const Fluid25DGpuResources& resources,
                                     const Fluid25DConfig& config,
                                     const Fluid25DRenderCamera& camera,
                                     cubey::render::ColorTargetView color_target,
                                     cubey::render::DepthTargetView depth_target) {
    const std::size_t vertex_count = fluid_25d_mesh_vertex_count(config);
    if (vertex_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("fluid 2.5D product mesh vertex count exceeds Vulkan draw range");
    }
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const CatchmentPushConstants push_constants = catchment_push_constants(config, camera);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target, depth_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.018F, 0.030F, 0.046F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [&resources, push_constants,
         vertex_count](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const VkDescriptorSet descriptor_set = resources.render_descriptor_set();
            const cubey::render::GraphicsPipelineResource& terrain = resources.terrain_pipeline();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, terrain.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, terrain.layout(), 0U,
                                              descriptor_set);
            pass_recorder.push_constants(terrain.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0U, push_constants);
            pass_recorder.draw(static_cast<std::uint32_t>(vertex_count));

            const cubey::render::GraphicsPipelineResource& water = resources.water_pipeline();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, water.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, water.layout(), 0U,
                                              descriptor_set);
            pass_recorder.push_constants(water.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0U, push_constants);
            pass_recorder.draw(static_cast<std::uint32_t>(vertex_count));
        });
}

[[nodiscard]] cubey::render::CompiledRenderGraph build_fluid_25d_frame_graph(
    cubey::render::ColorTargetView color_target, Fluid25DGpuResources& resources,
    const Fluid25DConfig& config, Fluid25DPresentationView presentation_view,
    Fluid25DDebugView debug_view, const Fluid25DRenderCamera& camera,
    Fluid25DRenderTargetMode target_mode, bool include_simulation, bool paused,
    bool& reset_requested, cubey::vulkan::GpuTimestampProfiler* profiler,
    std::uint32_t frame_slot_index) {
    Fluid25DGpuResources* resource_ptr = &resources;
    const Fluid25DConfig* config_ptr = &config;
    bool* reset_requested_ptr = &reset_requested;
    cubey::render::RenderGraphBuilder graph;
    const auto import = [&graph](const char* label, const cubey::vulkan::Buffer& buffer) {
        return graph.import_buffer({.label = label, .byte_size = buffer.size()}, buffer.handle());
    };
    const cubey::render::RenderGraphBufferHandle terrain =
        import("fluid 2.5D terrain", resources.terrain());
    const cubey::render::RenderGraphBufferHandle source =
        import("fluid 2.5D source rate", resources.source_rate());
    const cubey::render::RenderGraphBufferHandle sink =
        import("fluid 2.5D sink rate", resources.sink_rate());
    const cubey::render::RenderGraphBufferHandle initial =
        import("fluid 2.5D initial depth", resources.initial_depth());
    const cubey::render::RenderGraphBufferHandle depth_a =
        import("fluid 2.5D depth A", resources.depth_a());
    const cubey::render::RenderGraphBufferHandle depth_b =
        import("fluid 2.5D depth B", resources.depth_b());
    const cubey::render::RenderGraphBufferHandle flux =
        import("fluid 2.5D face flux", resources.flux());
    const cubey::render::RenderGraphBufferHandle velocity =
        import("fluid 2.5D velocity", resources.velocity());
    const cubey::render::RenderGraphBufferHandle ledger =
        import("fluid 2.5D ledger", resources.ledger());
    const cubey::render::RenderGraphTextureState initial_state =
        target_mode == Fluid25DRenderTargetMode::Present
            ? cubey::render::render_graph_undefined_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureState final_state =
        target_mode == Fluid25DRenderTargetMode::Present
            ? cubey::render::render_graph_present_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "fluid 2.5D backbuffer", color_target, initial_state, final_state);

    if (include_simulation) {
        graph.add_pass("fluid_25d simulation", cubey::render::RenderGraphQueueDomain::Compute)
            .read_storage_buffer(terrain)
            .read_storage_buffer(source)
            .read_storage_buffer(sink)
            .read_storage_buffer(initial)
            .read_write_storage_buffer(depth_a)
            .read_write_storage_buffer(depth_b)
            .read_write_storage_buffer(flux)
            .read_write_storage_buffer(velocity)
            .read_write_storage_buffer(ledger)
            .execute([resource_ptr, config_ptr, paused, reset_requested_ptr, profiler,
                      frame_slot_index](const cubey::render::RenderGraphExecutionContext& context) {
                record_fluid_25d_compute(context.recorder().handle(), *resource_ptr, *config_ptr,
                                         paused, *reset_requested_ptr, false, profiler,
                                         frame_slot_index);
            });
    }

    if (presentation_view == Fluid25DPresentationView::Diagnostics) {
        graph.add_pass("fluid_25d diagnostics", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_storage_buffer(terrain)
            // The actual current depth is parity-dependent, so both ping-pong
            // buffers are declared. The descriptor is chosen after the compute pass.
            .read_storage_buffer(depth_a)
            .read_storage_buffer(depth_b)
            .read_storage_buffer(velocity)
            .write_color(backbuffer)
            .execute([resource_ptr, config_ptr, debug_view,
                      backbuffer](const cubey::render::RenderGraphExecutionContext& context) {
                record_fluid_25d_fullscreen_draw(
                    context.recorder().handle(), *resource_ptr, *config_ptr, debug_view,
                    cubey::render::resolved_color_target_view(context, backbuffer));
            });
    } else {
        const cubey::render::RenderGraphTextureHandle catchment_depth =
            graph.create_texture(catchment_depth_desc(color_target.extent));
        graph.add_pass("fluid_25d catchment", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_storage_buffer(terrain)
            .read_storage_buffer(depth_a)
            .read_storage_buffer(depth_b)
            .read_storage_buffer(velocity)
            .write_color(backbuffer)
            .write_depth(catchment_depth)
            .execute([resource_ptr, config_ptr, camera, backbuffer,
                      catchment_depth](const cubey::render::RenderGraphExecutionContext& context) {
                record_fluid_25d_catchment_draw(
                    context.recorder().handle(), *resource_ptr, *config_ptr, camera,
                    cubey::render::resolved_color_target_view(context, backbuffer),
                    cubey::render::resolved_depth_target_view(context, catchment_depth));
            });
    }
    return graph.compile();
}

} // namespace cubey::projects::fluid::fluid_25d
