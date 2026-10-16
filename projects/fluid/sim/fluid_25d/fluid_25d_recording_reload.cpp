#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

ManagedSceneReload::ManagedSceneReload(RecordingApp& initial) : active_(&initial) {
    initial.managed_demo_ = this;
    if (!initial.config_.terrain_demo_control_path)
        return;
    control_ =
        std::make_unique<Fluid25DTerrainDemoControl>(*initial.config_.terrain_demo_control_path);
    current_case_id_ = selected_case_id = control_->initial_case_id();
    const auto& entry = control_->case_by_id(current_case_id_);
    const auto& metadata = initial.backend_metadata_;
    if (metadata.session.session_id != control_->status().session_id ||
        metadata.grid.width != entry.width || metadata.grid.height != entry.height ||
        metadata.grid.spacing_m != entry.spacing_m ||
        metadata.grid.input_sha256 != entry.input_sha256 ||
        metadata.grid.solver_bed_sha256 != entry.solver_bed_sha256 ||
        !initial.config_.native_scenic_surface_grid ||
        *initial.config_.native_scenic_surface_grid != entry.surface_grid)
        throw std::runtime_error(
            "managed initial scene does not match the prepared catalogue/session");
    if (initial.config_.terrain_demo_script_path)
        script_ = fluid_25d_load_terrain_demo_script(*initial.config_.terrain_demo_script_path);
    log("initial");
}

ManagedSceneReload::~ManagedSceneReload() = default;

void ManagedSceneReload::log(const std::string& event) {
    if (control_)
        control_->event(event, current_case_id_, active_->backend_metadata_.session.session_id,
                        active_->shown_->time_s, active_->external_snapshot_->rain_m_per_s * 3.6e6,
                        active_->simulation_.grid_width, active_->simulation_.grid_height,
                        active_->backend_metadata_.grid.solver_bed_sha256);
}

bool ManagedSceneReload::restart_allowed() const {
    return control_ && fluid_25d_demo_restart_allowed(control_->busy(),
                                                      active_->external_session_->command_pending(),
                                                      control_->status().worker_available);
}

void ManagedSceneReload::request_case(const std::string& id) {
    if (!restart_allowed())
        return;
    cancel_guide("Scene reset/replacement; guide stopped.");
    guide_actions_.clear();
    guide_finished_ = false;
    guide_note_.clear();
    control_->request(id, active_->external_snapshot_->rain_m_per_s * 3.6e6,
                      active_->external_snapshot_->pacing);
    selected_case_id = id;
    active_->managed_held_ = true;
    active_->visual_delta_ = 0;
    stage_ = "Validating selected case";
    log("switch-request");
}

void ManagedSceneReload::run_script() {
    if (script_.empty() || !control_)
        return;
    if (control_->status().phase == "error")
        throw std::runtime_error("managed regression switch failed: " + control_->status().error);
    if (control_->busy() || active_->managed_held_ || active_->external_session_->command_pending())
        return;
    if (!active_->external_error_.empty())
        throw std::runtime_error("managed regression service failed: " + active_->external_error_);
    run_sequence(script_, action_index_, action_waiting_, action_started_, action_ack_id_, false);
}

