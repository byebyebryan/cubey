#include "fluid_25d_builtin_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::builtin_detail {

void Fluid25DApp::create_global_resources_if_needed(cubey::vulkan::Device& device,
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

void Fluid25DApp::record_marker_timings(cubey::profiling::ProfileRecorder* recorder,
                                        std::uint32_t slot, bool draw_only, bool update_only) {
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

void Fluid25DApp::record_windowed_frame(cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& render_frame) {
    refresh_builtin_boundary(runtime_.gpu(), false);
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
        rain_study_.reset();
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
    const bool producer_available =
        !stopped_ && backend_metadata_.session.lifecycle != Fluid25DLifecycle::Failed;
    const bool advancing_for_inspection =
        producer_available && inspection_advance_.remaining_steps() > 0U;
    const Fluid25DWindowedPacingFrame pacing =
        advancing_for_inspection ? Fluid25DWindowedPacingFrame{}
                                 : windowed_pacing_.advance(render_frame.timing.delta_seconds,
                                                            paused_ || !producer_available);
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
    Fluid25DRainStudyControl next_rain_study = rain_study_;
    std::vector<Fluid25DStepForcing> forcings;
    forcings.reserve(fixed_step_count);
    for (std::uint32_t step = 0U; step < fixed_step_count; ++step) {
        forcings.push_back({
            .source_rate_scale =
                source_rate_scale_for_step(next_source_schedule, next_supply, next_rain_study),
            .dye_source_concentration =
                next_dye_source_schedule.source_concentration(config_.simulation),
        });
        next_source_schedule.advance_fixed_step();
        next_dye_source_schedule.advance_fixed_step();
    }
    // The windowed graph records every pending fixed step and then draws
    // from the resulting state in one command buffer. The cue update is
    // recorded after each outer step, never once per presented frame.
    const bool recorded_reset = reset_requested_;
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
    backend_reset_in_flight_ = backend_reset_in_flight_ || recorded_reset;
    source_schedule_ = std::move(next_source_schedule);
    dye_source_schedule_ = std::move(next_dye_source_schedule);
    hillside_supply_ = std::move(next_supply);
    rain_study_ = std::move(next_rain_study);
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
        metric("confirmed_gpu_physical_time_s", backend_metadata_.session.physical_time_s);
        metric("backend_generation",
               static_cast<double>(backend_metadata_.session.reset_generation));
        metric("backend_progress_readback_bytes", sizeof(Fluid25DBackendProgressGpu));
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
        if (config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy) {
            metric("rain_enabled", rain_study_.enabled() ? 1.0 : 0.0);
            metric("rain_applied_rate_mm_per_hour",
                   rain_study_.applied_rate_mm_per_hour(config_.simulation));
            metric("rain_cumulative_depth_m", rain_study_.state().cumulative_depth_m);
            metric("rain_scheduled_volume_m3", rain_study_.state().scheduled_volume_m3);
        }
    }
}

void Fluid25DApp::record_headless_simulation_frame(
    cubey::ProjectGpuServices& gpu, const cubey::host::HeadlessCaptureFrame& frame,
    cubey::profiling::ProfileRecorder* profile_recorder) {
    const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
    const std::uint64_t frame_index = profile_frame_index(project_frame);
    if (reset_requested_) {
        source_schedule_.reset();
        dye_source_schedule_.reset();
        hillside_supply_.reset();
        rain_study_.reset();
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
        source_rate_scale_for_step(source_schedule_, hillside_supply_, rain_study_);
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

} // namespace cubey::projects::fluid::fluid_25d::builtin_detail
