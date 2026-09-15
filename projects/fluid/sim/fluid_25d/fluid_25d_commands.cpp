#include "fluid_25d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/memory_barriers.h>

#include <array>
#include <cstdint>

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

static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);

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

} // namespace

void record_fluid_25d_compute(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                              const Fluid25DConfig& config, bool paused, bool& reset_requested,
                              bool include_render_visibility_barrier) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
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
                    .pipeline = &resources.render_pipeline(),
                    .descriptor_set = resources.render_descriptor_set(),
                },
                VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

[[nodiscard]] cubey::render::CompiledRenderGraph
build_fluid_25d_frame_graph(cubey::render::ColorTargetView color_target,
                            Fluid25DGpuResources& resources, const Fluid25DConfig& config,
                            Fluid25DDebugView debug_view, bool paused, bool& reset_requested) {
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
    const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "backbuffer", color_target, cubey::render::render_graph_undefined_texture_state(),
        cubey::render::render_graph_present_texture_state());

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
        .execute([resource_ptr, config_ptr, paused,
                  reset_requested_ptr](const cubey::render::RenderGraphExecutionContext& context) {
            record_fluid_25d_compute(context.recorder().handle(), *resource_ptr, *config_ptr,
                                     paused, *reset_requested_ptr, false);
        });
    graph.add_pass("fluid_25d diagnostics", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(terrain)
        // The actual current depth is parity-dependent, so both ping-pong
        // buffers are declared. The descriptor is chosen after the compute pass.
        .read_storage_buffer(depth_a)
        .read_storage_buffer(depth_b)
        .read_storage_buffer(velocity)
        .write_color(backbuffer)
        .execute([resource_ptr, config_ptr, debug_view, backbuffer,
                  color_target](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::render::RenderGraphResolvedTexture resolved =
                context.resolved_texture(backbuffer);
            record_fluid_25d_fullscreen_draw(
                context.recorder().handle(), *resource_ptr, *config_ptr, debug_view,
                cubey::render::color_target_view(color_target.extent, color_target.format,
                                                 resolved.image, resolved.view));
        });
    return graph.compile();
}

} // namespace cubey::projects::fluid::fluid_25d
