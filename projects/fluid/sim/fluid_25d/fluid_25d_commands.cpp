#include "fluid_25d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/render_graph_resolve.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/memory_barriers.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {

inline constexpr std::uint32_t kFluid25DComputeGroupSize = 8U;
inline constexpr std::uint32_t kFluid25DQuiverComputeGroupSize = 64U;

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
    cubey::math::Vec4 presentation{};
};

// Direct headless stepping is numerical evidence and must keep its historical
// solver-only command stream and timing span. The frame-graph presentation
// path opts into the render-only cue explicitly.
enum class Fluid25DPresentationCuePolicy : std::uint8_t {
    Disabled,
    WindowedPresentation,
};

enum class Fluid25DQuiverPolicy : std::uint8_t {
    Disabled,
    FlowInspection,
};

struct Fluid25DComputeRecordingPolicy {
    Fluid25DPresentationCuePolicy presentation_cue = Fluid25DPresentationCuePolicy::Disabled;
    bool* presentation_cue_reset_requested = nullptr;
    Fluid25DQuiverPolicy quiver = Fluid25DQuiverPolicy::Disabled;
    bool* quiver_reset_requested = nullptr;
};

static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);
static_assert(sizeof(CatchmentPushConstants) == sizeof(float) * 28U);

[[nodiscard]] SimulationPushConstants simulation_push_constants(const Fluid25DConfig& config,
                                                                float source_rate_scale) {
    if (!std::isfinite(source_rate_scale) || source_rate_scale < 0.0F) {
        throw std::runtime_error("fluid 2.5D source rate scale must be finite and nonnegative");
    }
    const float substep_delta =
        config.fixed_delta_seconds / static_cast<float>(config.simulation_substeps);
    return {
        .grid_dt_cell = {static_cast<float>(config.grid_width),
                         static_cast<float>(config.grid_height), substep_delta, config.cell_size_m},
        .physics = {config.gravity_m_per_s2, config.flow_damping_per_second,
                    config.minimum_wet_depth_m, source_rate_scale},
    };
}

[[nodiscard]] SimulationPushConstants
presentation_cue_push_constants(const Fluid25DConfig& config) {
    SimulationPushConstants push_constants = simulation_push_constants(config, 0.0F);
    // The presentation field advances once per outer fixed step, after all
    // solver substeps have published their final velocity/depth state.
    push_constants.grid_dt_cell[2] = config.fixed_delta_seconds;
    // Source scale is irrelevant to render-only compute; presentation shaders
    // deliberately ignore this otherwise-reserved word.
    push_constants.physics[3] = 0.0F;
    return push_constants;
}

[[nodiscard]] cubey::render::ComputeDispatchGroups dispatch_groups(const Fluid25DConfig& config) {
    return cubey::render::ceil_dispatch_groups(config.grid_width, config.grid_height,
                                               kFluid25DComputeGroupSize);
}

