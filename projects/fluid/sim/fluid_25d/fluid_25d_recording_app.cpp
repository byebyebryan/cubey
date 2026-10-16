#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

int RecordingApp::run() {
    const int status = config_.common.headless ? run_headless() : run_windowed();
    if (shutdown_failure_)
        std::rethrow_exception(shutdown_failure_);
    return stream_error_.empty() && external_error_.empty() && comparison_error_.empty()
               ? status
               : 1;
}

int RecordingApp::run_windowed() {
    ManagedSceneReload reload(*this);
    host::WindowedAppCallbacks callbacks;
    callbacks.create_global_resources = [&reload](host::WindowedAppContext& context) {
        reload.scene().create_resources(context.device(), context.gpu(),
                                        context.frame_slot_count());
    };
    callbacks.create_swapchain_resources = [&reload](host::WindowedAppContext& context) {
        auto& app = reload.scene();
        app.create_render_resources(
            context.device(), render::ColorTargetView{.extent = context.swapchain().extent(),
                                                      .format = context.swapchain().format()});
        app.graph_.clear();
        app.graph_.resize(context.frame_slot_count());
    };
    callbacks.destroy_swapchain_resources = [&reload](host::WindowedAppContext&) {
        auto& app = reload.scene();
        app.graph_.clear();
        app.scenic_.destroy_swapchain_resources();
        app.markers_.destroy_render_pipeline();
        app.resources_.destroy_swapchain_resources();
    };
    callbacks.draw_ui = [&reload](host::WindowedAppContext&) {
        reload.scene().draw_ui(&reload);
    };
    callbacks.update = [&reload](host::WindowedAppContext& context, const FrameTiming& timing) {
        reload.poll(
            context.device(), context.gpu(), context.frame_slot_count(),
            {.extent = context.swapchain().extent(), .format = context.swapchain().format()},
            [&] {
                for (std::uint32_t slot = 0; slot < context.frame_slot_count(); ++slot) {
                    context.frame_resources().wait_for_frame(slot);
                    context.gpu().mark_submission_completed(
                        context.frame_resources().submitted_ticket(slot));
                }
            });
        reload.scene().update_windowed(context, timing, &reload);
    };
    callbacks.record_frame = [&reload](host::WindowedAppContext& context,
                                       const host::WindowedRenderFrame& frame) {
        const vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        reload.scene().record(context.device(), frame.command_buffer, frame.color_target,
                              frame.frame_slot, Fluid25DRenderTargetMode::Present,
                              context.profile_recorder(), frame.timing.frame_index);
        recorder.end("terrain water window");
    };
    callbacks.shutdown = [&reload](host::WindowedAppContext& context) {
        reload.shutdown(context.gpu());
    };
    return host::run_windowed_app(
        {.run_config = config_.common,
         .app_name = "fluid_25d",
         .ready_status =
             external_session_ ? "attached to external fluid service (draw-only viewer)"
             : config_.stream_path
                 ? "rendering live external SynxFlow snapshots (no hydraulic solve)"
                 : "rendering recorded SynxFlow mountain runoff (no hydraulic solve)",
         .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
         .close_on_escape = true},
        std::move(callbacks));
}