void ManagedSceneReload::run_sequence(const std::vector<Fluid25DTerrainDemoAction>& actions,
                                      std::size_t& index, bool& waiting,
                                      std::chrono::steady_clock::time_point& started,
                                      std::optional<std::uint64_t>& expected_ack, bool guided) {
    if (control_->busy() || active_->managed_held_ ||
        active_->external_session_->command_pending() || !active_->external_error_.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (started == std::chrono::steady_clock::time_point{}) {
        started = now; // Cold setup time is not part of an action's delay.
        return;
    }
    if (active_->external_refresh_after_render_setup_)
        return; // Let update_view validate a post-setup publication first.
    if (waiting) {
        if (expected_ack) {
            const auto& ack = active_->external_snapshot_->acknowledgement;
            if (!ack || ack->command_id != *expected_ack ||
                ack->state != Fluid25DAcknowledgementState::Applied)
                throw std::runtime_error(
                    "scheduled solver command did not receive its applied acknowledgement");
            expected_ack.reset();
        }
        log(guided ? "guide-applied" : "action-applied");
        waiting = false;
        started = now;
    }
    if (index >= actions.size())
        return;
    const auto& action = actions[index];
    if (!fluid_25d_demo_action_due(action, std::chrono::duration<double>(now - started).count(),
                                   active_->shown_->time_s))
        return;
    if (action.kind == "start" || action.kind == "manual-start") {
        if (!active_->external_controls_allowed())
            return;
        if (action.kind == "manual-start")
            set_guide_preferred(false);
        const bool starts_guide = guided_start_selected();
        if (starts_guide && !guide_available())
            throw std::runtime_error("guided Start requires a healthy dry/paused session");
        const auto id = active_->external_next_command_id_;
        start_or_resume();
        if (!starts_guide) {
            if (active_->external_pending_command_id_ != id || !active_->external_error_.empty())
                throw std::runtime_error("manual Start was not submitted");
            expected_ack = id;
        }
    } else if (action.kind == "guide-start") {
        if (!guide_available())
            throw std::runtime_error(
                "rain guide requires the healthy dry/paused Mountain 1k/16m case");
        start_guide();
    } else if (action.kind == "guide-cancel") {
        cancel_guide("Guide explicitly stopped. Solver settings are retained.");
    } else if (action.kind == "camera") {
        if (control_->case_by_id(current_case_id_).site != "mountain")
            throw std::runtime_error("observation cameras require the mountain site");
        active_->select_camera(action.camera.c_str(), guided);
    } else if (action.kind == "switch") {
        cancel_guide("Scene replacement; guide stopped.");
        control_->request(action.case_id, action.rain_mm_per_hour, action.pacing);
        selected_case_id = action.case_id;
        active_->managed_held_ = true;
        stage_ = "Validating selected case";
    } else if (action.kind == "reset") {
        request_case(current_case_id_);
    } else {
        const auto kind = action.kind == "resume"     ? Fluid25DCommandKind::Resume
                          : action.kind == "pause"    ? Fluid25DCommandKind::Pause
                          : action.kind == "set-rain" ? Fluid25DCommandKind::SetRain
                                                      : Fluid25DCommandKind::SetTimeScale;
        auto value = action.value;
        if (action.kind == "set-rain")
            value = *value / 3.6e6;
        if (!active_->external_controls_allowed())
            return; // Health/pending transitions are observed, never bypassed.
        const auto id = active_->external_next_command_id_;
        active_->send_external_command(kind, value, guided);
        if (active_->external_pending_command_id_ != id || !active_->external_error_.empty())
            throw std::runtime_error("scheduled solver command was not submitted");
        expected_ack = id;
    }
    log(std::string(guided ? "guide-" : "action-") + action.kind);
    ++index;
    waiting = true;
}

bool ManagedSceneReload::guide_available() const {
    if (!control_ || !fluid_25d_rain_guide_site(control_->case_by_id(current_case_id_)) ||
        guide_active() || active_->shown_->time_s != 0 ||
        active_->shown_->stats.water_volume_m3 != 0 || !active_->external_controls_allowed())
        return false;
    const auto lifecycle = active_->external_snapshot_->header.lifecycle;
    const auto& capabilities = active_->backend_metadata_.capabilities.solver;
    return (lifecycle == Fluid25DLifecycle::Ready || lifecycle == Fluid25DLifecycle::Paused) &&
           capabilities.resume && capabilities.set_rain && capabilities.set_time_scale;
}

bool ManagedSceneReload::guided_start_selected() const {
    return control_ && fluid_25d_demo_guided_start(
                           guide_preferred_, control_->case_by_id(current_case_id_),
                           active_->shown_->time_s, active_->shown_->stats.water_volume_m3);
}

void ManagedSceneReload::set_guide_preferred(bool preferred) {
    guide_preferred_ = preferred;
    if (preferred)
        guide_note_.clear();
    else
        cancel_guide("Manual mode selected. Applied rain/pace and water are retained.");
}

void ManagedSceneReload::start_or_resume() {
    if (guided_start_selected())
        start_guide(); // An unavailable guide never silently falls back to manual Resume.
    else
        active_->send_external_command(Fluid25DCommandKind::Resume);
}

void ManagedSceneReload::manual_control(const char* reason) {
    guide_preferred_ = false;
    cancel_guide(reason);
}

void ManagedSceneReload::start_guide() {
    if (!guide_available())
        return;
    guide_preferred_ = true;
    guide_actions_ = fluid_25d_rain_guide_actions();
    guide_index_ = 0;
    guide_waiting_ = guide_finished_ = false;
    guide_action_started_ = {};
    guide_ack_id_.reset();
    guide_note_.clear();
    active_->select_camera("overview", true);
    log("guide-started");
}

void ManagedSceneReload::cancel_guide(const char* reason) {
    if (!guide_active())
        return;
    guide_actions_.clear();
    guide_waiting_ = false;
    guide_ack_id_.reset();
    guide_note_ = reason;
    log("guide-cancelled");
}

void ManagedSceneReload::run_guide() {
    if (!guide_active())
        return;
    const auto lifecycle = active_->external_snapshot_->header.lifecycle;
    if (!active_->external_error_.empty() || active_->managed_held_ || control_->busy() ||
        lifecycle == Fluid25DLifecycle::Completed || lifecycle == Fluid25DLifecycle::Stopped ||
        lifecycle == Fluid25DLifecycle::Failed) {
        cancel_guide("Guide stopped: producer is held, failed, replaced or at its horizon.");
        return;
    }
    try {
        run_sequence(guide_actions_, guide_index_, guide_waiting_, guide_action_started_,
                     guide_ack_id_, true);
    } catch (const std::exception& error) {
        cancel_guide(error.what());
        return;
    }
    if (guide_index_ == guide_actions_.size() && !guide_waiting_) {
        if (lifecycle != Fluid25DLifecycle::Running ||
            active_->external_snapshot_->rain_m_per_s != 0)
            throw std::runtime_error("guide ended without continued rain-off hydraulic evolution");
        guide_finished_ = true;
        guide_note_ = "Guide complete. The guide did not pause the solver; Reset water to repeat.";
        log("guide-complete-running");
    }
}

void ManagedSceneReload::poll(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots,
                              render::ColorTargetView target,
                              const std::function<void()>& wait_frames) {
    if (!control_)
        return;
    const auto now = std::chrono::steady_clock::now();
    bool status_changed = false;
    if (now >= next_poll_) {
        next_poll_ = now + std::chrono::milliseconds(100);
        if (control_->poll()) {
            status_changed = true;
            log("status-" + control_->status().phase);
        }
    }
    const auto& status = control_->status();
    active_->managed_held_ =
        control_->busy() ||
        (!status.worker_available && status.phase != "completed" && status.phase != "stopped") ||
        status.session_id != active_->backend_metadata_.session.session_id;
    if (status_changed && !control_->busy() && status.phase == "error" && status.worker_available &&
        status.session_id == active_->backend_metadata_.session.session_id) {
        // Failed preflight never stopped the current worker. Refresh its actual
        // publication rather than re-dating a held field after the loading pause.
        active_->external_refresh_after_render_setup_ = true;
    }
    if (status.phase == "ready" && status.request_id != preparing_request_id_ &&
        !prepared_.valid() && !uploaded_) {
        preparing_request_id_ = status.request_id;
        auto config = active_->config_;
        const auto& entry = control_->case_by_id(status.case_id);
        const auto& previous = control_->case_by_id(current_case_id_);
        config.external_session_path = control_->ready_session_directory();
        config.native_scenic_surface_grid = entry.surface_grid;
        if (entry.site != previous.site || entry.extent != previous.extent) {
            config.recording_camera = "overview";
            config.native_camera_target_x.reset();
            config.native_camera_target_z.reset();
            config.native_camera_span_m.reset();
        }
        const auto render = active_->render_;
        const auto material = active_->scenic_material_;
        const auto view = active_->view_;
        const auto reading = active_->catchment_view_;
        const auto debug = active_->debug_view_;
        const auto bank = active_->bank_view_;
        const auto expected_id = status.session_id;
        stage_ = "Loading terrain, climate and dry water fields";
        prepared_ = std::async(std::launch::async, [config, render, material, view, reading, debug,
                                                    bank, entry, expected_id] {
            auto replacement = std::make_unique<RecordingApp>(config);
            const auto& metadata = replacement->backend_metadata_;
            if (metadata.session.session_id != expected_id || metadata.grid.width != entry.width ||
                metadata.grid.height != entry.height ||
                metadata.grid.spacing_m != entry.spacing_m ||
                metadata.grid.input_sha256 != entry.input_sha256 ||
                metadata.grid.solver_bed_sha256 != entry.solver_bed_sha256 ||
                replacement->shown_->time_s != 0 || replacement->shown_->stats.max_depth_m != 0)
                throw std::runtime_error("replacement scene is not the selected dry session");
            replacement->render_ = render;
            replacement->scenic_material_ = material;
            // Pending GPU uploads must not mutate the active weather runtime.
            replacement->defer_weather_setup_ = true;
            replacement->view_ = view;
            replacement->catchment_view_ = reading;
            replacement->debug_view_ = debug;
            replacement->bank_view_ = bank;
            replacement->configure_camera();
            return replacement;
        });
    }
    bool retirement_started = false;
    try {
        if (prepared_.valid() &&
            prepared_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto replacement = prepared_.get();
            if (status.phase == "ready" && status.request_id == preparing_request_id_) {
                stage_ = "Preparing Vulkan scene resources";
                uploaded_.emplace(gpu.submit(
                    "terrain water replacement", [replacement = std::move(replacement), slots,
                                                  target](vulkan::GpuOwnerContext& owner) mutable {
                        try {
                            replacement->create_resources(owner.device(), owner.runtime(), slots);
                            replacement->create_render_resources(owner.device(), target);
                            return std::move(replacement);
                        } catch (...) {
                            replacement->shutdown();
                            throw;
                        }
                    }));
            }
        }
        if (uploaded_ && uploaded_->ready()) {
            auto upload = std::move(*uploaded_);
            uploaded_.reset();
            auto replacement = upload.get();
            wait_frames();
            if (status.phase != "ready" || status.request_id != preparing_request_id_) {
                replacement->shutdown();
            } else {
                const auto& previous = control_->case_by_id(current_case_id_);
                const auto& next = control_->case_by_id(status.case_id);
                if (previous.site == next.site && previous.extent == next.extent) {
                    replacement->camera_ = active_->camera_;
                    replacement->orbit_ = active_->orbit_;
                    replacement->target_ = active_->target_;
                }
                retirement_started = true;
                // All active frame submissions have retired; one controller
                // survives the swap, with no parallel weather descriptor writes.
                replacement->weather_ = std::move(active_->weather_);
                replacement->defer_weather_setup_ = false;
                active_->shutdown(); // Drains reader/whitewater jobs before their bed is released.
                if (active_->shutdown_failure_) {
                    replacement->shutdown();
                    std::rethrow_exception(active_->shutdown_failure_);
                }
                active_->external_session_.reset();
                active_->external_snapshot_.reset();
                active_->shown_.reset();
                active_->scenario_ = {};
                std::vector<Fluid25DVelocityGpu>().swap(active_->packed_velocity_);
                active_->whitewater_selection_ = {};
                active_->terrain_surface_.reset();
                installed_ = std::move(replacement);
                active_ = installed_.get();
                active_->managed_demo_ = this;
                current_case_id_ = selected_case_id = next.id;
                active_->managed_held_ = true;
                control_->confirm_installed(true);
                stage_ = "Scene installed; waiting for launcher acknowledgement";
                log("installed");
            }
        }
    } catch (const std::exception& error) {
        if (retirement_started)
            throw; // A failed numerical/upload audit is fatal, never a recoverable old scene.
        if (status.phase == "ready")
            control_->confirm_installed(false, error.what());
        stage_ = std::string("Replacement failed: ") + error.what();
        if (!script_.empty())
            throw;
    }
    if (!control_->busy())
        stage_.clear();
    run_script();
    run_guide();
    static_cast<void>(device); // GPU installation belongs to the shared runtime owner.
}

void ManagedSceneReload::shutdown(vulkan::GpuRuntime& gpu) {
    // Close during any loading stage still joins CPU work and releases unused
    // GPU products while the host device/runtime are alive.
    if (prepared_.valid()) {
        try {
            static_cast<void>(prepared_.get());
        } catch (...) {
        }
    }
    if (uploaded_) {
        try {
            auto unused = uploaded_->get();
            gpu.wait_queue_idle("unused terrain water replacement");
            unused->shutdown();
        } catch (...) {
        }
        uploaded_.reset();
    }
    active_->shutdown();
    if (active_->shutdown_failure_)
        std::rethrow_exception(active_->shutdown_failure_);
    if (!script_.empty() && (action_index_ != script_.size() || action_waiting_))
        throw std::runtime_error("managed regression ended before all actions were acknowledged");
    if (control_)
        log("viewer-closed");
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail
