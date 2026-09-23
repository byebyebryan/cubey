#include "fluid_25d_app.h"

#include "fluid_25d_commands.h"
#include "fluid_25d_diagnostics.h"
#include "fluid_25d_finite_volume_oracle.h"
#include "fluid_25d_gpu_resources.h"
#include "fluid_25d_oracle.h"
#include "fluid_25d_scenarios.h"
#include "fluid_25d_ui.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/immediate_commands.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

inline constexpr float kDepthToleranceM = 0.0005F;
inline constexpr float kFluxToleranceM3PerS = 0.002F;
inline constexpr float kMomentumToleranceM2PerS = 0.002F;
inline constexpr float kVelocityToleranceMPerS = 0.002F;
inline constexpr float kTracerQToleranceM = 0.0005F;
inline constexpr float kTracerConcentrationTolerance = 0.01F;
inline constexpr double kMinimumLedgerToleranceM3 = 0.003;
inline constexpr double kRelativeLedgerTolerance = 3.0e-4;
inline constexpr float kCatchmentCameraBaseYaw = -0.52F;
inline constexpr float kCatchmentCameraBasePitch = -0.92F;
// The terrain-and-water presentation spans kilometres in X/Z while deliberately
// compressing elevation. A conventional 0.1 m near plane leaves too little
// forward-Z precision to distinguish a valid shallow sheet from its bed at the
// far side of the catchment. This stays safely inside the closest permitted
// orbit (30% of the horizontal extent) while scaling with every terrain crop.
inline constexpr float kCatchmentCameraNearExtentFraction = 0.10F;
inline constexpr float kCatchmentCameraMinimumNearPlaneM = 8.0F;

[[nodiscard]] std::uint32_t headless_frame_count(const host::CommonRunConfig& config) {
    return config.frames == 0U ? 120U : config.frames;
}

