#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_presentation.h"
#include "fluid_25d_scenarios.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::uint32_t kFluid25DGpuProfilerPassCapacity = 1U;

// std430-compatible project-local cells. Virtual-pipe flux faces use the
// shared geometric face order left, right, down, up. The third velocity
// component is the wet-state bit (0 or 1).
struct Fluid25DFluxGpu {
    std::array<float, 4> faces_m3_per_s{};
};
// Finite volume keeps h*u and h*v as canonical state. The final pair is
// padding so every element has vec4 std430 alignment.
struct Fluid25DMomentumGpu {
    std::array<float, 4> momentum_xy_reserved{};
};
struct Fluid25DVelocityGpu {
    std::array<float, 4> velocity_wet{};
};
struct Fluid25DLedgerGpu {
    // source, explicit sink, boundary outflow, reserved. Keep this at vec4
    // alignment so the C++ layout remains the shader's std430 layout.
    std::array<float, 4> source_sink_boundary_reserved_m3{};
};
// Conservative tracer accounting is intentionally separate from water
// accounting. Its components have the same physical-volume units, but track
// q=h*c rather than water depth.
struct Fluid25DTracerLedgerGpu {
    std::array<float, 4> source_sink_boundary_reserved_m3{};
};
// One tiny selected-solver status buffer retains error flags until explicit
// solver reset. Its two CFL-max scratch words are cleared before each
// finite-volume substep, letting the prepass reject an update without a host
// round-trip. Any nonzero flag is an invalid comparison result.
struct Fluid25DFiniteVolumeStatusGpu {
    std::array<std::uint32_t, 4> flags_reserved{};
};
// Flow Inspection-only persistent render state. Anchor coordinates are set
// from one deterministic regular lattice on reset and never advect, respawn,
// or retire. The second vec4 is a smoothed local reading, not solver state.
struct Fluid25DQuiverGpu {
    std::array<float, 4> anchor_xy_reserved{};
    std::array<float, 4> direction_xy_strength_opacity{};
};
// Catchment-only explanatory markers in cell coordinates. The first vec4 is
// the original source.xy/outlet.xy layout; the second adds an optional second
// source without changing existing scene marker placement. Negative pairs
// disable the corresponding terrain/water annulus.
struct Fluid25DEndpointMarkersGpu {
    std::array<float, 4> source_xy_outlet_xy{};
    std::array<float, 4> secondary_source_xy_reserved{};
};
static_assert(offsetof(Fluid25DEndpointMarkersGpu, secondary_source_xy_reserved) ==
              sizeof(float) * 4U);