void RecordingApp::update_windowed(host::WindowedAppContext& context, const FrameTiming& timing,
                                   ManagedSceneReload* reload) {
    if (auto sample =
            frame_stats_.record_frame({.delta_seconds = timing.delta_seconds,
                                       .width = context.swapchain().extent().width,
                                       .height = context.swapchain().extent().height})) {
        frame_stats_snapshot_ = *sample;
        process_stats_ = process_sampler_.sample();
        device_memory_budget_ = context.device().device_memory_budget();
    }
    const auto input = context.filtered_input();
    if (reload->guide_active() &&
        (input.scroll_delta().y != 0 ||
         (input.mouse_button_down(input::MouseButton::Left) &&
          (input.cursor_delta().x != 0 || input.cursor_delta().y != 0))))
        reload->manual_control("Manual orbit/zoom; guide stopped. Solver continues.");
    orbit_.update_pointer_input(input, timing.delta_seconds);
    if (input.key_pressed(input::Key::S))
        cycle_bank_view();
    if (input.key_pressed(input::Key::W) && markers_available_)
        config_.motion_markers = !config_.motion_markers;
    if (input.key_pressed(input::Key::V))
        select_native_style(render_.native_presentation == 3U ? 2U : 3U);
    if (external_session_) {
        const auto lifecycle = external_snapshot_->header.lifecycle;
        const bool terminal = lifecycle == Fluid25DLifecycle::Completed ||
                              lifecycle == Fluid25DLifecycle::Failed ||
                              lifecycle == Fluid25DLifecycle::Stopped;
        const bool controls_available = !terminal && external_controls_allowed();
        if (input.key_pressed(input::Key::Space)) {
            if (controls_available && lifecycle == Fluid25DLifecycle::Running &&
                fluid_25d_command_supported(backend_metadata_.capabilities,
                                            Fluid25DControlDomain::Solver,
                                            Fluid25DCommandKind::Pause))
                send_external_command(Fluid25DCommandKind::Pause);
            else if (controls_available &&
                     (lifecycle == Fluid25DLifecycle::Paused ||
                      lifecycle == Fluid25DLifecycle::Ready) &&
                     fluid_25d_command_supported(backend_metadata_.capabilities,
                                                 Fluid25DControlDomain::Solver,
                                                 Fluid25DCommandKind::Resume))
                reload->start_or_resume();
        }
        if (reload->control() && reload->restart_allowed() && input.key_pressed(input::Key::R))
            reload->request_case(reload->current_case_id());
        else if (controls_available && input.key_pressed(input::Key::R) &&
                 fluid_25d_command_supported(backend_metadata_.capabilities,
                                             Fluid25DControlDomain::Solver,
                                             Fluid25DCommandKind::Reset))
            send_external_command(Fluid25DCommandKind::Reset);
    } else {
        if (input.key_pressed(input::Key::Space)) {
            if (config_.stream_path)
                live_playing_ = !live_playing_;
            else
                toggle_playback();
        }
        if (input.key_pressed(input::Key::R)) {
            restart_playback();
        }
    }
    if (input.key_pressed(input::Key::A)) {
        view_ = view_ == Fluid25DPresentationView::Diagnostics
                    ? Fluid25DPresentationView::Catchment
                    : Fluid25DPresentationView::Diagnostics;
        quiver_reset_ = true;
    }
    if (input.key_pressed(input::Key::D))
        debug_view_ = static_cast<Fluid25DDebugView>((static_cast<int>(debug_view_) + 1) % 6);
    update_view(timing.delta_seconds);
}