[[nodiscard]] cubey::render::ComputeDispatchGroups quiver_dispatch_groups(const Fluid25DConfig& config) {
    return cubey::render::ceil_dispatch_groups(
        fluid_25d_quiver_count(config.grid_width, config.grid_height), 1U,
        kFluid25DQuiverComputeGroupSize);
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

void record_presentation_cue_reset(VkCommandBuffer command_buffer,
                                   const cubey::vulkan::CommandRecorder& recorder,
                                   Fluid25DGpuResources& resources,
                                   const cubey::render::ComputeDispatchGroups& groups,
                                   const SimulationPushConstants& push_constants) {
    record_dispatch(recorder, resources.presentation_cue_reset_pipeline(),
                    resources.presentation_cue_reset_descriptor_set(), groups, push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
    resources.reset_presentation_cue_parity();
}

void record_presentation_cue_advection(VkCommandBuffer command_buffer,
                                       const cubey::vulkan::CommandRecorder& recorder,
                                       Fluid25DGpuResources& resources,
                                       const cubey::render::ComputeDispatchGroups& groups,
                                       const SimulationPushConstants& push_constants) {
    const bool depth_is_a = resources.current_depth_is_a();
    const bool cue_source_is_a = resources.current_presentation_cue_is_a();
    record_dispatch(
        recorder, resources.presentation_cue_advection_pipeline(),
        resources.presentation_cue_advection_descriptor_set(depth_is_a, cue_source_is_a), groups,
        push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
    resources.advance_presentation_cue_parity();
}

void record_quiver_reset(VkCommandBuffer command_buffer,
                         const cubey::vulkan::CommandRecorder& recorder,
                         Fluid25DGpuResources& resources,
                         const cubey::render::ComputeDispatchGroups& groups,
                         const SimulationPushConstants& push_constants) {
    record_dispatch(recorder, resources.quiver_reset_pipeline(),
                    resources.quiver_reset_descriptor_set(resources.current_depth_is_a()), groups,
                    push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
}

void record_quiver_update(VkCommandBuffer command_buffer,
                          const cubey::vulkan::CommandRecorder& recorder,
                          Fluid25DGpuResources& resources,
                          const cubey::render::ComputeDispatchGroups& groups,
                          const SimulationPushConstants& push_constants) {
    record_dispatch(recorder, resources.quiver_update_pipeline(),
                    resources.quiver_update_descriptor_set(resources.current_depth_is_a()),
                    groups, push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
}

void record_finite_volume_substep(VkCommandBuffer command_buffer,
                                  const cubey::vulkan::CommandRecorder& recorder,
                                  Fluid25DGpuResources& resources,
                                  const cubey::render::ComputeDispatchGroups& groups,
                                  const SimulationPushConstants& push_constants) {
    // Status flags are sticky until explicit solver reset, but each substep
    // has fresh CFL maxima. The previous update has shader-written this buffer;
    // establish compute-to-transfer ordering before clearing only those two
    // scratch words, then make the transfer visible to both compute dispatches.
    cubey::vulkan::record_memory_barrier(
        command_buffer, {
                            .src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                            .src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                        });
    vkCmdFillBuffer(command_buffer, resources.finite_volume_status().handle(),
                    static_cast<VkDeviceSize>(sizeof(std::uint32_t)),
                    static_cast<VkDeviceSize>(sizeof(std::uint32_t) * 2U), 0U);
    cubey::vulkan::record_transfer_write_barrier(
        command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    const bool source_is_a = resources.current_depth_is_a();
    record_dispatch(recorder, resources.finite_volume_cfl_pipeline(),
                    resources.finite_volume_cfl_descriptor_set(source_is_a), groups,
                    push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
    // The reduction-free prepass atomically records separate global x/y wave
    // maxima. A one-invocation finalize dispatch applies the same unsplit
    // dt/dx * (max_x + max_y) test as the CPU oracle before any state write.
    record_dispatch(recorder, resources.finite_volume_cfl_finalize_pipeline(),
                    resources.finite_volume_cfl_finalize_descriptor_set(),
                    {.x = 1U, .y = 1U, .z = 1U}, push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
    // Candidate writes only inactive h/q and isolated velocity/ledger-delta
    // buffers. This barrier makes every candidate flag/output visible before
    // commit makes the all-or-nothing publication decision.
    record_dispatch(recorder, resources.finite_volume_candidate_pipeline(),
                    resources.finite_volume_candidate_descriptor_set(source_is_a), groups,
                    push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
    record_dispatch(recorder, resources.finite_volume_commit_pipeline(),
                    resources.finite_volume_commit_descriptor_set(source_is_a), groups,
                    push_constants);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
    resources.advance_depth_parity();
}

[[nodiscard]] CatchmentPushConstants catchment_push_constants(const Fluid25DConfig& config,
                                                              const Fluid25DGpuResources& resources,
                                                              Fluid25DCatchmentView catchment_view,
                                                              const Fluid25DRenderCamera& camera) {
    return {
        .view_projection = camera.view_projection,
        .grid_cell = {static_cast<float>(config.grid_width), static_cast<float>(config.grid_height),
                      config.cell_size_m, fluid_25d_catchment_height_scale(config.scenario)},
        .camera_wet = {camera.position.x, camera.position.y, camera.position.z,
                       config.minimum_wet_depth_m},
        .presentation = {resources.current_presentation_cue_is_a() ? 1.0F : 0.0F,
                         static_cast<float>(static_cast<std::uint32_t>(catchment_view)), 0.0F,
                         fluid_25d_catchment_terrain_material_cue(config.scenario)},
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

void record_fluid_25d_compute_batch_internal(
    VkCommandBuffer command_buffer, Fluid25DGpuResources& resources, const Fluid25DConfig& config,
    std::span<const float> source_rate_scales, bool paused, bool& reset_requested,
    const Fluid25DComputeRecordingPolicy recording_policy,
    bool include_render_visibility_barrier, cubey::vulkan::GpuTimestampProfiler* profiler,
    std::uint32_t frame_slot_index) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    // Keep every fixed step and its solver substeps inside one aggregate solve
    // span. The direct headless lane selects Disabled below, leaving this as
    // the historical numerical solver span.
    cubey::vulkan::GpuTimestampScope profile_scope(profiler, command_buffer, frame_slot_index,
                                                   "fluid_25d solver");
    const cubey::render::ComputeDispatchGroups groups = dispatch_groups(config);
    const float reset_source_rate_scale =
        source_rate_scales.empty() ? 1.0F : source_rate_scales.front();
    const SimulationPushConstants reset_push_constants =
        simulation_push_constants(config, reset_source_rate_scale);
    const bool record_presentation_cue =
        recording_policy.presentation_cue == Fluid25DPresentationCuePolicy::WindowedPresentation;
    const bool record_quiver = recording_policy.quiver == Fluid25DQuiverPolicy::FlowInspection;
    const SimulationPushConstants cue_push_constants = presentation_cue_push_constants(config);
    const cubey::render::ComputeDispatchGroups quiver_groups = quiver_dispatch_groups(config);
    // Validate every per-step source scale before recording any dispatch. The
    // vector is deliberately small (the windowed catch-up cap), and this
    // preserves fail-fast behavior for callers supplying an invalid schedule.
    for (const float source_rate_scale : source_rate_scales) {
        static_cast<void>(simulation_push_constants(config, source_rate_scale));
    }

    if (reset_requested) {
        record_reset(command_buffer, resources, groups, reset_push_constants);
        reset_requested = false;
    }
    if (record_presentation_cue && recording_policy.presentation_cue_reset_requested != nullptr &&
        *recording_policy.presentation_cue_reset_requested) {
        record_presentation_cue_reset(command_buffer, recorder, resources, groups,
                                      cue_push_constants);
        *recording_policy.presentation_cue_reset_requested = false;
    }
    if (record_quiver && recording_policy.quiver_reset_requested != nullptr &&
        *recording_policy.quiver_reset_requested) {
        record_quiver_reset(command_buffer, recorder, resources, quiver_groups, cue_push_constants);
        *recording_policy.quiver_reset_requested = false;
    }
    if (paused) {
        return;
    }

    for (const float source_rate_scale : source_rate_scales) {
        const SimulationPushConstants push_constants =
            simulation_push_constants(config, source_rate_scale);
        for (std::uint32_t substep = 0; substep < config.simulation_substeps; ++substep) {
            if (config.solver == Fluid25DSolver::FiniteVolume) {
                record_finite_volume_substep(command_buffer, recorder, resources, groups,
                                             push_constants);
                continue;
            }
            const bool source_is_a = resources.current_depth_is_a();
            record_dispatch(recorder, resources.flux_pipeline(),
                            resources.flux_descriptor_set(source_is_a), groups, push_constants);
            // The flux solve writes directed faces and the source/sink/boundary
            // ledger; the next dispatch gathers those faces without scatter writes.
            cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
            record_dispatch(recorder, resources.depth_pipeline(),
                            resources.depth_descriptor_set(source_is_a), groups, push_constants);
            cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
            resources.advance_depth_parity();
        }
        if (record_presentation_cue) {
            record_presentation_cue_advection(command_buffer, recorder, resources, groups,
                                              cue_push_constants);
        }
        if (record_quiver) {
            // This follows the final published velocity for exactly one outer
            // fixed step. The shader itself preserves state if a finite-volume
            // status is rejected/sticky.
            record_quiver_update(command_buffer, recorder, resources, quiver_groups,
                                 cue_push_constants);
        }
    }
    if (include_render_visibility_barrier) {
        cubey::vulkan::record_compute_render_shader_write_barrier(command_buffer);
    }
}

void record_flow_inspection_quiver_internal(
    VkCommandBuffer command_buffer, Fluid25DGpuResources& resources, const Fluid25DConfig& config,
    bool& quiver_reset_requested) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const SimulationPushConstants push_constants = presentation_cue_push_constants(config);
    const cubey::render::ComputeDispatchGroups groups = quiver_dispatch_groups(config);
    if (quiver_reset_requested) {
        record_quiver_reset(command_buffer, recorder, resources, groups, push_constants);
        quiver_reset_requested = false;
    }
    // Headless Flow Inspection reaches this after its deliberately separate
    // solver submission. It remains outside solver profiling and has no
    // numerical state/readback dependency.
    record_quiver_update(command_buffer, recorder, resources, groups, push_constants);
    cubey::vulkan::record_compute_render_shader_write_barrier(command_buffer);
}

void record_flow_inspection_quiver_reset_internal(
    VkCommandBuffer command_buffer, Fluid25DGpuResources& resources, const Fluid25DConfig& config,
    bool& quiver_reset_requested) {
    if (!quiver_reset_requested) {
        return;
    }
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    record_quiver_reset(command_buffer, recorder, resources, quiver_dispatch_groups(config),
                           presentation_cue_push_constants(config));
    quiver_reset_requested = false;
}

} // namespace

void record_fluid_25d_compute_batch(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                                    const Fluid25DConfig& config,
                                    std::span<const float> source_rate_scales, bool paused,
                                    bool& reset_requested, bool include_render_visibility_barrier,
                                    cubey::vulkan::GpuTimestampProfiler* profiler,
                                    std::uint32_t frame_slot_index) {
    record_fluid_25d_compute_batch_internal(
        command_buffer, resources, config, source_rate_scales, paused, reset_requested,
        Fluid25DComputeRecordingPolicy{}, include_render_visibility_barrier, profiler,
        frame_slot_index);
}

void record_fluid_25d_compute(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                              const Fluid25DConfig& config, bool paused, bool& reset_requested,
                              bool include_render_visibility_barrier,
                              cubey::vulkan::GpuTimestampProfiler* profiler,
                              std::uint32_t frame_slot_index, float source_rate_scale) {
    const std::array<float, 1U> source_rate_scales{source_rate_scale};
    record_fluid_25d_compute_batch(
        command_buffer, resources, config, std::span<const float>(source_rate_scales), paused,
        reset_requested, include_render_visibility_barrier, profiler, frame_slot_index);
}

void record_fluid_25d_flow_inspection_quiver_step(
    VkCommandBuffer command_buffer, Fluid25DGpuResources& resources, const Fluid25DConfig& config,
    bool& quiver_reset_requested) {
    record_flow_inspection_quiver_internal(command_buffer, resources, config, quiver_reset_requested);
}

void record_fluid_25d_flow_inspection_quiver_reset(
    VkCommandBuffer command_buffer, Fluid25DGpuResources& resources, const Fluid25DConfig& config,
    bool& quiver_reset_requested) {
    record_flow_inspection_quiver_reset_internal(command_buffer, resources, config,
                                                  quiver_reset_requested);
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
                                     Fluid25DCatchmentView catchment_view,
                                     const Fluid25DRenderCamera& camera,
                                     cubey::render::ColorTargetView color_target,
                                     cubey::render::DepthTargetView depth_target) {
    const std::size_t vertex_count = fluid_25d_mesh_vertex_count(config);
    if (vertex_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("fluid 2.5D product mesh vertex count exceeds Vulkan draw range");
    }
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    const CatchmentPushConstants push_constants =
        catchment_push_constants(config, resources, catchment_view, camera);
    const std::uint32_t quiver_count =
        fluid_25d_quiver_count(config.grid_width, config.grid_height);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target, depth_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.018F, 0.030F, 0.046F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [&resources, catchment_view, push_constants, vertex_count,
         quiver_count](const cubey::vulkan::CommandRecorder& pass_recorder) {
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

            if (catchment_view == Fluid25DCatchmentView::FlowInspection) {
                const cubey::render::GraphicsPipelineResource& quiver = resources.quiver_pipeline();
                pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, quiver.pipeline());
                pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                  quiver.layout(), 0U,
                                                  resources.quiver_render_descriptor_set());
                pass_recorder.push_constants(
                    quiver.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0U, push_constants);
                pass_recorder.draw(kFluid25DQuiverVertexCount, quiver_count);
            }
        });
}

[[nodiscard]] cubey::render::CompiledRenderGraph build_fluid_25d_frame_graph(
    cubey::render::ColorTargetView color_target, Fluid25DGpuResources& resources,
    const Fluid25DConfig& config, Fluid25DPresentationView presentation_view,
    Fluid25DCatchmentView catchment_view, Fluid25DDebugView debug_view,
    const Fluid25DRenderCamera& camera,
    Fluid25DRenderTargetMode target_mode, bool include_simulation, bool paused,
    bool& reset_requested, bool& presentation_cue_reset_requested,
    bool& quiver_reset_requested,
    cubey::vulkan::GpuTimestampProfiler* profiler,
    std::uint32_t frame_slot_index, std::span<const float> source_rate_scales) {
    Fluid25DGpuResources* resource_ptr = &resources;
    const Fluid25DConfig* config_ptr = &config;
    bool* reset_requested_ptr = &reset_requested;
    bool* presentation_cue_reset_requested_ptr = &presentation_cue_reset_requested;
    bool* quiver_reset_requested_ptr = &quiver_reset_requested;
    const bool flow_inspection_active =
        presentation_view == Fluid25DPresentationView::Catchment &&
        catchment_view == Fluid25DCatchmentView::FlowInspection;
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
    const cubey::render::RenderGraphBufferHandle boundary_outflow_mask =
        import("fluid 2.5D boundary outflow mask", resources.boundary_outflow_mask());
    const cubey::render::RenderGraphBufferHandle initial =
        import("fluid 2.5D initial depth", resources.initial_depth());
    const cubey::render::RenderGraphBufferHandle depth_a =
        import("fluid 2.5D depth A", resources.depth_a());
    const cubey::render::RenderGraphBufferHandle depth_b =
        import("fluid 2.5D depth B", resources.depth_b());
    const cubey::render::RenderGraphBufferHandle velocity =
        import("fluid 2.5D velocity", resources.velocity());
    const cubey::render::RenderGraphBufferHandle ledger =
        import("fluid 2.5D ledger", resources.ledger());
    const cubey::render::RenderGraphBufferHandle presentation_cue_a =
        import("fluid 2.5D presentation cue A", resources.presentation_cue_a());
    const cubey::render::RenderGraphBufferHandle presentation_cue_b =
        import("fluid 2.5D presentation cue B", resources.presentation_cue_b());
    const cubey::render::RenderGraphBufferHandle presentation_cue_status =
        import("fluid 2.5D presentation cue status", resources.presentation_cue_status());
    const cubey::render::RenderGraphBufferHandle endpoint_markers =
        import("fluid 2.5D source outlet markers", resources.endpoint_markers());
    const cubey::render::RenderGraphBufferHandle quiver =
        import("fluid 2.5D flow inspection quiver", resources.quiver());
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
        auto simulation =
            graph.add_pass("fluid_25d simulation", cubey::render::RenderGraphQueueDomain::Compute);
        simulation.read_storage_buffer(terrain)
            .read_storage_buffer(source)
            .read_storage_buffer(sink)
            .read_storage_buffer(boundary_outflow_mask)
            .read_storage_buffer(initial)
            .read_write_storage_buffer(depth_a)
            .read_write_storage_buffer(depth_b)
            .read_write_storage_buffer(velocity)
            .read_write_storage_buffer(ledger)
            .read_write_storage_buffer(presentation_cue_a)
            .read_write_storage_buffer(presentation_cue_b);
        if (flow_inspection_active) {
            simulation.read_write_storage_buffer(quiver);
        }
        if (config.solver == Fluid25DSolver::VirtualPipes) {
            const cubey::render::RenderGraphBufferHandle flux =
                import("fluid 2.5D face flux", resources.flux());
            simulation.read_write_storage_buffer(flux).read_storage_buffer(presentation_cue_status);
        } else {
            const cubey::render::RenderGraphBufferHandle momentum_a =
                import("fluid 2.5D finite-volume momentum A", resources.momentum_a());
            const cubey::render::RenderGraphBufferHandle momentum_b =
                import("fluid 2.5D finite-volume momentum B", resources.momentum_b());
            const cubey::render::RenderGraphBufferHandle candidate_velocity =
                import("fluid 2.5D finite-volume candidate velocity",
                       resources.finite_volume_candidate_velocity());
            const cubey::render::RenderGraphBufferHandle candidate_ledger_delta =
                import("fluid 2.5D finite-volume candidate ledger delta",
                       resources.finite_volume_candidate_ledger_delta());
            simulation.read_write_storage_buffer(momentum_a)
                .read_write_storage_buffer(momentum_b)
                .read_write_storage_buffer(candidate_velocity)
                .read_write_storage_buffer(candidate_ledger_delta)
                .read_write_storage_buffer(presentation_cue_status);
        }
        simulation.execute([resource_ptr, config_ptr, source_rate_scales, paused,
                            flow_inspection_active, reset_requested_ptr,
                            presentation_cue_reset_requested_ptr, quiver_reset_requested_ptr,
                            profiler, frame_slot_index](
                               const cubey::render::RenderGraphExecutionContext& context) {
            record_fluid_25d_compute_batch_internal(
                context.recorder().handle(), *resource_ptr, *config_ptr, source_rate_scales, paused,
                *reset_requested_ptr,
                {.presentation_cue = Fluid25DPresentationCuePolicy::WindowedPresentation,
                 .presentation_cue_reset_requested = presentation_cue_reset_requested_ptr,
                 .quiver = flow_inspection_active ? Fluid25DQuiverPolicy::FlowInspection
                                                   : Fluid25DQuiverPolicy::Disabled,
                 .quiver_reset_requested = quiver_reset_requested_ptr},
                false, profiler, frame_slot_index);
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
        auto catchment =
            graph.add_pass("fluid_25d catchment", cubey::render::RenderGraphQueueDomain::Graphics);
        catchment.read_storage_buffer(terrain)
            .read_storage_buffer(depth_a)
            .read_storage_buffer(depth_b)
            .read_storage_buffer(velocity)
            .read_storage_buffer(presentation_cue_a)
            .read_storage_buffer(presentation_cue_b)
            .read_storage_buffer(endpoint_markers);
        if (flow_inspection_active) {
            catchment.read_storage_buffer(quiver);
        }
        catchment.write_color(backbuffer)
            .write_depth(catchment_depth)
            .execute([resource_ptr, config_ptr, catchment_view, camera, backbuffer,
                      catchment_depth](const cubey::render::RenderGraphExecutionContext& context) {
                record_fluid_25d_catchment_draw(
                    context.recorder().handle(), *resource_ptr, *config_ptr, catchment_view,
                    camera,
                    cubey::render::resolved_color_target_view(context, backbuffer),
                    cubey::render::resolved_depth_target_view(context, catchment_depth));
            });
    }
    return graph.compile();
}

} // namespace cubey::projects::fluid::fluid_25d