[[nodiscard]] FrameTiming fixed_headless_timing(const Fluid25DConfig& config,
                                                std::uint64_t frame_index) {
    if (frame_index == 0U) {
        throw std::runtime_error("fluid 2.5D fixed headless frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

template <typename Value>
[[nodiscard]] std::vector<Value> readback_values(cubey::ProjectGpuServices& gpu,
                                                 const cubey::vulkan::Buffer& buffer,
                                                 std::size_t value_count, const char* label) {
    const VkDeviceSize expected_size = static_cast<VkDeviceSize>(value_count * sizeof(Value));
    if (buffer.size() != expected_size) {
        throw std::runtime_error(std::string("fluid 2.5D ") + label +
                                 " buffer has an unexpected byte size");
    }
    const std::vector<std::uint8_t> bytes =
        gpu.readback_buffer(buffer.handle(), buffer.size(), label);
    if (bytes.size() != static_cast<std::size_t>(expected_size)) {
        throw std::runtime_error(std::string("fluid 2.5D ") + label +
                                 " readback has an unexpected byte size");
    }
    std::vector<Value> values(value_count);
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

[[nodiscard]] bool finite(float value) {
    return std::isfinite(value);
}

[[nodiscard]] double cumulative_ledger_tolerance_m3(double expected_volume_m3) {
    if (!std::isfinite(expected_volume_m3)) {
        throw std::runtime_error("fluid 2.5D expected cumulative ledger is nonfinite");
    }
    // GPU distributed per-cell cumulative ledger is float, while CPU oracle
    // accumulation is double. Scale the ledger tolerance at 0.03%; state
    // tolerances remain unchanged, and zero expected boundary flow stays strict.
    return std::max(kMinimumLedgerToleranceM3,
                    std::abs(expected_volume_m3) * kRelativeLedgerTolerance);
}

[[nodiscard]] Fluid25DScenarioData make_startup_scenario(Fluid25DProjectConfig& config) {
    validate_fluid_25d_project_config(config);
    const bool terrain_case = config.simulation.scenario == Fluid25DScenario::TerrainCase;
    const bool mountain_source_outlet =
        config.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo;
    if (!terrain_case && !mountain_source_outlet) {
        return make_fluid_25d_scenario(config.simulation.scenario, config.simulation.grid_width,
                                       config.simulation.grid_height,
                                       config.simulation.cell_size_m);
    }

    cubey::asset::TerrainRasterHeightSource source(config.terrain.heightfield_path.value());
    const float source_spacing_m = source.sample_spacing_m();
    resolve_fluid_25d_terrain_cell_size(config, source_spacing_m);

    Fluid25DScenarioData scenario =
        terrain_case ? make_fluid_25d_terrain_scenario(config.simulation, source,
                                                       config.terrain.crop_x.value_or(0U),
                                                       config.terrain.crop_z.value_or(0U))
                     : make_fluid_25d_mountain_source_outlet_scenario(config.simulation, source);
    if (!scenario.terrain_provenance.has_value()) {
        throw std::runtime_error("fluid 2.5D terrain-backed scenario did not produce provenance");
    }
    std::printf("fluid_25d: terrain-backed identity=%s source=%s manifest=%s\n",
                scenario.terrain_provenance->identity.c_str(),
                scenario.terrain_provenance->source_id.c_str(),
                scenario.terrain_provenance->manifest_path.string().c_str());
    return scenario;
}

class Fluid25DApp {
  public:
    explicit Fluid25DApp(Fluid25DProjectConfig config)
        : config_(std::move(config)),
          windowed_pacing_(config_.simulation.fixed_delta_seconds,
                           config_.presentation_time_scale),
          scenario_(make_startup_scenario(config_)),
          presentation_view_(fluid_25d_presentation_view_from_name(config_.view)),
          catchment_view_(fluid_25d_catchment_view_from_name(config_.catchment_view)),
          debug_view_(fluid_25d_debug_view_from_name(config_.debug_view)) {
        initial_water_volume_m3_ =
            fluid_25d_water_volume_m3(config_.simulation, scenario_.initial_water_depth_m);
        if (config_.simulation.scenario == Fluid25DScenario::TerrainCase) {
            std::printf(
                "fluid_25d: terrain-water protocol=%s rainfall_m_per_s=%.9f "
                "sheet_depth_m=%.6f\n",
                fluid_25d_terrain_water_protocol_name(config_.simulation.terrain_water_protocol),
                config_.simulation.rainfall_depth_rate_m_per_s,
                config_.simulation.sheet_initial_depth_m);
        }
        configure_catchment_camera();
        if (config_.gpu_oracle_validation) {
            if (config_.simulation.solver == Fluid25DSolver::FiniteVolume) {
                finite_volume_oracle_.emplace(config_.simulation, scenario_);
            } else {
                oracle_.emplace(config_.simulation, scenario_);
            }
        }
    }

    Fluid25DApp(const Fluid25DApp&) = delete;
    Fluid25DApp& operator=(const Fluid25DApp&) = delete;

    int run() {
        return config_.common.headless ? run_headless() : run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            resources_.create_render_pipelines(context.device(), context.swapchain().format(),
                                               VK_FORMAT_D32_SFLOAT, context.swapchain().extent());
            graph_executor_.clear();
            graph_executor_.resize(context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            graph_executor_.clear();
            resources_.destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            const bool was_flow_inspection_active = flow_inspection_active();
            const auto input = context.filtered_input();
            orbit_controller_.update_pointer_input(input, timing.delta_seconds);
            if (input.key_pressed(cubey::input::Key::Space)) {
                paused_ = !paused_;
            }
            if (input.key_pressed(cubey::input::Key::R)) {
                reset_requested_ = true;
                presentation_cue_reset_requested_ = true;
                quiver_reset_requested_ = true;
                windowed_pacing_.reset();
            }
            if (input.key_pressed(cubey::input::Key::D)) {
                debug_view_ = static_cast<Fluid25DDebugView>(
                    (static_cast<std::uint32_t>(debug_view_) + 1U) % 6U);
            }
            if (input.key_pressed(cubey::input::Key::A)) {
                presentation_view_ = presentation_view_ == Fluid25DPresentationView::Diagnostics
                                         ? Fluid25DPresentationView::Catchment
                                         : Fluid25DPresentationView::Diagnostics;
            }
            if (!was_flow_inspection_active && flow_inspection_active()) {
                quiver_reset_requested_ = true;
            }
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) { draw_ui(); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context, frame);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            graph_executor_.clear();
            resources_.destroy_all_resources();
            runtime_.detach_gpu_if_attached();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_.common,
                .app_name = "fluid_25d",
                .ready_status = "rendering River V0 terrain-water catchment",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    void draw_ui() {
        const bool was_flow_inspection_active = flow_inspection_active();
        draw_fluid_25d_ui({
            .title = "Fluid 2.5D",
            .scenario = config_.simulation.scenario,
            .transport_inspection_available =
                fluid_25d_transport_inspection_available(config_.simulation),
            .presentation_view = presentation_view_,
            .catchment_view = catchment_view_,
            .debug_view = debug_view_,
            .presentation_time_scale = config_.presentation_time_scale,
            .windowed_pacing = windowed_pacing_,
            .paused = paused_,
            .reset_requested = reset_requested_,
            .presentation_cue_reset_requested = presentation_cue_reset_requested_,
            .quiver_reset_requested = quiver_reset_requested_,
        });
        if (!was_flow_inspection_active && flow_inspection_active()) {
            // Flow Inspection starts from a deterministic render-only field;
            // changing the reading mode never changes solver state.
            quiver_reset_requested_ = true;
        }
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        runtime_.attach_gpu_if_needed(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), config_.simulation,
                                                     scenario_, frame_slot_count);
    }

    void record_windowed_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& render_frame) {
        const ProjectFrame project_frame = runtime_.frame_for_timing(render_frame.timing);
        cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
        if (profiler != nullptr) {
            profiler->collect(render_frame.frame_slot.index);
            record_gpu_timings(
                context.profile_recorder(),
                collected_profile_frame_index(project_frame, render_frame.frame_slot),
                resources_.latest_timings());
        }
        if (reset_requested_) {
            source_schedule_.reset();
            dye_source_schedule_.reset();
            windowed_pacing_.reset();
        }
        const Fluid25DWindowedPacingFrame pacing =
            windowed_pacing_.advance(render_frame.timing.delta_seconds, paused_);
        const std::uint32_t fixed_step_count = pacing.fixed_step_count;
        Fluid25DSourceRateSchedule next_source_schedule = source_schedule_;
        Fluid25DDyeSourceSchedule next_dye_source_schedule = dye_source_schedule_;
        std::vector<Fluid25DStepForcing> forcings;
        forcings.reserve(fixed_step_count);
        for (std::uint32_t step = 0U; step < fixed_step_count; ++step) {
            forcings.push_back({
                .source_rate_scale = next_source_schedule.source_rate_scale(config_.simulation),
                .dye_source_concentration =
                    next_dye_source_schedule.source_concentration(config_.simulation),
            });
            next_source_schedule.advance_fixed_step();
            next_dye_source_schedule.advance_fixed_step();
        }
        // The windowed graph records every pending fixed step and then draws
        // from the resulting state in one command buffer. The cue update is
        // recorded after each outer step, never once per presented frame.
        const cubey::render::CompiledRenderGraph graph = build_fluid_25d_frame_graph(
            render_frame.color_target, resources_, config_.simulation, presentation_view_,
            catchment_view_, debug_view_, render_camera(render_frame.color_target.extent),
            Fluid25DRenderTargetMode::Present, true, paused_, reset_requested_,
            presentation_cue_reset_requested_, quiver_reset_requested_, profiler,
            render_frame.frame_slot.index,
            forcings);
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (profiler != nullptr) {
            profiler->begin_frame(render_frame.command_buffer, render_frame.frame_slot.index);
        }
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &context.device(),
                .command_buffer = render_frame.command_buffer,
                .frame_slot = render_frame.frame_slot,
                .label = "vkEndCommandBuffer fluid_25d",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            graph);
        recorder.end("vkEndCommandBuffer fluid_25d");
        source_schedule_ = std::move(next_source_schedule);
        dye_source_schedule_ = std::move(next_dye_source_schedule);
    }

    void configure_catchment_camera() {
        const auto [terrain_minimum, terrain_maximum] = std::minmax_element(
            scenario_.terrain_height_m.begin(), scenario_.terrain_height_m.end());
        const float world_width =
            static_cast<float>(config_.simulation.grid_width - 1U) * config_.simulation.cell_size_m;
        const float world_height = static_cast<float>(config_.simulation.grid_height - 1U) *
                                   config_.simulation.cell_size_m;
        const float horizontal_extent = std::max(world_width, world_height);
        const float render_height_scale =
            fluid_25d_catchment_height_scale(config_.simulation.scenario);
        const float scaled_terrain_span =
            (*terrain_maximum - *terrain_minimum) * render_height_scale;
        float framing_horizontal_extent = horizontal_extent;
        catchment_target_ = {
            0.0F,
            (*terrain_minimum + *terrain_maximum) * 0.5F * render_height_scale,
            0.0F,
        };
        if (fluid_25d_is_source_outlet_demo(config_.simulation.scenario)) {
            // Each explanation route gets a one-time endpoint framing with
            // modest terrain context. This changes neither later orbit/zoom
            // input nor any non-demonstration scenario.
            const auto world_x = [this](std::size_t index) {
                const std::uint32_t x = static_cast<std::uint32_t>(index % scenario_.width);
                return (static_cast<float>(x) -
                        (0.5F * static_cast<float>(scenario_.width - 1U))) *
                       config_.simulation.cell_size_m;
            };
            const auto world_z = [this](std::size_t index) {
                const std::uint32_t y =
                    static_cast<std::uint32_t>(index / static_cast<std::size_t>(scenario_.width));
                return (static_cast<float>(y) -
                        (0.5F * static_cast<float>(scenario_.height - 1U))) *
                       config_.simulation.cell_size_m;
            };
            const float route_span =
                std::max(std::abs(world_x(scenario_.sink_cell) - world_x(scenario_.source_cell)),
                         std::abs(world_z(scenario_.sink_cell) - world_z(scenario_.source_cell)));
            framing_horizontal_extent =
                std::max(32.0F, fluid_25d_catchment_home_horizontal_extent(
                                    config_.simulation.scenario, horizontal_extent, route_span));
            catchment_target_.x =
                0.5F * (world_x(scenario_.source_cell) + world_x(scenario_.sink_cell));
            catchment_target_.z =
                0.5F * (world_z(scenario_.source_cell) + world_z(scenario_.sink_cell));
            if (config_.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo) {
                // The crop's high ridge is useful context but should not pull
                // the long low-elevation source-to-outlet route off screen.
                // This is a camera target only; imported elevations stay
                // untouched in every numerical buffer.
                catchment_target_.y = 0.5F *
                                      (scenario_.terrain_height_m[scenario_.source_cell] +
                                       scenario_.terrain_height_m[scenario_.sink_cell]) *
                                      render_height_scale;
            }
        }
        const float camera_distance =
            std::max(framing_horizontal_extent *
                         fluid_25d_catchment_home_distance_scale(config_.simulation.scenario),
                     scaled_terrain_span * 6.0F + 16.0F);
        orbit_controller_.set_distance_limits(std::max(8.0F, framing_horizontal_extent * 0.30F),
                                              std::max(48.0F, framing_horizontal_extent * 4.0F));
        orbit_controller_.set_pitch_limits(-0.38F, 0.38F);
        orbit_controller_.set_home_distance(camera_distance);
        const float near_plane = std::max(kCatchmentCameraMinimumNearPlaneM,
                                          framing_horizontal_extent *
                                              kCatchmentCameraNearExtentFraction);
        camera_.set_projection(fluid_25d_catchment_home_fovy_radians(
                                   std::numbers::pi_v<float> / 3.0F, config_.simulation.scenario),
                               near_plane, camera_distance * 5.0F + scaled_terrain_span + 64.0F);
    }

    [[nodiscard]] cubey::Transform3D render_camera_transform() const {
        return cubey::orbit_camera_transform({
            .target = catchment_target_,
            .distance = orbit_controller_.distance(),
            .yaw = kCatchmentCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = fluid_25d_catchment_home_pitch(kCatchmentCameraBasePitch,
                                                    config_.simulation.scenario) +
                     orbit_controller_.pitch(),
        });
    }

    [[nodiscard]] Fluid25DRenderCamera render_camera(VkExtent2D extent) const {
        const cubey::Transform3D transform = render_camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .position = transform.translation,
        };
    }

    [[nodiscard]] bool flow_inspection_active() const noexcept {
        return presentation_view_ == Fluid25DPresentationView::Catchment &&
               catchment_view_ == Fluid25DCatchmentView::FlowInspection;
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const cubey::host::HeadlessCaptureFrame& frame,
                                          cubey::profiling::ProfileRecorder* profile_recorder) {
        const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
        const std::uint64_t frame_index = profile_frame_index(project_frame);
        if (reset_requested_) {
            source_schedule_.reset();
            dye_source_schedule_.reset();
            expected_source_volume_m3_ = 0.0;
            expected_sink_volume_m3_ = 0.0;
            expected_boundary_outflow_volume_m3_ = 0.0;
            expected_tracer_source_amount_m3_ = 0.0;
            expected_tracer_sink_amount_m3_ = 0.0;
            expected_tracer_boundary_outflow_amount_m3_ = 0.0;
            if (oracle_.has_value()) {
                oracle_->reset();
            }
            if (finite_volume_oracle_.has_value()) {
                finite_volume_oracle_->reset();
            }
        }
        const float source_rate_scale = source_schedule_.source_rate_scale(config_.simulation);
        const float dye_source_concentration =
            dye_source_schedule_.source_concentration(config_.simulation);
        const Fluid25DStepForcing forcing{
            .source_rate_scale = source_rate_scale,
            .dye_source_concentration = dye_source_concentration,
        };
        const bool record_flow_inspection_quiver = flow_inspection_active();
        static_cast<void>(gpu.submit_and_wait({
            .label = "fluid_25d headless simulation frame",
            .work =
                [this, frame, profile_recorder, forcing, frame_index,
                 record_flow_inspection_quiver](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
                    if (profiler != nullptr) {
                        profiler->begin_frame(commands.command_buffer(), frame.frame_slot.index);
                    }
                    if (record_flow_inspection_quiver && quiver_reset_requested_ &&
                        !reset_requested_) {
                        // Seed from the capture's current initial state before
                        // the first numerical update. The reset is still
                        // presentation-only and outside the solver timestamp;
                        // later updates remain after each completed solver
                        // record below. An explicit solver reset instead stays
                        // queued for that post-solver step, where it observes
                        // the reset numerical field.
                        record_fluid_25d_flow_inspection_quiver_reset(
                            commands.command_buffer(), resources_, config_.simulation,
                            quiver_reset_requested_);
                    }
                    record_fluid_25d_compute(commands.command_buffer(), resources_,
                                             config_.simulation, false, reset_requested_, false,
                                             profiler, frame.frame_slot.index, forcing);
                    if (record_flow_inspection_quiver) {
                        // This runs after the direct solver recorder returns,
                        // so headless numerical timestamps and oracle command
                        // streams remain solver-only.
                        record_fluid_25d_flow_inspection_quiver_step(
                            commands.command_buffer(), resources_, config_.simulation,
                            quiver_reset_requested_);
                    }
                    commands.submit_and_wait();
                    if (profiler != nullptr) {
                        profiler->collect(frame.frame_slot.index);
                        record_gpu_timings(profile_recorder, frame_index,
                                           resources_.latest_timings());
                    }
                },
        }));
        record_headless_profile_diagnostics_if_requested(gpu, profile_recorder, frame_index);
        if (oracle_.has_value() || finite_volume_oracle_.has_value()) {
            const Fluid25DStepLedger step_ledger = oracle_.has_value()
                                                     ? oracle_->step(source_rate_scale)
                                                     : finite_volume_oracle_->step_with_dye(
                                                           source_rate_scale)
                                                           .water;
            expected_source_volume_m3_ += step_ledger.source_volume_m3;
            expected_sink_volume_m3_ += step_ledger.sink_volume_m3;
            expected_boundary_outflow_volume_m3_ += step_ledger.boundary_outflow_volume_m3;
            if (finite_volume_oracle_.has_value()) {
                const Fluid25DTracerStepLedger& tracer =
                    finite_volume_oracle_->last_tracer_step_ledger();
                expected_tracer_source_amount_m3_ += tracer.source_amount_m3;
                expected_tracer_sink_amount_m3_ += tracer.sink_amount_m3;
                expected_tracer_boundary_outflow_amount_m3_ +=
                    tracer.boundary_outflow_amount_m3;
            }
        }
        source_schedule_.advance_fixed_step();
        dye_source_schedule_.advance_fixed_step();
    }

    void record_headless_profile_diagnostics_if_requested(
        cubey::ProjectGpuServices& gpu, cubey::profiling::ProfileRecorder* profile_recorder,
        std::uint64_t frame_index) {
        if (!should_record_fluid_25d_profile_diagnostics(profile_recorder, config_.common,
                                                         frame_index)) {
            return;
        }
        const std::size_t cells = fluid_25d_cell_count(config_.simulation);
        const cubey::vulkan::Buffer& current_depth =
            resources_.current_depth_is_a() ? resources_.depth_a() : resources_.depth_b();
        const std::vector<float> depth_m =
            readback_values<float>(gpu, current_depth, cells, "profile diagnostic depth");
        const std::vector<Fluid25DVelocityGpu> velocity = readback_values<Fluid25DVelocityGpu>(
            gpu, resources_.velocity(), cells, "profile diagnostic velocity");
        const std::vector<Fluid25DLedgerGpu> ledger = readback_values<Fluid25DLedgerGpu>(
            gpu, resources_.ledger(), cells, "profile diagnostic ledger");
        const cubey::vulkan::Buffer& current_tracer_q =
            resources_.current_depth_is_a() ? resources_.tracer_q_a() : resources_.tracer_q_b();
        const std::vector<float> tracer_q_m =
            readback_values<float>(gpu, current_tracer_q, cells, "profile diagnostic tracer q");
        const std::vector<Fluid25DTracerLedgerGpu> tracer_ledger =
            readback_values<Fluid25DTracerLedgerGpu>(
                gpu, resources_.tracer_ledger(), cells, "profile diagnostic tracer ledger");
        if (config_.simulation.solver == Fluid25DSolver::FiniteVolume) {
            const std::uint32_t status_flags = read_finite_volume_status(gpu, "profile diagnostic");
            profile_recorder->record_metric(frame_index, "fluid_25d.solver",
                                            "finite_volume_status_flags",
                                            static_cast<double>(status_flags));
        }
        const Fluid25DProfileDiagnostics diagnostics = compute_fluid_25d_profile_diagnostics(
            config_.simulation, depth_m, velocity, ledger, initial_water_volume_m3_);
        record_fluid_25d_profile_diagnostics(*profile_recorder, frame_index, diagnostics);
        const Fluid25DTracerProfileDiagnostics tracer_diagnostics =
            compute_fluid_25d_tracer_profile_diagnostics(config_.simulation, depth_m, tracer_q_m,
                                                         scenario_.sink_depth_rate_m_per_s,
                                                         tracer_ledger);
        record_fluid_25d_tracer_profile_diagnostics(*profile_recorder, frame_index,
                                                    tracer_diagnostics);
        if (config_.simulation.scenario == Fluid25DScenario::SourceOutletDemo) {
            const auto stations = fluid_25d_source_outlet_cross_section_stations(
                config_.simulation.grid_width, config_.simulation.grid_height);
            for (const Fluid25DSourceOutletCrossSectionStation& station : stations) {
                const Fluid25DSourceOutletCrossSectionDiagnostics section =
                    compute_fluid_25d_source_outlet_cross_section_diagnostics(
                        config_.simulation, station, scenario_.terrain_height_m, depth_m, velocity);
                record_fluid_25d_source_outlet_cross_section_diagnostics(*profile_recorder,
                                                                         frame_index, section);
            }
        }
    }

    [[nodiscard]] std::uint32_t read_finite_volume_status(cubey::ProjectGpuServices& gpu,
                                                          const char* context) const {
        const std::vector<Fluid25DFiniteVolumeStatusGpu> status =
            readback_values<Fluid25DFiniteVolumeStatusGpu>(gpu, resources_.finite_volume_status(),
                                                           1U, context);
        const Fluid25DFiniteVolumeStatusGpu& value = status.front();
        if (value.flags_reserved[3] != 0U) {
            throw std::runtime_error("fluid 2.5D finite-volume GPU status padding is nonzero");
        }
        if (value.flags_reserved[0] != 0U) {
            throw std::runtime_error("fluid 2.5D finite-volume GPU status is nonzero: flags=" +
                                     std::to_string(value.flags_reserved[0]));
        }
        return value.flags_reserved[0];
    }

    void validate_gpu_oracle(cubey::ProjectGpuServices& gpu) {
        if (!oracle_.has_value() && !finite_volume_oracle_.has_value()) {
            // Ordinary headless finite-volume capture still surfaces a sticky
            // device failure at its one final capture boundary. Windowed use
            // deliberately remains GPU-resident and therefore cannot report
            // the flag to the host without an explicit diagnostic mode.
            if (config_.simulation.solver == Fluid25DSolver::FiniteVolume) {
                static_cast<void>(read_finite_volume_status(gpu, "headless final status"));
            }
            return;
        }
        const std::size_t cells = fluid_25d_cell_count(config_.simulation);
        const cubey::vulkan::Buffer& current_depth =
            resources_.current_depth_is_a() ? resources_.depth_a() : resources_.depth_b();
        const std::vector<float> actual_depth =
            readback_values<float>(gpu, current_depth, cells, "oracle depth");
        const std::vector<Fluid25DLedgerGpu> actual_ledger =
            readback_values<Fluid25DLedgerGpu>(gpu, resources_.ledger(), cells, "oracle ledger");
        const std::vector<Fluid25DVelocityGpu> actual_velocity =
            readback_values<Fluid25DVelocityGpu>(gpu, resources_.velocity(), cells,
                                                 "oracle velocity");

        float maximum_depth_error = 0.0F;
        float maximum_velocity_error = 0.0F;
        float maximum_flux_error = 0.0F;
        float maximum_momentum_error = 0.0F;
        double actual_source_volume_m3 = 0.0;
        double actual_sink_volume_m3 = 0.0;
        double actual_boundary_outflow_volume_m3 = 0.0;
        for (std::size_t index = 0; index < cells; ++index) {
            if (!finite(actual_depth[index]) || actual_depth[index] < 0.0F) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed nonfinite or negative depth");
            }
            for (const float component : actual_velocity[index].velocity_wet) {
                if (!finite(component)) {
                    throw std::runtime_error(
                        "fluid 2.5D GPU oracle observed nonfinite velocity state");
                }
            }
            if (actual_velocity[index].velocity_wet[2] != 0.0F &&
                actual_velocity[index].velocity_wet[2] != 1.0F) {
                throw std::runtime_error("fluid 2.5D GPU oracle observed invalid wet-state bit");
            }
            actual_source_volume_m3 += actual_ledger[index].source_sink_boundary_reserved_m3[0];
            actual_sink_volume_m3 += actual_ledger[index].source_sink_boundary_reserved_m3[1];
            actual_boundary_outflow_volume_m3 +=
                actual_ledger[index].source_sink_boundary_reserved_m3[2];
            for (const float component : actual_ledger[index].source_sink_boundary_reserved_m3) {
                if (!finite(component)) {
                    throw std::runtime_error("fluid 2.5D GPU oracle observed nonfinite ledger");
                }
            }
            if (actual_ledger[index].source_sink_boundary_reserved_m3[0] < 0.0F ||
                actual_ledger[index].source_sink_boundary_reserved_m3[1] < 0.0F ||
                actual_ledger[index].source_sink_boundary_reserved_m3[2] < 0.0F ||
                actual_ledger[index].source_sink_boundary_reserved_m3[3] != 0.0F) {
                throw std::runtime_error("fluid 2.5D GPU oracle observed invalid ledger state");
            }
        }
        const double source_ledger_error =
            std::abs(actual_source_volume_m3 - expected_source_volume_m3_);
        const double sink_ledger_error = std::abs(actual_sink_volume_m3 - expected_sink_volume_m3_);
        const double boundary_ledger_error =
            std::abs(actual_boundary_outflow_volume_m3 - expected_boundary_outflow_volume_m3_);
        const double source_ledger_tolerance_m3 =
            cumulative_ledger_tolerance_m3(expected_source_volume_m3_);
        const double sink_ledger_tolerance_m3 =
            cumulative_ledger_tolerance_m3(expected_sink_volume_m3_);
        const double boundary_ledger_tolerance_m3 =
            cumulative_ledger_tolerance_m3(expected_boundary_outflow_volume_m3_);
        if (oracle_.has_value()) {
            const std::vector<Fluid25DFluxGpu> actual_flux =
                readback_values<Fluid25DFluxGpu>(gpu, resources_.flux(), cells, "oracle flux");
            for (std::size_t index = 0; index < cells; ++index) {
                maximum_depth_error =
                    std::max(maximum_depth_error,
                             std::abs(actual_depth[index] - oracle_->water_depth_m()[index]));
                for (std::size_t component = 0; component < 2U; ++component) {
                    const float actual = actual_velocity[index].velocity_wet[component];
                    const float expected = component == 0U
                                               ? oracle_->velocity_m_per_s()[index].x_m_per_s
                                               : oracle_->velocity_m_per_s()[index].y_m_per_s;
                    maximum_velocity_error =
                        std::max(maximum_velocity_error, std::abs(actual - expected));
                }
                const float expected_wet = oracle_->wet_mask()[index] == 0U ? 0.0F : 1.0F;
                maximum_velocity_error =
                    std::max(maximum_velocity_error,
                             std::abs(actual_velocity[index].velocity_wet[2] - expected_wet));
                for (std::size_t face = 0; face < 4U; ++face) {
                    const float actual = actual_flux[index].faces_m3_per_s[face];
                    if (!finite(actual) || actual < 0.0F) {
                        throw std::runtime_error(
                            "fluid 2.5D GPU oracle observed invalid face flux");
                    }
                    maximum_flux_error =
                        std::max(maximum_flux_error,
                                 std::abs(actual - oracle_->outgoing_flux_m3_per_s()[index][face]));
                }
            }
            if (maximum_depth_error > kDepthToleranceM ||
                maximum_flux_error > kFluxToleranceM3PerS ||
                maximum_velocity_error > kVelocityToleranceMPerS ||
                source_ledger_error > source_ledger_tolerance_m3 ||
                sink_ledger_error > sink_ledger_tolerance_m3 ||
                boundary_ledger_error > boundary_ledger_tolerance_m3) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle mismatch: max_depth=" +
                    std::to_string(maximum_depth_error) +
                    " max_flux=" + std::to_string(maximum_flux_error) +
                    " max_velocity=" + std::to_string(maximum_velocity_error) +
                    " source_ledger=" + std::to_string(source_ledger_error) +
                    " sink_ledger=" + std::to_string(sink_ledger_error) +
                    " boundary_ledger=" + std::to_string(boundary_ledger_error));
            }
            std::printf("fluid_25d_gpu_oracle: PASS solver=virtual-pipes scenario=%s "
                        "max_depth=%.7f max_flux=%.7f max_velocity=%.7f source_ledger=%.7f "
                        "sink_ledger=%.7f boundary_ledger=%.7f\n",
                        fluid_25d_scenario_name(config_.simulation.scenario), maximum_depth_error,
                        maximum_flux_error, maximum_velocity_error, source_ledger_error,
                        sink_ledger_error, boundary_ledger_error);
            return;
        }

        const std::uint32_t status_flags = read_finite_volume_status(gpu, "oracle status");
        const std::vector<Fluid25DMomentumGpu> actual_momentum =
            readback_values<Fluid25DMomentumGpu>(
                gpu,
                resources_.current_depth_is_a() ? resources_.momentum_a() : resources_.momentum_b(),
                cells, "oracle finite-volume momentum");
        const std::vector<float> actual_tracer_q = readback_values<float>(
            gpu, resources_.current_depth_is_a() ? resources_.tracer_q_a() : resources_.tracer_q_b(),
            cells, "oracle finite-volume tracer q");
        const std::vector<Fluid25DTracerLedgerGpu> actual_tracer_ledger =
            readback_values<Fluid25DTracerLedgerGpu>(gpu, resources_.tracer_ledger(), cells,
                                                      "oracle finite-volume tracer ledger");
        float maximum_tracer_q_error = 0.0F;
        float maximum_tracer_concentration_error = 0.0F;
        double actual_tracer_amount_m3 = 0.0;
        double actual_tracer_source_amount_m3 = 0.0;
        double actual_tracer_sink_amount_m3 = 0.0;
        double actual_tracer_boundary_outflow_amount_m3 = 0.0;
        const double cell_area_m2 = static_cast<double>(config_.simulation.cell_size_m) *
                                    static_cast<double>(config_.simulation.cell_size_m);
        for (std::size_t index = 0; index < cells; ++index) {
            maximum_depth_error = std::max(
                maximum_depth_error,
                std::abs(actual_depth[index] - finite_volume_oracle_->water_depth_m()[index]));
            const Fluid25DMomentum expected_momentum =
                finite_volume_oracle_->momentum_m2_per_s()[index];
            for (std::size_t component = 0; component < 2U; ++component) {
                const float actual = actual_momentum[index].momentum_xy_reserved[component];
                const float expected =
                    component == 0U ? expected_momentum.x_m2_per_s : expected_momentum.y_m2_per_s;
                if (!finite(actual)) {
                    throw std::runtime_error(
                        "fluid 2.5D GPU oracle observed invalid finite-volume momentum");
                }
                maximum_momentum_error =
                    std::max(maximum_momentum_error, std::abs(actual - expected));
                const float expected_velocity =
                    component == 0U ? finite_volume_oracle_->velocity_m_per_s()[index].x_m_per_s
                                    : finite_volume_oracle_->velocity_m_per_s()[index].y_m_per_s;
                maximum_velocity_error = std::max(
                    maximum_velocity_error,
                    std::abs(actual_velocity[index].velocity_wet[component] - expected_velocity));
            }
            if (actual_momentum[index].momentum_xy_reserved[2] != 0.0F ||
                actual_momentum[index].momentum_xy_reserved[3] != 0.0F) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed nonzero finite-volume padding");
            }
            const float expected_wet = finite_volume_oracle_->wet_mask()[index] == 0U ? 0.0F : 1.0F;
            maximum_velocity_error =
                std::max(maximum_velocity_error,
                         std::abs(actual_velocity[index].velocity_wet[2] - expected_wet));
            const float actual_q = actual_tracer_q[index];
            const float expected_q = finite_volume_oracle_->tracer_mass_per_area_m()[index];
            if (!finite(actual_q) || actual_q < -1.0e-7F ||
                actual_q > actual_depth[index] + 1.0e-7F) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed invalid finite-volume tracer q");
            }
            maximum_tracer_q_error =
                std::max(maximum_tracer_q_error, std::abs(actual_q - expected_q));
            if (actual_depth[index] > config_.simulation.minimum_wet_depth_m &&
                finite_volume_oracle_->water_depth_m()[index] >
                    config_.simulation.minimum_wet_depth_m) {
                const float actual_concentration = actual_q / actual_depth[index];
                const float expected_concentration =
                    finite_volume_oracle_->tracer_concentration()[index];
                maximum_tracer_concentration_error = std::max(
                    maximum_tracer_concentration_error,
                    std::abs(actual_concentration - expected_concentration));
            }
            actual_tracer_amount_m3 += static_cast<double>(std::max(0.0F, actual_q)) *
                                       cell_area_m2;
            const Fluid25DTracerLedgerGpu& tracer_ledger = actual_tracer_ledger[index];
            for (std::size_t component_index = 0U;
                 component_index < tracer_ledger.source_sink_boundary_reserved_m3.size();
                 ++component_index) {
                const float component =
                    tracer_ledger.source_sink_boundary_reserved_m3[component_index];
                if (!finite(component)) {
                    throw std::runtime_error(
                        "fluid 2.5D GPU oracle observed nonfinite tracer ledger at cell=" +
                        std::to_string(index) + " component=" +
                        std::to_string(component_index));
                }
            }
            if (tracer_ledger.source_sink_boundary_reserved_m3[0] < 0.0F ||
                tracer_ledger.source_sink_boundary_reserved_m3[1] < 0.0F ||
                tracer_ledger.source_sink_boundary_reserved_m3[2] < 0.0F ||
                tracer_ledger.source_sink_boundary_reserved_m3[3] != 0.0F) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed invalid tracer ledger at cell=" +
                    std::to_string(index) + " values=" +
                    std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[0]) + "," +
                    std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[1]) + "," +
                    std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[2]) + "," +
                    std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[3]));
            }
            actual_tracer_source_amount_m3 +=
                tracer_ledger.source_sink_boundary_reserved_m3[0];
            actual_tracer_sink_amount_m3 += tracer_ledger.source_sink_boundary_reserved_m3[1];
            actual_tracer_boundary_outflow_amount_m3 +=
                tracer_ledger.source_sink_boundary_reserved_m3[2];
        }
        const double tracer_source_ledger_error =
            std::abs(actual_tracer_source_amount_m3 - expected_tracer_source_amount_m3_);
        const double tracer_sink_ledger_error =
            std::abs(actual_tracer_sink_amount_m3 - expected_tracer_sink_amount_m3_);
        const double tracer_boundary_ledger_error = std::abs(
            actual_tracer_boundary_outflow_amount_m3 - expected_tracer_boundary_outflow_amount_m3_);
        const double tracer_conservation_residual_m3 =
            actual_tracer_amount_m3 - actual_tracer_source_amount_m3 +
            actual_tracer_sink_amount_m3 + actual_tracer_boundary_outflow_amount_m3;
        const double expected_tracer_amount_m3 = finite_volume_oracle_->total_tracer_amount_m3();
        const double tracer_amount_error =
            std::abs(actual_tracer_amount_m3 - expected_tracer_amount_m3);
        const double tracer_source_ledger_tolerance_m3 =
            cumulative_ledger_tolerance_m3(expected_tracer_source_amount_m3_);
        const double tracer_sink_ledger_tolerance_m3 =
            cumulative_ledger_tolerance_m3(expected_tracer_sink_amount_m3_);
        const double tracer_boundary_ledger_tolerance_m3 =
            cumulative_ledger_tolerance_m3(expected_tracer_boundary_outflow_amount_m3_);
        const double tracer_amount_tolerance_m3 =
            cumulative_ledger_tolerance_m3(expected_tracer_amount_m3);
        const double tracer_conservation_tolerance_m3 =
            cumulative_ledger_tolerance_m3(actual_tracer_amount_m3);
        if (status_flags != 0U || maximum_depth_error > kDepthToleranceM ||
            maximum_momentum_error > kMomentumToleranceM2PerS ||
            maximum_velocity_error > kVelocityToleranceMPerS ||
            maximum_tracer_q_error > kTracerQToleranceM ||
            maximum_tracer_concentration_error > kTracerConcentrationTolerance ||
            source_ledger_error > source_ledger_tolerance_m3 ||
            sink_ledger_error > sink_ledger_tolerance_m3 ||
            boundary_ledger_error > boundary_ledger_tolerance_m3 ||
            tracer_source_ledger_error > tracer_source_ledger_tolerance_m3 ||
            tracer_sink_ledger_error > tracer_sink_ledger_tolerance_m3 ||
            tracer_boundary_ledger_error > tracer_boundary_ledger_tolerance_m3 ||
            tracer_amount_error > tracer_amount_tolerance_m3 ||
            std::abs(tracer_conservation_residual_m3) > tracer_conservation_tolerance_m3) {
            throw std::runtime_error("fluid 2.5D finite-volume GPU oracle mismatch: status=" +
                                     std::to_string(status_flags) +
                                     " max_depth=" + std::to_string(maximum_depth_error) +
                                     " max_momentum=" + std::to_string(maximum_momentum_error) +
                                     " max_velocity=" + std::to_string(maximum_velocity_error) +
                                     " max_tracer_q=" + std::to_string(maximum_tracer_q_error) +
                                     " max_tracer_concentration=" +
                                     std::to_string(maximum_tracer_concentration_error) +
                                     " source_ledger=" + std::to_string(source_ledger_error) +
                                     " sink_ledger=" + std::to_string(sink_ledger_error) +
                                     " boundary_ledger=" + std::to_string(boundary_ledger_error) +
                                     " tracer_amount=" + std::to_string(tracer_amount_error) +
                                     " tracer_source_ledger=" +
                                     std::to_string(tracer_source_ledger_error) +
                                     " tracer_sink_ledger=" +
                                     std::to_string(tracer_sink_ledger_error) +
                                     " tracer_boundary_ledger=" +
                                     std::to_string(tracer_boundary_ledger_error) +
                                     " tracer_conservation=" +
                                     std::to_string(tracer_conservation_residual_m3));
        }
        std::printf("fluid_25d_gpu_oracle: PASS solver=finite-volume scenario=%s status=%u "
                    "max_depth=%.7f max_momentum=%.7f max_velocity=%.7f max_tracer_q=%.7f "
                    "max_tracer_concentration=%.7f tracer_amount=%.7f source_ledger=%.7f sink_ledger=%.7f "
                    "boundary_ledger=%.7f tracer_source_ledger=%.7f tracer_sink_ledger=%.7f "
                    "tracer_boundary_ledger=%.7f tracer_conservation=%.7f\n",
                    fluid_25d_scenario_name(config_.simulation.scenario), status_flags,
                    maximum_depth_error, maximum_momentum_error, maximum_velocity_error,
                    maximum_tracer_q_error, maximum_tracer_concentration_error, tracer_amount_error,
                    source_ledger_error, sink_ledger_error, boundary_ledger_error,
                    tracer_source_ledger_error, tracer_sink_ledger_error,
                    tracer_boundary_ledger_error, tracer_conservation_residual_m3);
    }

    int run_headless() {
        if (!config_.gpu_oracle_validation) {
            std::printf(
                "fluid_25d: GPU oracle validation disabled; simulation state remains "
                "GPU-resident except finite-volume checks sticky status at final capture\n");
        }
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_.common;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const std::uint32_t frame_slot_count =
                cubey::host::headless_capture_frame_slot_count(config_.common);
            create_global_resources_if_needed(context.device(), context.gpu(), frame_slot_count);
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            resources_.create_render_pipelines(context.device(), target.format,
                                               VK_FORMAT_D32_SFLOAT, target.extent);
            graph_executor_.clear();
            graph_executor_.resize(frame_slot_count);
        };
        cubey::host::install_headless_simulation_driver(
            callbacks, config_.common,
            {
                .png_frame_count = headless_frame_count(config_.common),
                .png_timing =
                    [this](std::uint64_t frame_index) {
                        return fixed_headless_timing(config_.simulation, frame_index);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext& context,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        record_headless_simulation_frame(runtime_.gpu(), frame,
                                                         context.profile_recorder());
                    },
            });
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            validate_gpu_oracle(runtime_.gpu());
            const cubey::render::CompiledRenderGraph graph = build_fluid_25d_frame_graph(
                target, resources_, config_.simulation, presentation_view_, catchment_view_,
                debug_view_, render_camera(target.extent),
                Fluid25DRenderTargetMode::ColorAttachment, false, false, reset_requested_,
                presentation_cue_reset_requested_, quiver_reset_requested_);
            graph_executor_.record(
                {
                    .device = &context.device(),
                    .command_buffer = command_buffer,
                    .frame_slot = frame.frame_slot,
                    .label = "vkEndCommandBuffer fluid_25d headless graph",
                    .command_buffer_mode =
                        cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                },
                graph);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            graph_executor_.clear();
            resources_.destroy_all_resources();
            runtime_.detach_gpu_if_attached();
        };
        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    Fluid25DProjectConfig config_;
    Fluid25DWindowedPacing windowed_pacing_;
    Fluid25DScenarioData scenario_;
    cubey::ProjectRuntimeAdapter runtime_{1};
    Fluid25DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::math::Vec3 catchment_target_{0.0F, 0.0F, 0.0F};
    Fluid25DPresentationView presentation_view_ = Fluid25DPresentationView::Catchment;
    Fluid25DCatchmentView catchment_view_ = Fluid25DCatchmentView::Composite;
    Fluid25DDebugView debug_view_ = Fluid25DDebugView::Terrain;
    std::optional<Fluid25DOracle> oracle_;
    std::optional<Fluid25DFiniteVolumeOracle> finite_volume_oracle_;
    Fluid25DSourceRateSchedule source_schedule_;
    Fluid25DDyeSourceSchedule dye_source_schedule_;
    double expected_source_volume_m3_ = 0.0;
    double expected_sink_volume_m3_ = 0.0;
    double expected_boundary_outflow_volume_m3_ = 0.0;
    double expected_tracer_source_amount_m3_ = 0.0;
    double expected_tracer_sink_amount_m3_ = 0.0;
    double expected_tracer_boundary_outflow_amount_m3_ = 0.0;
    double initial_water_volume_m3_ = 0.0;
    bool paused_ = false;
    // Headless simulation keeps the uploaded initial numerical state and
    // solver dispatch stream unchanged. Windowed presentation initializes its
    // render-only cue separately on the first frame or an explicit reset.
    bool reset_requested_ = false;
    bool presentation_cue_reset_requested_ = true;
    bool quiver_reset_requested_ = true;
};

} // namespace

int run_fluid_25d(const Fluid25DProjectConfig& config) {
    Fluid25DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid::fluid_25d