[[nodiscard]] inline Fluid25DEndpointMarkersGpu
fluid_25d_endpoint_markers(const Fluid25DConfig& config, const Fluid25DScenarioData& scenario) {
    Fluid25DEndpointMarkersGpu markers{};
    markers.source_xy_outlet_xy.fill(-1.0F);
    markers.secondary_source_xy_reserved.fill(-1.0F);
    if (config.grid_width == 0U) {
        return markers;
    }

    const auto cell_xy = [width = config.grid_width](std::size_t index) {
        return std::array<float, 2>{static_cast<float>(index % width),
                                    static_cast<float>(index / width)};
    };
    if (fluid_25d_is_source_outlet_demo(config.scenario) &&
        scenario.source_cell != kFluid25DNoCell && scenario.sink_cell != kFluid25DNoCell) {
        const std::array<float, 2> source_xy = cell_xy(scenario.source_cell);
        const std::array<float, 2> outlet_xy = cell_xy(scenario.sink_cell);
        markers.source_xy_outlet_xy = {source_xy[0], source_xy[1], outlet_xy[0], outlet_xy[1]};
    } else if (config.scenario == Fluid25DScenario::SustainedHeadwatersDemo &&
               scenario.source_cell != kFluid25DNoCell &&
               scenario.secondary_source_cell != kFluid25DNoCell &&
               scenario.outlet_cell != kFluid25DNoCell) {
        const std::array<float, 2> source_xy = cell_xy(scenario.source_cell);
        const std::array<float, 2> secondary_source_xy = cell_xy(scenario.secondary_source_cell);
        const std::array<float, 2> outlet_xy = cell_xy(scenario.outlet_cell);
        markers.source_xy_outlet_xy = {source_xy[0], source_xy[1], outlet_xy[0], outlet_xy[1]};
        markers.secondary_source_xy_reserved = {secondary_source_xy[0], secondary_source_xy[1],
                                                -1.0F, -1.0F};
    } else if (config.scenario == Fluid25DScenario::HillsideFlowStudy &&
               scenario.source_cell != kFluid25DNoCell && scenario.outlet_cell == kFluid25DNoCell &&
               scenario.sink_cell == kFluid25DNoCell) {
        const std::array<float, 2> source_xy = cell_xy(scenario.source_cell);
        markers.source_xy_outlet_xy = {source_xy[0], source_xy[1], -1.0F, -1.0F};
    } else if (config.scenario == Fluid25DScenario::NaturalFlowStudy &&
               scenario.source_cell != kFluid25DNoCell && scenario.outlet_cell != kFluid25DNoCell &&
               scenario.sink_cell == kFluid25DNoCell) {
        const std::array<float, 2> source_xy = cell_xy(scenario.source_cell);
        const std::array<float, 2> expected_exit_xy = cell_xy(scenario.outlet_cell);
        markers.source_xy_outlet_xy = {source_xy[0], source_xy[1], expected_exit_xy[0],
                                       expected_exit_xy[1]};
    }
    return markers;
}

inline constexpr std::uint32_t kFluid25DFiniteVolumeStatusCflRejected = 1U << 0U;
inline constexpr std::uint32_t kFluid25DFiniteVolumeStatusInvalidState = 1U << 1U;
inline constexpr std::uint32_t kFluid25DFiniteVolumeStatusClosedBoundaryMass = 1U << 2U;
inline constexpr std::uint32_t kFluid25DFiniteVolumeStatusOpenBoundaryInflow = 1U << 3U;

static_assert(sizeof(Fluid25DFluxGpu) == sizeof(float) * 4U);
static_assert(sizeof(Fluid25DMomentumGpu) == sizeof(float) * 4U);
static_assert(sizeof(Fluid25DVelocityGpu) == sizeof(float) * 4U);
static_assert(sizeof(Fluid25DLedgerGpu) == sizeof(float) * 4U);
static_assert(sizeof(Fluid25DTracerLedgerGpu) == sizeof(float) * 4U);
static_assert(sizeof(Fluid25DFiniteVolumeStatusGpu) == sizeof(std::uint32_t) * 4U);
static_assert(sizeof(Fluid25DQuiverGpu) == sizeof(float) * 8U);
static_assert(sizeof(Fluid25DEndpointMarkersGpu) == sizeof(float) * 8U);

