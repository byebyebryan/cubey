#include "fluid_25d_app.h"

#include "fluid_25d_commands.h"
#include "fluid_25d_diagnostics.h"
#include "fluid_25d_finite_volume_oracle.h"
#include "fluid_25d_gpu_resources.h"
#include "fluid_25d_hillside_supply.h"
#include "fluid_25d_mass_audit.h"
#include "fluid_25d_motion_markers.h"
#include "fluid_25d_natural_flow_recipe.h"
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
#include <bit>
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
    const bool natural_flow_study = fluid_25d_is_natural_terrain_study(config.simulation.scenario);
    if (!terrain_case && !mountain_source_outlet && !natural_flow_study) {
        Fluid25DScenarioData scenario = make_fluid_25d_scenario(
            config.simulation.scenario, config.simulation.grid_width, config.simulation.grid_height,
            config.simulation.cell_size_m, config.simulation.headwaters_source_scale);
        if (!config.mass_audit_control.empty() && config.mass_audit_control != "none") {
            std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
            std::fill(scenario.source_depth_rate_m_per_s.begin(),
                      scenario.source_depth_rate_m_per_s.end(), 0.0F);
            std::fill(scenario.sink_depth_rate_m_per_s.begin(),
                      scenario.sink_depth_rate_m_per_s.end(), 0.0F);
            std::fill(scenario.boundary_outflow_face_mask.begin(),
                      scenario.boundary_outflow_face_mask.end(), 0U);
            scenario.source_cell = scenario.sink_cell = scenario.outlet_cell = kFluid25DNoCell;
            if (config.mass_audit_control == "source-basin") {
                std::fill(scenario.initial_water_depth_m.begin(),
                          scenario.initial_water_depth_m.end(), 1.0F);
                // Uniform supply isolates representable source additions;
                // without a surface gradient there is no horizontal transfer.
                std::fill(scenario.source_depth_rate_m_per_s.begin(),
                          scenario.source_depth_rate_m_per_s.end(), 20.0F / 900.0F);
            } else if (config.mass_audit_control == "dam-break") {
                std::fill(scenario.initial_water_depth_m.begin(),
                          scenario.initial_water_depth_m.end(), 0.02F);
                const std::uint32_t width = config.simulation.grid_width;
                const std::uint32_t height = config.simulation.grid_height;
                scenario.initial_water_depth_m[(height / 2U) * width + width / 2U] = 2.0F;
            } else {
                throw std::runtime_error("unknown fluid 2.5D mass audit control");
            }
            std::printf("fluid_25d: mass-audit-control=%s closed-boundary diagnostic fields\n",
                        config.mass_audit_control.c_str());
        }
        return scenario;
    }

    cubey::asset::TerrainRasterHeightSource source(config.terrain.heightfield_path.value());
    const float source_spacing_m = source.sample_spacing_m();
    resolve_fluid_25d_terrain_cell_size(config, source_spacing_m);

    Fluid25DScenarioData scenario;
    if (terrain_case) {
        scenario = make_fluid_25d_terrain_scenario(config.simulation, source,
                                                   config.terrain.crop_x.value_or(0U),
                                                   config.terrain.crop_z.value_or(0U));
    } else if (mountain_source_outlet) {
        scenario = make_fluid_25d_mountain_source_outlet_scenario(config.simulation, source);
    } else {
        const Fluid25DNaturalFlowRecipe recipe =
            load_fluid_25d_natural_flow_recipe(config.natural_flow_recipe_path.value());
        validate_fluid_25d_natural_flow_recipe(recipe, config.simulation.grid_width,
                                               config.simulation.grid_height,
                                               config.simulation.cell_size_m);
        scenario = make_fluid_25d_natural_flow_study_scenario(config.simulation, source, recipe,
                                                              config.terrain.crop_x.value(),
                                                              config.terrain.crop_z.value());
    }
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
          windowed_pacing_(config_.simulation.fixed_delta_seconds, config_.presentation_time_scale),
          scenario_(make_startup_scenario(config_)),
          presentation_view_(fluid_25d_presentation_view_from_name(config_.view)),
          catchment_view_(fluid_25d_catchment_view_from_name(config_.catchment_view)),
          debug_view_(fluid_25d_debug_view_from_name(config_.debug_view)) {
        if (config_.hillside_supply_gpu_controls) {
            // Independent synthetic control only, never the terrain study.
            scenario_.source_cell =
                (scenario_.height / 2U) * scenario_.width + scenario_.width / 2U;
            scenario_.source_depth_rate_m_per_s[scenario_.source_cell] = 0.04F;
            std::printf("fluid_25d: synthetic supply control scales 1 / 1.5 / 0.5 / 1\n");
        }
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
        hillside_supply_ = Fluid25DHillsideSupply(config_.hillside_supply_response);
        last_source_m3_per_s_ = config_.simulation.natural_flow_source_m3_per_s;
        show_motion_markers_ = config_.motion_markers;
        if (config_.hillside_inspection_advance_seconds) {
            inspection_advance_.request(*config_.hillside_inspection_advance_seconds,
                                        config_.simulation.fixed_delta_seconds);
            paused_ = true;
        }
        if (config_.hillside_advance_and_continue_seconds) {
            inspection_advance_.request(*config_.hillside_advance_and_continue_seconds,
                                        config_.simulation.fixed_delta_seconds);
            resume_after_advance_ = true;
            paused_ = true;
        }
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
            if (config_.motion_markers)
                motion_markers_.create_render_pipeline(
                    context.device(), context.swapchain().format(), VK_FORMAT_D32_SFLOAT,
                    context.swapchain().extent());
            graph_executor_.clear();
            graph_executor_.resize(context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            graph_executor_.clear();
            motion_markers_.destroy_render_pipeline();
            resources_.destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            const bool was_flow_inspection_active = flow_inspection_active();
            const auto input = context.filtered_input();
            orbit_controller_.update_pointer_input(input, timing.delta_seconds);
            if (input.key_pressed(cubey::input::Key::Space)) {
                if (inspection_advance_.remaining_steps() > 0U) {
                    inspection_advance_.reset();
                    windowed_pacing_.reset();
                    paused_ = true;
                    resume_after_advance_ = false;
                } else {
                    paused_ = !paused_;
                }
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
            motion_markers_.destroy();
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
        const bool was_source_context = config_.hillside_source_context;
        const std::string was_hillside_camera = config_.hillside_camera;
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
            .inspection_advance = inspection_advance_,
            .simulation_elapsed_seconds = source_schedule_.elapsed_seconds(config_.simulation),
            .fixed_delta_seconds = config_.simulation.fixed_delta_seconds,
            .continuous_source_m3_per_s = last_source_m3_per_s_,
            .hillside_supply = hillside_supply_,
            .hillside_depth_cues = config_.catchment_render.hillside_depth_cues,
            .downstream_hillside_cameras_available =
                scenario_.width == 512U && scenario_.height == 512U,
            .hillside_source_context = config_.hillside_source_context,
            .hillside_camera = config_.hillside_camera,
            .resume_after_advance = resume_after_advance_,
            .motion_markers_available = config_.motion_markers,
            .local_motion_markers = config_.motion_marker_mode == "local",
            .show_motion_markers = show_motion_markers_,
            .dye_enabled = config_.simulation.dye_pulse_start_seconds.has_value(),
            .dye_start_seconds = config_.simulation.dye_pulse_start_seconds.value_or(0.0F),
            .dye_end_seconds = config_.simulation.dye_pulse_start_seconds.value_or(0.0F) +
                               config_.simulation.dye_pulse_duration_seconds.value_or(0.0F),
            .paused = paused_,
            .reset_requested = reset_requested_,
            .presentation_cue_reset_requested = presentation_cue_reset_requested_,
            .quiver_reset_requested = quiver_reset_requested_,
        });
        if (was_source_context != config_.hillside_source_context ||
            was_hillside_camera != config_.hillside_camera) {
            configure_catchment_camera();
            orbit_controller_.reset();
        }
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
        if (config_.motion_markers && !motion_markers_.created()) {
            const auto endpoints = fluid_25d_endpoint_markers(config_.simulation, scenario_);
            motion_markers_.create(
                device, runtime_.gpu(), config_.simulation,
                {&resources_.terrain(), &resources_.depth_a(), &resources_.depth_b(),
                 &resources_.velocity(), &resources_.source_rate(),
                 &resources_.finite_volume_status()},
                {endpoints.source_xy_outlet_xy[0], endpoints.source_xy_outlet_xy[1]},
                frame_slot_count, !config_.common.profile_output_prefix.empty(),
                config_.motion_marker_mode == "local" ? Fluid25DMotionMarkerMode::Local
                                                      : Fluid25DMotionMarkerMode::Source);
        }
    }

    void record_marker_timings(cubey::profiling::ProfileRecorder* recorder, std::uint32_t slot,
                               bool draw_only = false, bool update_only = false) {
        auto* profiler = motion_markers_.profiler();
        if (profiler == nullptr || recorder == nullptr)
            return;
        profiler->collect(slot);
        std::vector<cubey::vulkan::GpuPassTiming> timings;
        for (const auto& timing : profiler->latest_timings()) {
            const bool draw = timing.label == "fluid_25d motion markers draw";
            if ((!draw_only || draw) && (!update_only || !draw))
                timings.push_back(timing);
        }
        record_gpu_timings(recorder, motion_markers_.profile_frame_index(slot), timings);
    }

    void record_windowed_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& render_frame) {
        const ProjectFrame project_frame = runtime_.frame_for_timing(render_frame.timing);
        record_marker_timings(context.profile_recorder(), render_frame.frame_slot.index);
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
            hillside_supply_.reset();
            scheduled_source_volume_m3_ = 0.0;
            last_source_m3_per_s_ = config_.simulation.natural_flow_source_m3_per_s;
            windowed_pacing_.reset();
        }
        if (reset_requested_) {
            inspection_advance_.reset();
            resume_after_advance_ = false;
            marker_display_clock_.reset();
            continuous_wall_seconds_ = 0.0;
            continuous_physical_seconds_ = 0.0;
        }
        const bool advancing_for_inspection = inspection_advance_.remaining_steps() > 0U;
        const Fluid25DWindowedPacingFrame pacing =
            advancing_for_inspection
                ? Fluid25DWindowedPacingFrame{}
                : windowed_pacing_.advance(render_frame.timing.delta_seconds, paused_);
        const std::uint32_t fixed_step_count =
            advancing_for_inspection ? inspection_advance_.take_batch() : pacing.fixed_step_count;
        if (advancing_for_inspection && inspection_advance_.remaining_steps() == 0U) {
            paused_ = !resume_after_advance_;
            resume_after_advance_ = false;
            windowed_pacing_.reset();
        }
        marker_display_clock_.update(windowed_pacing_.accumulator_seconds(),
                                     config_.simulation.fixed_delta_seconds, fixed_step_count,
                                     advancing_for_inspection, paused_);
        Fluid25DSourceRateSchedule next_source_schedule = source_schedule_;
        Fluid25DDyeSourceSchedule next_dye_source_schedule = dye_source_schedule_;
        Fluid25DHillsideSupply next_supply = hillside_supply_;
        std::vector<Fluid25DStepForcing> forcings;
        forcings.reserve(fixed_step_count);
        for (std::uint32_t step = 0U; step < fixed_step_count; ++step) {
            forcings.push_back({
                .source_rate_scale = source_rate_scale_for_step(next_source_schedule, next_supply),
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
            Fluid25DRenderTargetMode::Present, true, paused_ && !advancing_for_inspection,
            reset_requested_, presentation_cue_reset_requested_, quiver_reset_requested_, profiler,
            render_frame.frame_slot.index, forcings, config_.catchment_render,
            config_.motion_markers ? &motion_markers_ : nullptr, marker_display_clock_.fraction(),
            show_motion_markers_);
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (config_.motion_markers)
            motion_markers_.begin_frame(render_frame.command_buffer, render_frame.frame_slot.index,
                                        profile_frame_index(project_frame));
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
        hillside_supply_ = std::move(next_supply);
        if (auto* profile = context.profile_recorder(); profile != nullptr) {
            const auto frame_index = profile_frame_index(project_frame);
            const auto metric = [&](const char* name, double value) {
                profile->record_metric(frame_index, "fluid_25d.playback", name, value);
            };
            if (!advancing_for_inspection && !paused_) {
                continuous_wall_seconds_ += render_frame.timing.delta_seconds;
                continuous_physical_seconds_ +=
                    static_cast<double>(fixed_step_count) * config_.simulation.fixed_delta_seconds;
            }
            metric("physical_time_s", source_schedule_.elapsed_seconds(config_.simulation));
            metric("last_step_source_m3_per_s", last_source_m3_per_s_);
            metric("scheduled_source_volume_m3", scheduled_source_volume_m3_);
            metric("manual_supply_override", hillside_supply_.manual() ? 1.0 : 0.0);
            metric("fixed_steps", fixed_step_count);
            metric("paused", paused_ ? 1.0 : 0.0);
            metric("advance_remaining_steps", inspection_advance_.remaining_steps());
            metric("requested_playback_x", config_.presentation_time_scale);
            metric("achieved_continuous_playback_x",
                   continuous_wall_seconds_ > 0.0
                       ? continuous_physical_seconds_ / continuous_wall_seconds_
                       : 0.0);
            metric("dropped_backlog_frames",
                   static_cast<double>(windowed_pacing_.dropped_backlog_frames()));
        }
    }

    float source_rate_scale_for_step(const Fluid25DSourceRateSchedule& clock,
                                     Fluid25DHillsideSupply& supply) {
        const float legacy_scale = clock.source_rate_scale(config_.simulation);
        if (config_.hillside_supply_gpu_controls) {
            constexpr std::array<double, 4> control_times{3598.0, 3600.0, 5400.0, 7200.0};
            return Fluid25DHillsideSupply(true).rate(control_times.at(clock.completed_steps()),
                                                     100.0F) /
                   100.0F;
        }
        if (config_.simulation.scenario != Fluid25DScenario::HillsideFlowStudy)
            return legacy_scale;
        supply.apply_pending();
        const double time =
            static_cast<double>(clock.completed_steps()) * config_.simulation.fixed_delta_seconds;
        const float reference = config_.simulation.natural_flow_source_m3_per_s;
        const float rate = supply.rate(time, reference);
        if (rate != last_source_m3_per_s_)
            std::printf("fluid_25d: supply step=%llu time_s=%.0f Q_m3_per_s=%.1f mode=%s\n",
                        static_cast<unsigned long long>(clock.completed_steps()), time, rate,
                        supply.manual()     ? "manual"
                        : supply.response() ? "response"
                                            : "reference");
        last_source_m3_per_s_ = rate;
        scheduled_source_volume_m3_ +=
            static_cast<double>(rate) * config_.simulation.fixed_delta_seconds;
        return legacy_scale * (rate / reference);
    }

    void configure_catchment_camera() {
        const auto [terrain_minimum, terrain_maximum] = std::minmax_element(
            scenario_.terrain_height_m.begin(), scenario_.terrain_height_m.end());
        const float world_width =
            static_cast<float>(config_.simulation.grid_width - 1U) * config_.simulation.cell_size_m;
        const float world_height = static_cast<float>(config_.simulation.grid_height - 1U) *
                                   config_.simulation.cell_size_m;
        const float horizontal_extent = std::max(world_width, world_height);
        const float render_height_scale = config_.catchment_render.terrain_height_scale.value_or(
            fluid_25d_catchment_height_scale(config_.simulation.scenario));
        const float scaled_terrain_span =
            (*terrain_maximum - *terrain_minimum) * render_height_scale;
        const float default_camera_terrain_span =
            (*terrain_maximum - *terrain_minimum) *
            fluid_25d_catchment_height_scale(config_.simulation.scenario);
        float framing_horizontal_extent = horizontal_extent;
        catchment_target_ = {
            0.0F,
            (*terrain_minimum + *terrain_maximum) * 0.5F * render_height_scale,
            0.0F,
        };
        const bool authored_source_outlet =
            fluid_25d_is_source_outlet_demo(config_.simulation.scenario);
        const bool natural_flow_study =
            config_.simulation.scenario == Fluid25DScenario::NaturalFlowStudy;
        const bool hillside = config_.simulation.scenario == Fluid25DScenario::HillsideFlowStudy;
        const bool hillside_close =
            hillside &&
            (config_.hillside_camera == "source" || config_.hillside_camera == "branch" ||
             config_.hillside_camera == "travel" || config_.hillside_camera == "collection" ||
             (config_.hillside_camera.empty() && config_.hillside_source_context));
        if (hillside_close) {
            framing_horizontal_extent =
                std::min(horizontal_extent, config_.hillside_camera == "source"       ? 1800.0F
                                            : config_.hillside_camera == "branch"     ? 2400.0F
                                            : config_.hillside_camera == "travel"     ? 2800.0F
                                            : config_.hillside_camera == "collection" ? 1600.0F
                                                                                      : 3200.0F);
            const std::uint32_t source_x =
                static_cast<std::uint32_t>(scenario_.source_cell % scenario_.width);
            const std::uint32_t source_z =
                static_cast<std::uint32_t>(scenario_.source_cell / scenario_.width);
            catchment_target_.x =
                (static_cast<float>(source_x) - 0.5F * static_cast<float>(scenario_.width - 1U)) *
                config_.simulation.cell_size_m;
            catchment_target_.z =
                (static_cast<float>(source_z) - 0.5F * static_cast<float>(scenario_.height - 1U)) *
                config_.simulation.cell_size_m;
            catchment_target_.y =
                scenario_.terrain_height_m[scenario_.source_cell] * render_height_scale;
            if (config_.hillside_camera == "branch") {
                // Framing only: the lower branch observed in the retained
                // study lies south-west of the source. No route or source
                // cell is changed by this presentation preset.
                const std::uint32_t branch_x = source_x > 12U ? source_x - 12U : 0U;
                const std::uint32_t branch_z = std::min(source_z + 30U, scenario_.height - 1U);
                catchment_target_.x = (static_cast<float>(branch_x) -
                                       0.5F * static_cast<float>(scenario_.width - 1U)) *
                                      config_.simulation.cell_size_m;
                catchment_target_.z = (static_cast<float>(branch_z) -
                                       0.5F * static_cast<float>(scenario_.height - 1U)) *
                                      config_.simulation.cell_size_m;
                catchment_target_.y =
                    scenario_
                        .terrain_height_m[static_cast<std::size_t>(branch_z) * scenario_.width +
                                          branch_x] *
                    render_height_scale;
            }
            if (config_.hillside_camera == "travel" || config_.hillside_camera == "collection") {
                // Frozen 512 reference at 7200 s: deepest collection beyond
                // 2 km is cell (58,251), relative to source (213,213). The
                // travel view covers the intervening western descent. These
                // are observation targets, not a prescribed hydraulic path.
                const int dx = config_.hillside_camera == "travel" ? -75 : -155;
                const int dz = config_.hillside_camera == "travel" ? 21 : 38;
                const auto x = static_cast<std::uint32_t>(std::clamp(
                    static_cast<int>(source_x) + dx, 0, static_cast<int>(scenario_.width) - 1));
                const auto z = static_cast<std::uint32_t>(std::clamp(
                    static_cast<int>(source_z) + dz, 0, static_cast<int>(scenario_.height) - 1));
                catchment_target_.x =
                    (static_cast<float>(x) - 0.5F * static_cast<float>(scenario_.width - 1U)) *
                    config_.simulation.cell_size_m;
                catchment_target_.z =
                    (static_cast<float>(z) - 0.5F * static_cast<float>(scenario_.height - 1U)) *
                    config_.simulation.cell_size_m;
                catchment_target_.y =
                    scenario_.terrain_height_m[static_cast<std::size_t>(z) * scenario_.width + x] *
                    render_height_scale;
            }
        }
        if (authored_source_outlet || natural_flow_study) {
            // Each explanation route gets a one-time endpoint framing with
            // modest terrain context. This changes neither later orbit/zoom
            // input nor any non-demonstration scenario.
            const auto world_x = [this](std::size_t index) {
                const std::uint32_t x = static_cast<std::uint32_t>(index % scenario_.width);
                return (static_cast<float>(x) - (0.5F * static_cast<float>(scenario_.width - 1U))) *
                       config_.simulation.cell_size_m;
            };
            const auto world_z = [this](std::size_t index) {
                const std::uint32_t y =
                    static_cast<std::uint32_t>(index / static_cast<std::size_t>(scenario_.width));
                return (static_cast<float>(y) -
                        (0.5F * static_cast<float>(scenario_.height - 1U))) *
                       config_.simulation.cell_size_m;
            };
            const std::size_t endpoint_cell =
                natural_flow_study ? scenario_.outlet_cell : scenario_.sink_cell;
            const float route_span =
                std::max(std::abs(world_x(endpoint_cell) - world_x(scenario_.source_cell)),
                         std::abs(world_z(endpoint_cell) - world_z(scenario_.source_cell)));
            framing_horizontal_extent =
                std::max(32.0F, fluid_25d_catchment_home_horizontal_extent(
                                    config_.simulation.scenario, horizontal_extent, route_span));
            catchment_target_.x = 0.5F * (world_x(scenario_.source_cell) + world_x(endpoint_cell));
            catchment_target_.z = 0.5F * (world_z(scenario_.source_cell) + world_z(endpoint_cell));
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
        float camera_distance =
            std::max(framing_horizontal_extent *
                         fluid_25d_catchment_home_distance_scale(config_.simulation.scenario),
                     default_camera_terrain_span * (hillside ? 1.5F : 6.0F) + 16.0F);
        if (hillside && config_.hillside_camera == "travel")
            camera_distance = 3000.0F;
        if (hillside && config_.hillside_camera == "collection")
            camera_distance = 1200.0F;
        const auto [minimum_camera_distance, maximum_camera_distance] =
            fluid_25d_catchment_orbit_limits(config_.simulation.scenario, horizontal_extent,
                                             framing_horizontal_extent);
        if (config_.catchment_render.home_camera_distance_m.has_value()) {
            const float requested_distance = *config_.catchment_render.home_camera_distance_m;
            if (requested_distance < minimum_camera_distance ||
                requested_distance > maximum_camera_distance) {
                throw std::runtime_error("fluid 2.5D terrain home camera distance falls outside "
                                         "the current orbit limits");
            }
            camera_distance = requested_distance;
        }
        orbit_controller_.set_distance_limits(minimum_camera_distance, maximum_camera_distance);
        orbit_controller_.set_pitch_limits(-0.38F, 0.38F);
        orbit_controller_.set_home_distance(camera_distance);
        const float near_plane =
            std::max(kCatchmentCameraMinimumNearPlaneM,
                     framing_horizontal_extent * kCatchmentCameraNearExtentFraction);
        camera_.set_projection(fluid_25d_catchment_home_fovy_radians(
                                   std::numbers::pi_v<float> / 3.0F, config_.simulation.scenario),
                               near_plane, camera_distance * 5.0F + scaled_terrain_span + 64.0F);
    }

    [[nodiscard]] cubey::Transform3D render_camera_transform() const {
        return cubey::orbit_camera_transform({
            .target = catchment_target_,
            .distance = orbit_controller_.distance(),
            .yaw = kCatchmentCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = (fluid_25d_is_natural_terrain_study(config_.simulation.scenario) &&
                              config_.natural_flow_home_pitch_radians.has_value()
                          ? *config_.natural_flow_home_pitch_radians
                          : fluid_25d_catchment_home_pitch(kCatchmentCameraBasePitch,
                                                           config_.simulation.scenario)) +
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
            hillside_supply_.reset();
            scheduled_source_volume_m3_ = 0.0;
            last_source_m3_per_s_ = config_.simulation.natural_flow_source_m3_per_s;
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
        const float source_rate_scale =
            source_rate_scale_for_step(source_schedule_, hillside_supply_);
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
                    if (config_.motion_markers) {
                        record_marker_timings(profile_recorder, frame.frame_slot.index, true,
                                              false);
                        motion_markers_.begin_frame(commands.command_buffer(),
                                                    frame.frame_slot.index, frame_index);
                    }
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
                    if (config_.motion_markers)
                        motion_markers_.record_step(commands.command_buffer(),
                                                    resources_.current_depth_is_a());
                    if (record_flow_inspection_quiver) {
                        // This runs after the direct solver recorder returns,
                        // so headless numerical timestamps and oracle command
                        // streams remain solver-only.
                        record_fluid_25d_flow_inspection_quiver_step(commands.command_buffer(),
                                                                     resources_, config_.simulation,
                                                                     quiver_reset_requested_);
                    }
                    commands.submit_and_wait();
                    record_marker_timings(profile_recorder, frame.frame_slot.index, false, true);
                    if (profiler != nullptr) {
                        profiler->collect(frame.frame_slot.index);
                        record_gpu_timings(profile_recorder, frame_index,
                                           resources_.latest_timings());
                    }
                },
        }));
        record_headless_profile_diagnostics_if_requested(gpu, profile_recorder, frame_index);
        if (profile_recorder != nullptr &&
            config_.simulation.scenario == Fluid25DScenario::HillsideFlowStudy) {
            profile_recorder->record_metric(
                frame_index, "fluid_25d.supply", "step_start_seconds",
                static_cast<double>(source_schedule_.completed_steps()) *
                    config_.simulation.fixed_delta_seconds);
            profile_recorder->record_metric(frame_index, "fluid_25d.supply", "source_m3_per_s",
                                            last_source_m3_per_s_);
            profile_recorder->record_metric(frame_index, "fluid_25d.supply", "scheduled_volume_m3",
                                            scheduled_source_volume_m3_);
        }
        if (oracle_.has_value() || finite_volume_oracle_.has_value()) {
            const Fluid25DStepLedger step_ledger =
                oracle_.has_value() ? oracle_->step(source_rate_scale)
                                    : finite_volume_oracle_->step_with_dye(source_rate_scale).water;
            expected_source_volume_m3_ += step_ledger.source_volume_m3;
            expected_sink_volume_m3_ += step_ledger.sink_volume_m3;
            expected_boundary_outflow_volume_m3_ += step_ledger.boundary_outflow_volume_m3;
            if (finite_volume_oracle_.has_value()) {
                const Fluid25DTracerStepLedger& tracer =
                    finite_volume_oracle_->last_tracer_step_ledger();
                expected_tracer_source_amount_m3_ += tracer.source_amount_m3;
                expected_tracer_sink_amount_m3_ += tracer.sink_amount_m3;
                expected_tracer_boundary_outflow_amount_m3_ += tracer.boundary_outflow_amount_m3;
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
        if (config_.simulation.scenario == Fluid25DScenario::HillsideFlowStudy) {
            // Observation only: locate the intermediate moving footprint and
            // deepest downstream collection. Nothing feeds back to forcing.
            const double sx = static_cast<double>(scenario_.source_cell % scenario_.width);
            const double sz = static_cast<double>(scenario_.source_cell / scenario_.width);
            double sum = 0.0, weighted_x = 0.0, weighted_z = 0.0, deepest = 0.0;
            std::size_t collection_cell = scenario_.source_cell;
            for (std::size_t cell = 0U; cell < cells; ++cell) {
                if (depth_m[cell] <= 0.01F)
                    continue;
                const double x = static_cast<double>(cell % scenario_.width);
                const double z = static_cast<double>(cell / scenario_.width);
                const double distance = std::hypot(x - sx, z - sz) * config_.simulation.cell_size_m;
                if (distance >= 600.0 && distance < 2000.0) {
                    sum += depth_m[cell];
                    weighted_x += depth_m[cell] * x;
                    weighted_z += depth_m[cell] * z;
                }
                if (distance >= 2000.0 && depth_m[cell] > deepest) {
                    deepest = depth_m[cell];
                    collection_cell = cell;
                }
            }
            const auto observation = [&](const char* name, double value) {
                profile_recorder->record_metric(frame_index, "fluid_25d.hillside_observation", name,
                                                value);
            };
            observation("travel_cell_x", sum > 0.0 ? weighted_x / sum : sx);
            observation("travel_cell_z", sum > 0.0 ? weighted_z / sum : sz);
            observation("collection_cell_x",
                        static_cast<double>(collection_cell % scenario_.width));
            observation("collection_cell_z",
                        static_cast<double>(collection_cell / scenario_.width));
            observation("collection_depth_m", deepest);
        }
        const std::vector<Fluid25DLedgerGpu> ledger = readback_values<Fluid25DLedgerGpu>(
            gpu, resources_.ledger(), cells, "profile diagnostic ledger");
        const cubey::vulkan::Buffer& current_tracer_q =
            resources_.current_depth_is_a() ? resources_.tracer_q_a() : resources_.tracer_q_b();
        const std::vector<float> tracer_q_m =
            readback_values<float>(gpu, current_tracer_q, cells, "profile diagnostic tracer q");
        const std::vector<Fluid25DTracerLedgerGpu> tracer_ledger =
            readback_values<Fluid25DTracerLedgerGpu>(gpu, resources_.tracer_ledger(), cells,
                                                     "profile diagnostic tracer ledger");
        if (config_.simulation.solver == Fluid25DSolver::FiniteVolume) {
            const std::uint32_t status_flags = read_finite_volume_status(gpu, "profile diagnostic");
            profile_recorder->record_metric(frame_index, "fluid_25d.solver",
                                            "finite_volume_status_flags",
                                            static_cast<double>(status_flags));
            // Diagnostic-only identity of the complete hydraulic state. Dye
            // fields are deliberately excluded so pulse/no-pulse replays can
            // prove that the transport experiment did not change hydraulics.
            const auto momentum = readback_values<Fluid25DMomentumGpu>(
                gpu,
                resources_.current_depth_is_a() ? resources_.momentum_a() : resources_.momentum_b(),
                cells, "profile hydraulic identity momentum");
            const auto residual = readback_values<Fluid25DConservationResidualGpu>(
                gpu, resources_.finite_volume_conservation_residual(), cells,
                "profile hydraulic identity residual");
            std::uint64_t hash = 14695981039346656037ULL;
            const auto append = [&](float value) {
                const auto bits = std::bit_cast<std::uint32_t>(value);
                for (unsigned shift = 0; shift < 32; shift += 8) {
                    hash ^= (bits >> shift) & 0xffU;
                    hash *= 1099511628211ULL;
                }
            };
            for (std::size_t i = 0; i < cells; ++i) {
                append(depth_m[i]);
                for (float value : momentum[i].momentum_xy_reserved) {
                    append(value);
                }
                for (float value : velocity[i].velocity_wet) {
                    append(value);
                }
                for (float value : ledger[i].source_sink_boundary_reserved_m3) {
                    append(value);
                }
                append(residual[i].depth_tracer_reserved[0]);
                for (float value : residual[i].water_ledger) {
                    append(value);
                }
            }
            profile_recorder->record_metric(frame_index, "fluid_25d.hydraulic_identity",
                                            "hash_hi_u32", static_cast<double>(hash >> 32));
            profile_recorder->record_metric(frame_index, "fluid_25d.hydraulic_identity",
                                            "hash_lo_u32",
                                            static_cast<double>(hash & 0xffffffffULL));
        }
        const Fluid25DProfileDiagnostics diagnostics = compute_fluid_25d_profile_diagnostics(
            config_.simulation, depth_m, velocity, ledger, initial_water_volume_m3_);
        record_fluid_25d_profile_diagnostics(*profile_recorder, frame_index, diagnostics);
        if (config_.simulation.scenario == Fluid25DScenario::HillsideFlowStudy) {
            record_fluid_25d_hillside_progress(
                *profile_recorder, frame_index,
                compute_fluid_25d_hillside_progress(config_.simulation, scenario_, depth_m));
            record_fluid_25d_hillside_spatial(*profile_recorder, frame_index,
                                              compute_fluid_25d_hillside_spatial(config_.simulation,
                                                                                 scenario_, depth_m,
                                                                                 velocity));
            if (config_.motion_markers) {
                const auto markers = readback_values<Fluid25DMotionMarkerGpu>(
                    gpu, motion_markers_.buffer(), kFluid25DMotionMarkerCount,
                    "profile motion marker state");
                const float sx = static_cast<float>(scenario_.source_cell % scenario_.width);
                const float sy = static_cast<float>(scenario_.source_cell / scenario_.width);
                double active = 0.0, far = 0.0, total_distance = 0.0, max_age = 0.0;
                std::uint64_t hash = 14695981039346656037ULL;
                const auto append = [&](float value) {
                    if (!std::isfinite(value))
                        throw std::runtime_error("nonfinite motion marker state");
                    const auto bits = std::bit_cast<std::uint32_t>(value);
                    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                        hash ^= (bits >> shift) & 0xffU;
                        hash *= 1099511628211ULL;
                    }
                };
                for (const auto& marker : markers) {
                    for (float value : marker.position_age_active)
                        append(value);
                    for (float value : marker.previous_xy_reserved)
                        append(value);
                    for (const auto& point : marker.history)
                        for (float value : point)
                            append(value);
                    const auto& p = marker.position_age_active;
                    if (p[3] != 0.0F && p[3] != 1.0F)
                        throw std::runtime_error("invalid motion marker active flag");
                    if (p[3] < 0.5F)
                        continue;
                    const int x = static_cast<int>(std::floor(p[0] + 0.5F));
                    const int y = static_cast<int>(std::floor(p[1] + 0.5F));
                    if (x < 0 || y < 0 || x >= static_cast<int>(scenario_.width) ||
                        y >= static_cast<int>(scenario_.height) ||
                        depth_m[static_cast<std::size_t>(y) * scenario_.width +
                                static_cast<std::size_t>(x)] <=
                            config_.simulation.minimum_wet_depth_m)
                        throw std::runtime_error("motion marker lacks wet hydraulic support");
                    const double distance =
                        std::hypot(p[0] - sx, p[1] - sy) * config_.simulation.cell_size_m;
                    ++active;
                    far = std::max(far, distance);
                    total_distance += distance;
                    max_age = std::max(max_age, static_cast<double>(p[2]));
                }
                const auto metric = [&](const char* name, double value) {
                    profile_recorder->record_metric(frame_index, "fluid_25d.motion_markers", name,
                                                    value);
                };
                metric("active_count", active);
                metric("farthest_from_source_m", far);
                metric("mean_from_source_m", active > 0.0 ? total_distance / active : 0.0);
                metric("maximum_age_s", max_age);
                metric("hash_hi_u32", static_cast<double>(hash >> 32U));
                metric("hash_lo_u32", static_cast<double>(hash & 0xffffffffULL));
            }
        }
        if ((config_.simulation.scenario == Fluid25DScenario::TerrainCase &&
             config_.simulation.terrain_water_protocol ==
                 Fluid25DTerrainWaterProtocol::RainPulse) ||
            config_.simulation.scenario == Fluid25DScenario::SustainedHeadwatersDemo ||
            fluid_25d_is_natural_terrain_study(config_.simulation.scenario)) {
            record_fluid_25d_boundary_outflow_diagnostics(*profile_recorder, frame_index,
                                                          diagnostics);
        }
        if (config_.simulation.scenario == Fluid25DScenario::NaturalFlowStudy) {
            if (!scenario_.natural_flow_study.has_value()) {
                throw std::runtime_error(
                    "fluid 2.5D natural-flow-study profile has no validated recipe metadata");
            }
            const Fluid25DNaturalFlowStudyMetadata& study = scenario_.natural_flow_study.value();
            const auto gauges = compute_fluid_25d_natural_flow_gauge_diagnostics(
                config_.simulation, study, depth_m, velocity, tracer_q_m);
            record_fluid_25d_natural_flow_gauge_diagnostics(*profile_recorder, frame_index, gauges);
            const Fluid25DNaturalFlowBoundaryLedgerDiagnostics boundary =
                compute_fluid_25d_natural_flow_boundary_ledger_diagnostics(
                    config_.simulation, study, ledger,
                    diagnostics.cumulative_boundary_outflow_volume_m3);
            record_fluid_25d_natural_flow_boundary_ledger_diagnostics(*profile_recorder,
                                                                      frame_index, boundary);
        }
        if (config_.simulation.scenario == Fluid25DScenario::SustainedHeadwatersDemo) {
            const auto stations = compute_fluid_25d_sustained_headwaters_stations(
                config_.simulation, depth_m, velocity);
            record_fluid_25d_sustained_headwaters_stations(*profile_recorder, frame_index,
                                                           stations);
            const auto cross_section_stations =
                fluid_25d_sustained_headwaters_cross_section_stations(config_.simulation);
            for (const Fluid25DSustainedHeadwatersCrossSectionStation& station :
                 cross_section_stations) {
                const Fluid25DSustainedHeadwatersCrossSectionDiagnostics section =
                    compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                        config_.simulation, station, scenario_.terrain_height_m, depth_m, velocity);
                record_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                    *profile_recorder, frame_index, section);
            }
            const Fluid25DSustainedHeadwatersCorridorDiagnostics corridor =
                compute_fluid_25d_sustained_headwaters_corridor_diagnostics(config_.simulation,
                                                                            depth_m);
            record_fluid_25d_sustained_headwaters_corridor_diagnostics(*profile_recorder,
                                                                       frame_index, corridor);
        }
        const Fluid25DTracerProfileDiagnostics tracer_diagnostics =
            compute_fluid_25d_tracer_profile_diagnostics(config_.simulation, depth_m, tracer_q_m,
                                                         scenario_.sink_depth_rate_m_per_s,
                                                         tracer_ledger);
        record_fluid_25d_tracer_profile_diagnostics(*profile_recorder, frame_index,
                                                    tracer_diagnostics);
        if (config_.simulation.mass_audit) {
            const auto audit_cells = readback_values<Fluid25DMassAuditGpu>(
                gpu, resources_.finite_volume_mass_audit(), cells, "committed mass audit");
            const auto water_audit = compute_fluid_25d_mass_audit(
                audit_cells, diagnostics.total_water_volume_m3, initial_water_volume_m3_,
                diagnostics.cumulative_source_volume_m3, diagnostics.cumulative_sink_volume_m3,
                diagnostics.cumulative_boundary_outflow_volume_m3);
            const auto tracer_audit = compute_fluid_25d_mass_audit(
                audit_cells, tracer_diagnostics.total_tracer_amount_m3, 0.0,
                tracer_diagnostics.cumulative_source_amount_m3,
                tracer_diagnostics.cumulative_sink_amount_m3,
                tracer_diagnostics.cumulative_boundary_outflow_amount_m3, true);
            const auto record_audit = [&](const Fluid25DMassAuditDiagnostics& audit,
                                          std::string_view category) {
                constexpr std::array<std::string_view, 9> terms{
                    "independent_source_m3",   "independent_sink_m3",
                    "independent_boundary_m3", "actual_source_addition_m3",
                    "actual_sink_removal_m3",  "internal_transport_m3",
                    "boundary_transport_m3",   "update_arithmetic_m3",
                    "clamp_correction_m3"};
                for (std::size_t i = 0; i < terms.size(); ++i) {
                    profile_recorder->record_metric(frame_index, category, terms[i],
                                                    audit.totals_m3[i]);
                }
                profile_recorder->record_metric(frame_index, category, "field_budget_residual_m3",
                                                audit.field_budget_residual_m3);
                profile_recorder->record_metric(frame_index, category,
                                                "cumulative_ledger_rounding_m3",
                                                audit.cumulative_ledger_rounding_m3);
                profile_recorder->record_metric(frame_index, category,
                                                "source_representation_difference_m3",
                                                audit.source_representation_difference_m3);
                profile_recorder->record_metric(frame_index, category,
                                                "sink_representation_difference_m3",
                                                audit.sink_representation_difference_m3);
                profile_recorder->record_metric(frame_index, category,
                                                "boundary_definition_difference_m3",
                                                audit.boundary_definition_difference_m3);
                profile_recorder->record_metric(frame_index, category,
                                                "observed_conservation_residual_m3",
                                                audit.observed_conservation_residual_m3);
                profile_recorder->record_metric(frame_index, category, "committed_substeps",
                                                static_cast<double>(audit.committed_substeps));
            };
            record_audit(water_audit, "fluid_25d.mass_audit.water");
            record_audit(tracer_audit, "fluid_25d.mass_audit.tracer");
        }
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
            const Fluid25DSourceOutletSpatialDiagnostics spatial =
                compute_fluid_25d_source_outlet_spatial_diagnostics(
                    config_.simulation, scenario_.terrain_height_m, depth_m, tracer_q_m);
            record_fluid_25d_source_outlet_spatial_diagnostics(*profile_recorder, frame_index,
                                                               spatial);
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
        double actual_water_volume_m3 = 0.0;
        const double oracle_cell_area_m2 = static_cast<double>(config_.simulation.cell_size_m) *
                                           static_cast<double>(config_.simulation.cell_size_m);
        for (float h : actual_depth) {
            actual_water_volume_m3 += static_cast<double>(h) * oracle_cell_area_m2;
        }
        const double cpu_water_volume_m3 = finite_volume_oracle_->total_water_volume_m3();
        std::printf("fluid_25d_oracle_water_budget: gpu_stored=%.9f cpu_stored=%.9f "
                    "gpu_residual=%.9f cpu_residual=%.9f\n",
                    actual_water_volume_m3, cpu_water_volume_m3,
                    actual_water_volume_m3 - initial_water_volume_m3_ - actual_source_volume_m3 +
                        actual_sink_volume_m3 + actual_boundary_outflow_volume_m3,
                    cpu_water_volume_m3 - initial_water_volume_m3_ - expected_source_volume_m3_ +
                        expected_sink_volume_m3_ + expected_boundary_outflow_volume_m3_);
        const std::vector<Fluid25DMomentumGpu> actual_momentum =
            readback_values<Fluid25DMomentumGpu>(
                gpu,
                resources_.current_depth_is_a() ? resources_.momentum_a() : resources_.momentum_b(),
                cells, "oracle finite-volume momentum");
        const std::vector<float> actual_tracer_q = readback_values<float>(
            gpu,
            resources_.current_depth_is_a() ? resources_.tracer_q_a() : resources_.tracer_q_b(),
            cells, "oracle finite-volume tracer q");
        const std::vector<Fluid25DTracerLedgerGpu> actual_tracer_ledger =
            readback_values<Fluid25DTracerLedgerGpu>(gpu, resources_.tracer_ledger(), cells,
                                                     "oracle finite-volume tracer ledger");
        float maximum_tracer_q_error = 0.0F;
        float maximum_tracer_concentration_error = 0.0F;
        std::size_t maximum_velocity_error_cell = 0U;
        std::size_t maximum_velocity_error_component = 0U;
        float maximum_velocity_actual = 0.0F;
        float maximum_velocity_expected = 0.0F;
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
                const float actual_component = actual_velocity[index].velocity_wet[component];
                const float velocity_error = std::abs(actual_component - expected_velocity);
                if (velocity_error > maximum_velocity_error) {
                    maximum_velocity_error = velocity_error;
                    maximum_velocity_error_cell = index;
                    maximum_velocity_error_component = component;
                    maximum_velocity_actual = actual_component;
                    maximum_velocity_expected = expected_velocity;
                }
            }
            if (actual_momentum[index].momentum_xy_reserved[2] != 0.0F ||
                actual_momentum[index].momentum_xy_reserved[3] != 0.0F) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed nonzero finite-volume padding");
            }
            const float expected_wet = finite_volume_oracle_->wet_mask()[index] == 0U ? 0.0F : 1.0F;
            const float wet_error = std::abs(actual_velocity[index].velocity_wet[2] - expected_wet);
            if (wet_error > maximum_velocity_error) {
                maximum_velocity_error = wet_error;
                maximum_velocity_error_cell = index;
                maximum_velocity_error_component = 2U;
                maximum_velocity_actual = actual_velocity[index].velocity_wet[2];
                maximum_velocity_expected = expected_wet;
            }
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
                maximum_tracer_concentration_error =
                    std::max(maximum_tracer_concentration_error,
                             std::abs(actual_concentration - expected_concentration));
            }
            actual_tracer_amount_m3 += static_cast<double>(std::max(0.0F, actual_q)) * cell_area_m2;
            const Fluid25DTracerLedgerGpu& tracer_ledger = actual_tracer_ledger[index];
            for (std::size_t component_index = 0U;
                 component_index < tracer_ledger.source_sink_boundary_reserved_m3.size();
                 ++component_index) {
                const float component =
                    tracer_ledger.source_sink_boundary_reserved_m3[component_index];
                if (!finite(component)) {
                    throw std::runtime_error(
                        "fluid 2.5D GPU oracle observed nonfinite tracer ledger at cell=" +
                        std::to_string(index) + " component=" + std::to_string(component_index));
                }
            }
            if (tracer_ledger.source_sink_boundary_reserved_m3[0] < 0.0F ||
                tracer_ledger.source_sink_boundary_reserved_m3[1] < 0.0F ||
                tracer_ledger.source_sink_boundary_reserved_m3[2] < 0.0F ||
                tracer_ledger.source_sink_boundary_reserved_m3[3] != 0.0F) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed invalid tracer ledger at cell=" +
                    std::to_string(index) +
                    " values=" + std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[0]) +
                    "," + std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[1]) + "," +
                    std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[2]) + "," +
                    std::to_string(tracer_ledger.source_sink_boundary_reserved_m3[3]));
            }
            actual_tracer_source_amount_m3 += tracer_ledger.source_sink_boundary_reserved_m3[0];
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
            throw std::runtime_error(
                "fluid 2.5D finite-volume GPU oracle mismatch: status=" +
                std::to_string(status_flags) + " max_depth=" + std::to_string(maximum_depth_error) +
                " max_momentum=" + std::to_string(maximum_momentum_error) +
                " max_velocity=" + std::to_string(maximum_velocity_error) +
                " velocity_cell=" + std::to_string(maximum_velocity_error_cell) +
                " velocity_component=" + std::to_string(maximum_velocity_error_component) +
                " velocity_actual=" + std::to_string(maximum_velocity_actual) +
                " velocity_expected=" + std::to_string(maximum_velocity_expected) +
                " velocity_actual_depth=" +
                std::to_string(actual_depth[maximum_velocity_error_cell]) +
                " velocity_expected_depth=" +
                std::to_string(
                    finite_volume_oracle_->water_depth_m()[maximum_velocity_error_cell]) +
                " max_tracer_q=" + std::to_string(maximum_tracer_q_error) +
                " max_tracer_concentration=" + std::to_string(maximum_tracer_concentration_error) +
                " source_ledger=" + std::to_string(source_ledger_error) +
                " sink_ledger=" + std::to_string(sink_ledger_error) +
                " boundary_ledger=" + std::to_string(boundary_ledger_error) +
                " tracer_amount=" + std::to_string(tracer_amount_error) +
                " tracer_source_ledger=" + std::to_string(tracer_source_ledger_error) +
                " tracer_sink_ledger=" + std::to_string(tracer_sink_ledger_error) +
                " tracer_boundary_ledger=" + std::to_string(tracer_boundary_ledger_error) +
                " tracer_conservation=" + std::to_string(tracer_conservation_residual_m3));
        }
        std::printf(
            "fluid_25d_gpu_oracle: PASS solver=finite-volume scenario=%s status=%u "
            "max_depth=%.7f max_momentum=%.7f max_velocity=%.7f max_tracer_q=%.7f "
            "max_tracer_concentration=%.7f tracer_amount=%.7f source_ledger=%.7f sink_ledger=%.7f "
            "boundary_ledger=%.7f tracer_source_ledger=%.7f tracer_sink_ledger=%.7f "
            "tracer_boundary_ledger=%.7f tracer_conservation=%.7f\n",
            fluid_25d_scenario_name(config_.simulation.scenario), status_flags, maximum_depth_error,
            maximum_momentum_error, maximum_velocity_error, maximum_tracer_q_error,
            maximum_tracer_concentration_error, tracer_amount_error, source_ledger_error,
            sink_ledger_error, boundary_ledger_error, tracer_source_ledger_error,
            tracer_sink_ledger_error, tracer_boundary_ledger_error,
            tracer_conservation_residual_m3);
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
            if (config_.motion_markers)
                motion_markers_.create_render_pipeline(context.device(), target.format,
                                                       VK_FORMAT_D32_SFLOAT, target.extent);
            if (config_.motion_marker_gpu_controls)
                validate_fluid_25d_motion_marker_gpu_controls(context.device(), runtime_.gpu());
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
                presentation_cue_reset_requested_, quiver_reset_requested_, nullptr, 0U, {},
                config_.catchment_render,
                config_.motion_markers && show_motion_markers_ ? &motion_markers_ : nullptr, 1.0F);
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
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext& context) {
            for (std::uint32_t slot = 0U; slot < motion_markers_.profile_slot_count(); ++slot)
                record_marker_timings(context.profile_recorder(), slot, true, false);
            graph_executor_.clear();
            motion_markers_.destroy();
            resources_.destroy_all_resources();
            runtime_.detach_gpu_if_attached();
        };
        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    Fluid25DProjectConfig config_;
    Fluid25DWindowedPacing windowed_pacing_;
    Fluid25DInspectionAdvance inspection_advance_;
    Fluid25DScenarioData scenario_;
    cubey::ProjectRuntimeAdapter runtime_{1};
    Fluid25DGpuResources resources_;
    Fluid25DMotionMarkers motion_markers_;
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
    Fluid25DHillsideSupply hillside_supply_;
    float last_source_m3_per_s_ = 0.0F;
    double scheduled_source_volume_m3_ = 0.0;
    double expected_source_volume_m3_ = 0.0;
    double expected_sink_volume_m3_ = 0.0;
    double expected_boundary_outflow_volume_m3_ = 0.0;
    double expected_tracer_source_amount_m3_ = 0.0;
    double expected_tracer_sink_amount_m3_ = 0.0;
    double expected_tracer_boundary_outflow_amount_m3_ = 0.0;
    double initial_water_volume_m3_ = 0.0;
    bool paused_ = false;
    bool resume_after_advance_ = false;
    bool show_motion_markers_ = false;
    Fluid25DMotionMarkerDisplayClock marker_display_clock_;
    double continuous_wall_seconds_ = 0.0;
    double continuous_physical_seconds_ = 0.0;
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