int RecordingApp::run_headless() {
    ManagedSceneReload reload(*this);
    host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [&reload](host::HeadlessPngContext& context) {
        reload.scene().create_resources(
            context.device(), context.gpu(),
            host::headless_capture_frame_slot_count(context.config()));
        reload.scene().create_render_resources(context.device(), context.render_target());
    };
    callbacks.before_frame = [&reload](host::HeadlessPngContext& context,
                                       const host::HeadlessCaptureFrame& frame) {
        reload.poll(context.device(), context.gpu(),
                    host::headless_capture_frame_slot_count(context.config()),
                    context.render_target(),
                    [&] { context.gpu().wait_queue_idle("terrain scene replacement"); });
        if (context.config().headless_render_only) {
            auto span =
                context.profile_recorder()
                    ? context.profile_recorder()->cpu_span(frame.index, "fluid25d.update_view")
                    : profiling::ScopedCpuProfileSpan{};
            reload.scene().update_view(frame.timing.delta_seconds);
        }
    };
    callbacks.record_frame =
        [&reload](host::HeadlessPngContext& context, const host::HeadlessCaptureFrame& frame,
                  VkCommandBuffer commands, const host::HeadlessRenderTarget& target) {
            reload.scene().record_headless_frame(context, frame, commands, target);
        };
    callbacks.shutdown = [&reload](host::HeadlessPngContext& context) {
        reload.shutdown(context.gpu());
    };
    host::HeadlessPngHost host(
        {.run_config = config_.common,
         .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
        std::move(callbacks));
    return host.run();
}

void RecordingApp::record_headless_frame(host::HeadlessPngContext& context,
                                         const host::HeadlessCaptureFrame& frame,
                                         VkCommandBuffer commands,
                                         const host::HeadlessRenderTarget& target) {
    if (!config_.common.headless_render_only && weather_ && frame.index > 0U)
        weather_->advance(1.0 / double(config_.common.fps));
    if (config_.common.headless_render_only) {
        record(context.device(), commands, target, frame.frame_slot,
               Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(),
               frame.index);
        return;
    }
    if (external_session_) {
        visual_delta_ = 0.0;
        rain_clock_.capture(frame.index, config_.common.fps,
                            config_.common.capture_mode == CaptureMode::Video &&
                                external_snapshot_->header.lifecycle ==
                                    Fluid25DLifecycle::Running,
                            config_.native_rain_speed);
        record(context.device(), commands, target, frame.frame_slot,
               Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(),
               frame.index);
        std::printf(
            "fluid_25d_external_capture: output_frame=%u physical_s=%.9f "
            "generation=%llu sequence=%llu camera=%s hydraulic_dispatches=0\n",
            frame.index, shown_->time_s,
            static_cast<unsigned long long>(external_snapshot_->header.reset_generation),
            static_cast<unsigned long long>(external_snapshot_->header.sequence),
            config_.recording_camera.c_str());
        log_rain_capture(frame.index);
        return;
    }
    // Load by immutable capture index on the GPU-owner callback. The
    // video producer may already be preparing later output frames.
    const bool video = config_.common.capture_mode == CaptureMode::Video;
    const bool editorial = config_.recording_opening_frames || config_.recording_closing_frames;
    const auto source_frame = [&](std::uint32_t index) {
        return editorial
                   ? fluid_25d_project_config_detail::recording_source_frame(
                         index, host::headless_capture_frame_count(config_.common),
                         config_.recording_opening_frames, config_.recording_closing_frames)
                   : index;
    };
    const double unbounded = static_cast<double>(config_.recording_time_seconds) +
                             (video ? static_cast<double>(source_frame(frame.index)) *
                                          config_.recording_frame_interval_seconds
                                    : 0.0);
    const double time =
        comparison_ ? unbounded : std::min(recording_->times_s().back(), unbounded);
    const auto bounded =
        comparison_ ? comparison_->map(time, comparison_loop_) : Fluid25DBankWindowTime{time};
    const double presented = bounded.time_s;
    const double previous = shown_->time_s;
    double previous_requested = frame.index == 0U
                                    ? time
                                    : static_cast<double>(config_.recording_time_seconds) +
                                          static_cast<double>(source_frame(frame.index - 1U)) *
                                              config_.recording_frame_interval_seconds;
    if (comparison_)
        previous_requested = comparison_->map(previous_requested, comparison_loop_).time_s;
    else
        previous_requested = std::min(recording_->times_s().back(), previous_requested);
    if (frame.index > 0U && presented < previous_requested)
        seek_playback(presented);
    else
        clock_.seek(presented);
    comparison_at_end_ = bounded.at_end;
    const auto index = clock_.frame_index();
    if (index != shown_index_) {
        accept_frame(recording_->frame(index));
        shown_index_ = index;
    }
    if (config_.bank_comparison_cycle_frames != 0U) {
        const auto mode = static_cast<Fluid25DBankView>(
            (frame.index / config_.bank_comparison_cycle_frames) % 3U);
        if (mode != bank_view_)
            set_bank_view(mode);
    }
    // A sparse recording can jump far in physical time. Bound only
    // the optional approximate visual history, never the saved fields.
    visual_delta_ = !video || presented < previous_requested ? 0.0
                    : render_.native_presentation != 0U
                        ? fluid_25d_recorded_visual_delta(previous_requested, presented,
                                                          recording_->times_s().back())
                        : std::clamp(shown_->time_s - previous, 0.0, 60.0);
    // Optional editorial captures have a wall-time cosmetic clock even
    // when endpoint hydraulic fields are held. Ordinary replay is unchanged.
    if (editorial)
        visual_delta_ = frame.index == 0U ? 0.0 : 1.0 / double(config_.common.fps);
    const double captured_visual_delta = visual_delta_;
    if (editorial || frame.index == 0U || presented > previous_requested)
        rain_clock_.capture(frame.index, config_.common.fps, video, config_.native_rain_speed);
    record(context.device(), commands, target, frame.frame_slot,
           Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(), frame.index);
    std::printf("fluid_25d_recording_capture: output_frame=%u requested_s=%.9f "
                "saved_s=%.9f camera=%s hydraulic_dispatches=0 visual_delta_s=%.9f "
                "presentation=%s\n",
                frame.index, presented, shown_->time_s, config_.recording_camera.c_str(),
                captured_visual_delta, config_.native_presentation.c_str());
    log_rain_capture(frame.index);
    if (comparison_)
        std::printf(
            "fluid_25d_bank_capture: output_frame=%u mode=%s requested_unbounded_s=%.9f "
            "presented_s=%.9f saved_s=%.9f end=%u looped=%u cache_hit=%u\n",
            frame.index, fluid_25d_bank_view_name(bank_view_), time, presented, shown_->time_s,
            bounded.at_end ? 1U : 0U, bounded.looped ? 1U : 0U, coverage_cache_ ? 1U : 0U);
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail

namespace cubey::projects::fluid::fluid_25d {
using recording_detail::RecordingApp;

int run_fluid_25d_recording(const Fluid25DProjectConfig& config) {
    RecordingApp app(config);
    return app.run();
}
} // namespace cubey::projects::fluid::fluid_25d
