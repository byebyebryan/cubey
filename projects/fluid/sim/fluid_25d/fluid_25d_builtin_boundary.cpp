#include "fluid_25d_builtin_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::builtin_detail {

void Fluid25DApp::refresh_builtin_boundary(cubey::ProjectGpuServices& gpu, bool force) {
    const auto now = std::chrono::steady_clock::now();
    const auto& previous = local_session_->metadata().session;
    const bool pause_changed = paused_ != backend_applied_paused_;
    const bool scale_changed = config_.presentation_time_scale != backend_applied_time_scale_;
    const bool stop_changed = stopped_ && previous.lifecycle != Fluid25DLifecycle::Stopped;
    if (!force && !backend_reset_in_flight_ && !pause_changed && !scale_changed &&
        !stop_changed && now < next_backend_status_poll_)
        return;
    next_backend_status_poll_ = now + std::chrono::milliseconds(100);
    // This submission/fence reads only sixteen diagnostic bytes after all
    // preceding graphics-queue work. No hydraulic field is downloaded.
    const auto progress =
        readback_values<Fluid25DBackendProgressGpu>(gpu, resources_.backend_progress(), 1U,
                                                    "builtin backend completed progress")
            .front();
    if (progress.reserved != 0U)
        throw std::runtime_error("builtin backend progress padding is nonzero");
    const std::uint64_t accepted =
        static_cast<std::uint64_t>(progress.accepted_substeps_low) |
        (static_cast<std::uint64_t>(progress.accepted_substeps_high) << 32U);
    const std::uint64_t scheduled =
        source_schedule_.completed_steps() * config_.simulation.simulation_substeps;
    if (accepted > scheduled || (progress.flags == 0U && accepted != scheduled))
        throw std::runtime_error("builtin completed substeps disagree with submitted work");
    const auto acknowledge = [&](Fluid25DCommandKind kind, double time_s,
                                 Fluid25DLifecycle lifecycle,
                                 std::optional<double> value = {}) {
        const auto ack = local_session_->apply(Fluid25DControlDomain::Solver, kind, time_s,
                                               lifecycle, value);
        std::printf("fluid_25d_backend_ack: %s\n",
                    encode_fluid_25d_command_acknowledgement_json(ack).c_str());
    };
    if (backend_reset_in_flight_) {
        acknowledge(Fluid25DCommandKind::Reset, 0.0, Fluid25DLifecycle::Ready);
        backend_reset_in_flight_ = false;
        backend_applied_paused_ = paused_;
        backend_applied_time_scale_ = config_.presentation_time_scale;
    }
    const double time_s =
        static_cast<double>(accepted) *
        static_cast<double>(config_.simulation.fixed_delta_seconds /
                            static_cast<float>(config_.simulation.simulation_substeps));
    if (local_session_->metadata().session.lifecycle == Fluid25DLifecycle::Failed ||
        local_session_->metadata().session.lifecycle == Fluid25DLifecycle::Stopped)
        return;
    const auto lifecycle = paused_ ? Fluid25DLifecycle::Paused : Fluid25DLifecycle::Running;
    // Publish the accepted physical clock first. A rejected FV candidate
    // does not increment the observer and cannot masquerade as advancement.
    const auto frame = local_session_->publish(time_s, lifecycle);
    if (progress.flags != 0U) {
        local_session_->fail("native finite-volume update rejected, flags=" +
                             std::to_string(progress.flags));
        paused_ = true;
        inspection_advance_.reset();
        resume_after_advance_ = false;
    } else {
        if (pause_changed) {
            acknowledge(paused_ ? Fluid25DCommandKind::Pause : Fluid25DCommandKind::Resume,
                        time_s, lifecycle);
            backend_applied_paused_ = paused_;
        }
        if (scale_changed) {
            acknowledge(Fluid25DCommandKind::SetTimeScale, time_s, lifecycle,
                        config_.presentation_time_scale);
            backend_applied_time_scale_ = config_.presentation_time_scale;
        }
        if (stop_changed) {
            acknowledge(Fluid25DCommandKind::Stop, time_s, Fluid25DLifecycle::Stopped);
            paused_ = true;
        }
    }
    backend_metadata_ = local_session_->metadata();
    std::printf("fluid_25d_backend_frame: %s\n",
                encode_fluid_25d_frame_header_json(frame).c_str());
    if (progress.flags != 0U)
        std::printf("fluid_25d_backend_runtime_metadata: %s\n",
                    encode_fluid_25d_backend_metadata_json(backend_metadata_).c_str());
}

float Fluid25DApp::source_rate_scale_for_step(const Fluid25DSourceRateSchedule& clock,
                                              Fluid25DHillsideSupply& supply,
                                              Fluid25DRainStudyControl& rain_study) {
    const float legacy_scale = clock.source_rate_scale(config_.simulation);
    if (config_.hillside_supply_gpu_controls) {
        constexpr std::array<double, 4> control_times{3598.0, 3600.0, 5400.0, 7200.0};
        return Fluid25DHillsideSupply(true).rate(control_times.at(clock.completed_steps()),
                                                 100.0F) /
               100.0F;
    }
    if (config_.simulation.scenario != Fluid25DScenario::HillsideFlowStudy &&
        config_.simulation.scenario != Fluid25DScenario::HillsideRainStudy)
        return legacy_scale;
    if (config_.simulation.scenario == Fluid25DScenario::HillsideRainStudy) {
        if (config_.rain_study_gpu_controls) {
            if (rain_study.state().completed_steps == 1U)
                rain_study.queue_enabled(false);
            if (rain_study.state().completed_steps == 2U)
                rain_study.queue_enabled(true);
        }
        const float scale = rain_study.prepare_fixed_step(config_.simulation);
        last_source_m3_per_s_ =
            static_cast<float>(rain_study.total_input_m3_per_s(config_.simulation));
        return legacy_scale * scale;
    }
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

} // namespace cubey::projects::fluid::fluid_25d::builtin_detail