class Fluid25DGpuResources {
  public:
    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::ProjectGpuServices& gpu,
                                           const Fluid25DConfig& config,
                                           const Fluid25DScenarioData& scenario,
                                           std::uint32_t frame_slot_count);
    void create_render_pipelines(cubey::vulkan::Device& device, VkFormat color_format,
                                 VkFormat depth_format, VkExtent2D extent);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] const cubey::vulkan::Buffer& terrain() const;
    [[nodiscard]] const cubey::vulkan::Buffer& source_rate() const;
    [[nodiscard]] const cubey::vulkan::Buffer& sink_rate() const;
    [[nodiscard]] const cubey::vulkan::Buffer& boundary_outflow_mask() const;
    [[nodiscard]] const cubey::vulkan::Buffer& initial_depth() const;
    [[nodiscard]] const cubey::vulkan::Buffer& depth_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& depth_b() const;
    // q=h*c tracer state always follows the depth A/B parity. Virtual-pipes
    // owns inert zero buffers so later shared render bindings remain valid.
    [[nodiscard]] const cubey::vulkan::Buffer& tracer_q_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& tracer_q_b() const;
    // Virtual-pipes-only canonical state.
    [[nodiscard]] const cubey::vulkan::Buffer& flux() const;
    // Finite-volume-only canonical state/status.
    [[nodiscard]] const cubey::vulkan::Buffer& momentum_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& momentum_b() const;
    // Finite-volume candidate-only results are isolated until the matching
    // commit dispatch accepts the whole substep.
    [[nodiscard]] const cubey::vulkan::Buffer& finite_volume_candidate_velocity() const;
    [[nodiscard]] const cubey::vulkan::Buffer& finite_volume_candidate_ledger_delta() const;
    [[nodiscard]] const cubey::vulkan::Buffer&
    finite_volume_candidate_tracer_ledger_delta() const;
    [[nodiscard]] const cubey::vulkan::Buffer& finite_volume_status() const;
    [[nodiscard]] const cubey::vulkan::Buffer& velocity() const;
    [[nodiscard]] const cubey::vulkan::Buffer& ledger() const;
    [[nodiscard]] const cubey::vulkan::Buffer& tracer_ledger() const;
    // Render-only scalar cue state. It deliberately never enters solver
    // descriptors, diagnostics, readback, or CPU oracle comparisons.
    [[nodiscard]] const cubey::vulkan::Buffer& presentation_cue_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& presentation_cue_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& presentation_cue_status() const;
    [[nodiscard]] const cubey::vulkan::Buffer& endpoint_markers() const;
    [[nodiscard]] const cubey::vulkan::Buffer& quiver() const;
    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* profiler() noexcept {
        return profiler_.has_value() ? &profiler_.value() : nullptr;
    }
    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_timings() const;

    [[nodiscard]] const cubey::render::ComputePipelineResource& reset_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& flux_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& depth_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& finite_volume_cfl_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    finite_volume_cfl_finalize_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    finite_volume_candidate_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    finite_volume_commit_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    presentation_cue_reset_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource&
    presentation_cue_advection_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& quiver_reset_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& quiver_update_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& diagnostic_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& terrain_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& water_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& quiver_pipeline() const;

    [[nodiscard]] VkDescriptorSet reset_descriptor_set() const noexcept {
        if (solver_ == Fluid25DSolver::FiniteVolume) {
            return finite_volume_reset_descriptors_.has_value()
                       ? finite_volume_reset_descriptors_->set()
                       : VK_NULL_HANDLE;
        }
        return reset_descriptors_.has_value() ? reset_descriptors_->set() : VK_NULL_HANDLE;
    }
    [[nodiscard]] VkDescriptorSet flux_descriptor_set(bool source_is_a) const noexcept {
        return source_is_a && flux_a_descriptors_.has_value()
                   ? flux_a_descriptors_.value().set()
                   : (!source_is_a && flux_b_descriptors_.has_value()
                          ? flux_b_descriptors_.value().set()
                          : VK_NULL_HANDLE);
    }
    [[nodiscard]] VkDescriptorSet depth_descriptor_set(bool source_is_a) const noexcept {
        return source_is_a && depth_a_to_b_descriptors_.has_value()
                   ? depth_a_to_b_descriptors_.value().set()
                   : (!source_is_a && depth_b_to_a_descriptors_.has_value()
                          ? depth_b_to_a_descriptors_.value().set()
                          : VK_NULL_HANDLE);
    }
    [[nodiscard]] VkDescriptorSet
    finite_volume_cfl_descriptor_set(bool source_is_a) const noexcept {
        return source_is_a && finite_volume_cfl_a_descriptors_.has_value()
                   ? finite_volume_cfl_a_descriptors_.value().set()
                   : (!source_is_a && finite_volume_cfl_b_descriptors_.has_value()
                          ? finite_volume_cfl_b_descriptors_.value().set()
                          : VK_NULL_HANDLE);
    }
    [[nodiscard]] VkDescriptorSet
    finite_volume_candidate_descriptor_set(bool source_is_a) const noexcept {
        return source_is_a && finite_volume_candidate_a_to_b_descriptors_.has_value()
                   ? finite_volume_candidate_a_to_b_descriptors_.value().set()
                   : (!source_is_a && finite_volume_candidate_b_to_a_descriptors_.has_value()
                          ? finite_volume_candidate_b_to_a_descriptors_.value().set()
                          : VK_NULL_HANDLE);
    }
    [[nodiscard]] VkDescriptorSet
    finite_volume_commit_descriptor_set(bool source_is_a) const noexcept {
        return source_is_a && finite_volume_commit_a_to_b_descriptors_.has_value()
                   ? finite_volume_commit_a_to_b_descriptors_.value().set()
                   : (!source_is_a && finite_volume_commit_b_to_a_descriptors_.has_value()
                          ? finite_volume_commit_b_to_a_descriptors_.value().set()
                          : VK_NULL_HANDLE);
    }
    [[nodiscard]] VkDescriptorSet finite_volume_cfl_finalize_descriptor_set() const noexcept {
        return finite_volume_cfl_finalize_descriptors_.has_value()
                   ? finite_volume_cfl_finalize_descriptors_->set()
                   : VK_NULL_HANDLE;
    }
    [[nodiscard]] VkDescriptorSet presentation_cue_reset_descriptor_set() const noexcept {
        return presentation_cue_reset_descriptors_.has_value()
                   ? presentation_cue_reset_descriptors_->set()
                   : VK_NULL_HANDLE;
    }
    [[nodiscard]] VkDescriptorSet
    presentation_cue_advection_descriptor_set(bool depth_is_a,
                                              bool cue_source_is_a) const noexcept {
        if (depth_is_a && cue_source_is_a) {
            return presentation_cue_depth_a_a_to_b_descriptors_.has_value()
                       ? presentation_cue_depth_a_a_to_b_descriptors_->set()
                       : VK_NULL_HANDLE;
        }
        if (depth_is_a) {
            return presentation_cue_depth_a_b_to_a_descriptors_.has_value()
                       ? presentation_cue_depth_a_b_to_a_descriptors_->set()
                       : VK_NULL_HANDLE;
        }
        if (cue_source_is_a) {
            return presentation_cue_depth_b_a_to_b_descriptors_.has_value()
                       ? presentation_cue_depth_b_a_to_b_descriptors_->set()
                       : VK_NULL_HANDLE;
        }
        return presentation_cue_depth_b_b_to_a_descriptors_.has_value()
                   ? presentation_cue_depth_b_b_to_a_descriptors_->set()
                   : VK_NULL_HANDLE;
    }
    [[nodiscard]] VkDescriptorSet quiver_reset_descriptor_set(bool depth_is_a) const noexcept {
        return depth_is_a && quiver_reset_a_descriptors_.has_value()
                   ? quiver_reset_a_descriptors_->set()
                   : (!depth_is_a && quiver_reset_b_descriptors_.has_value()
                          ? quiver_reset_b_descriptors_->set()
                          : VK_NULL_HANDLE);
    }
    [[nodiscard]] VkDescriptorSet
    quiver_update_descriptor_set(bool depth_is_a) const noexcept {
        return depth_is_a && quiver_update_a_descriptors_.has_value()
                   ? quiver_update_a_descriptors_->set()
                   : (!depth_is_a && quiver_update_b_descriptors_.has_value()
                          ? quiver_update_b_descriptors_->set()
                          : VK_NULL_HANDLE);
    }
    [[nodiscard]] VkDescriptorSet quiver_render_descriptor_set() const noexcept {
        return current_depth_is_a_ ? quiver_render_a_descriptors_.value().set()
                                   : quiver_render_b_descriptors_.value().set();
    }
    [[nodiscard]] VkDescriptorSet render_descriptor_set() const noexcept {
        return current_depth_is_a_ ? render_a_descriptors_.value().set()
                                   : render_b_descriptors_.value().set();
    }

    [[nodiscard]] bool current_depth_is_a() const noexcept {
        return current_depth_is_a_;
    }
    void reset_depth_parity() noexcept {
        current_depth_is_a_ = true;
    }
    void advance_depth_parity() noexcept {
        current_depth_is_a_ = !current_depth_is_a_;
    }
    [[nodiscard]] bool current_presentation_cue_is_a() const noexcept {
        return presentation_cue_parity_.source_is_a();
    }
    void reset_presentation_cue_parity() noexcept {
        presentation_cue_parity_.reset();
    }
    void advance_presentation_cue_parity() noexcept {
        presentation_cue_parity_.advance();
    }
    [[nodiscard]] Fluid25DSolver solver() const noexcept {
        return solver_;
    }

  private:
    void create_buffers(cubey::ProjectGpuServices& gpu, const Fluid25DConfig& config,
                        const Fluid25DScenarioData& scenario);
    void create_descriptors(cubey::vulkan::Device& device);
    void create_compute_pipelines(cubey::vulkan::Device& device);

    std::optional<cubey::vulkan::Buffer> terrain_;
    std::optional<cubey::vulkan::Buffer> source_rate_;
    std::optional<cubey::vulkan::Buffer> sink_rate_;
    std::optional<cubey::vulkan::Buffer> boundary_outflow_mask_;
    std::optional<cubey::vulkan::Buffer> initial_depth_;
    std::optional<cubey::vulkan::Buffer> depth_a_;
    std::optional<cubey::vulkan::Buffer> depth_b_;
    std::optional<cubey::vulkan::Buffer> tracer_q_a_;
    std::optional<cubey::vulkan::Buffer> tracer_q_b_;
    std::optional<cubey::vulkan::Buffer> flux_;
    std::optional<cubey::vulkan::Buffer> momentum_a_;
    std::optional<cubey::vulkan::Buffer> momentum_b_;
    std::optional<cubey::vulkan::Buffer> finite_volume_candidate_velocity_;
    std::optional<cubey::vulkan::Buffer> finite_volume_candidate_ledger_delta_;
    std::optional<cubey::vulkan::Buffer> finite_volume_candidate_tracer_ledger_delta_;
    std::optional<cubey::vulkan::Buffer> finite_volume_status_;
    std::optional<cubey::vulkan::Buffer> velocity_;
    std::optional<cubey::vulkan::Buffer> ledger_;
    std::optional<cubey::vulkan::Buffer> tracer_ledger_;
    std::optional<cubey::vulkan::Buffer> presentation_cue_a_;
    std::optional<cubey::vulkan::Buffer> presentation_cue_b_;
    std::optional<cubey::vulkan::Buffer> presentation_cue_virtual_status_;
    std::optional<cubey::vulkan::Buffer> endpoint_markers_;
    std::optional<cubey::vulkan::Buffer> quiver_;
    std::optional<cubey::vulkan::GpuTimestampProfiler> profiler_;

    std::optional<cubey::vulkan::DescriptorSetBundle> reset_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> flux_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> flux_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> depth_a_to_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> depth_b_to_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_reset_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_cfl_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_cfl_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_cfl_finalize_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_candidate_a_to_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_candidate_b_to_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_commit_a_to_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> finite_volume_commit_b_to_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> presentation_cue_reset_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> presentation_cue_depth_a_a_to_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> presentation_cue_depth_a_b_to_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> presentation_cue_depth_b_a_to_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> presentation_cue_depth_b_b_to_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> quiver_reset_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> quiver_reset_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> quiver_update_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> quiver_update_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> quiver_render_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> quiver_render_b_descriptors_;

    std::optional<cubey::render::ComputePipelineResource> reset_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> flux_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> depth_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> finite_volume_reset_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> finite_volume_cfl_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> finite_volume_cfl_finalize_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> finite_volume_candidate_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> finite_volume_commit_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> presentation_cue_reset_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> presentation_cue_advection_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> quiver_reset_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> quiver_update_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> diagnostic_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> terrain_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> water_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> quiver_pipeline_;
    bool current_depth_is_a_ = true;
    Fluid25DPresentationCueParity presentation_cue_parity_;
    Fluid25DSolver solver_ = Fluid25DSolver::VirtualPipes;
};

} // namespace cubey::projects::fluid::fluid_25d
