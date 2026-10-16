#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

void RecordingApp::draw_external_rain_ui(bool busy) {
    ImGui::SeparatorText("Rainfall - uniform across the map");
    ImGui::Text("Active supply: %.3f mm/h (%s)", external_snapshot_->rain_m_per_s * 3.6e6,
                external_snapshot_->rain_m_per_s > 0.0 ? "ON" : "OFF");
    if (backend_metadata_.capabilities.solver.set_rain) {
        ImGui::BeginDisabled(busy);
        if (ImGui::BeginCombo("Rain preset", "Choose")) {
            for (const int rate : {0, 128, 256, 512, 1024}) {
                const auto label =
                    rate == 0 ? std::string("Off") : std::to_string(rate) + " mm/h";
                if (ImGui::Selectable(label.c_str()))
                    send_external_command(Fluid25DCommandKind::SetRain, double(rate) / 3.6e6);
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        // Typing is local, not a solver command. Keep edits available across
        // paused heartbeat phases; Apply alone requires healthy authority.
        // A matching applied acknowledgement refreshes the input. Do not
        // allow a newer draft to be overwritten while a command is in flight.
        ImGui::BeginDisabled(external_session_->command_pending() || managed_held_);
        ImGui::SetNextItemWidth(140.0F);
        if (ImGui::InputFloat("Intensity (mm/h)", &external_rain_input_mm_per_hour_, 0.1F, 1.0F,
                              "%.3f"))
            external_rain_dirty_ = true;
        ImGui::EndDisabled();
        ImGui::BeginDisabled(busy || !external_rain_dirty_ ||
                             !std::isfinite(external_rain_input_mm_per_hour_) ||
                             external_rain_input_mm_per_hour_ < 0.0F ||
                             external_rain_input_mm_per_hour_ > 36000.0F);
        if (ImGui::Button("Apply rain"))
            send_external_command(Fluid25DCommandKind::SetRain,
                                  static_cast<double>(external_rain_input_mm_per_hour_) /
                                      3.6e6);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted(external_session_->command_pending()
                                   ? "Command pending acknowledgement"
                               : external_rain_dirty_ ? "Typed value not applied"
                                                      : "Showing acknowledged supply");
    } else {
        ImGui::TextDisabled("This backend does not support changing rainfall.");
    }
    if (busy)
        ImGui::TextDisabled("Apply unavailable while busy, unhealthy or terminal; pending "
                            "commands also lock typing.");
    ImGui::TextWrapped(
        "Set zero to stop new rain; existing water still flows and drains. "
        "Intensity is depth added per hour, not retained water or playback speed.");
    ImGui::TextDisabled(
        "128-1024 mm/h presets are exaggerated demo rainfall, not typical weather.");
    ImGui::Separator();
}

void RecordingApp::draw_terrain_selection_ui(ManagedSceneReload& demo) {
    auto* control = demo.control();
    if (!control)
        return;
    const auto& current = control->case_by_id(demo.current_case_id());
    ImGui::SeparatorText("Terrain and simulation grid");
    ImGui::Text("Current: %s | %u x %u | %.0f m cells", current.site_label.c_str(),
                current.width, current.height, current.spacing_m);
    const auto& selected = control->case_by_id(demo.selected_case_id);
    ImGui::BeginDisabled(control->busy());
    if (ImGui::BeginCombo("Site", selected.site_label.c_str())) {
        for (const auto& entry : control->cases()) {
            if (entry.configuration != selected.configuration)
                continue;
            ImGui::BeginDisabled(!entry.available);
            if (ImGui::Selectable(entry.site_label.c_str(), selected.site == entry.site))
                demo.selected_case_id = entry.id;
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    const auto& edited = control->case_by_id(demo.selected_case_id);
    if (ImGui::BeginCombo("Grid / area", edited.label.c_str())) {
        for (const bool same_area : {true, false}) {
            ImGui::SeparatorText(same_area ? "Same area: 16.384 km square"
                                           : "Advanced: different coverage");
            for (const auto& entry : control->cases()) {
                if (entry.site != edited.site || (entry.extent == "same") != same_area)
                    continue;
                ImGui::BeginDisabled(!entry.available);
                const std::string label =
                    entry.label + " | " + std::to_string(entry.width) + " | " +
                    std::to_string(static_cast<int>(entry.spacing_m)) + " m";
                if (ImGui::Selectable(label.c_str(), entry.id == edited.id))
                    demo.selected_case_id = entry.id;
                if (!entry.available &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", entry.error.c_str());
                ImGui::EndDisabled();
            }
        }
        ImGui::EndCombo();
    }
    const auto& pending = control->case_by_id(demo.selected_case_id);
    ImGui::Text("Selected: %u x %u | %.0f m | %.3f km square", pending.width, pending.height,
                pending.spacing_m, double(pending.width) * pending.spacing_m / 1000);
    ImGui::TextDisabled(
        "Source terrain: %.0f m. Finer sim grids interpolate, not add terrain detail.",
        pending.source_spacing_m);
    if (pending.width >= 2048)
        ImGui::TextWrapped("Fine/large grids run the solver substantially slower; 60x is a "
                           "request, not a guarantee.");
    ImGui::BeginDisabled(!pending.available || pending.id == current.id ||
                         !demo.restart_allowed());
    if (ImGui::Button("Apply & restart"))
        demo.request_case(pending.id);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard selection"))
        demo.selected_case_id = current.id;
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "Clears water/time; keeps rainfall, pace and appearance. Starts dry and paused.");
    if (control->busy())
        ImGui::TextWrapped("LOADING: %s. Previous scene held; no old/new field mixing.",
                           demo.stage().empty() ? control->status().phase.c_str()
                                                : demo.stage().c_str());
    if (!control->status().error.empty()) {
        ImGui::TextWrapped("Load failed: %s", control->status().error.c_str());
        ImGui::BeginDisabled(!demo.restart_allowed());
        if (ImGui::Button("Retry selected"))
            demo.request_case(demo.selected_case_id);
        ImGui::SameLine();
        if (ImGui::Button("Restart previous case (dry)"))
            demo.request_case(current.id);
        ImGui::EndDisabled();
    }
}

void RecordingApp::draw_external_ui(ManagedSceneReload* demo) {
    ImGui::SetNextWindowSize(
        ImVec2(450.0F, std::min(790.0F, ImGui::GetIO().DisplaySize.y - 32.0F)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Terrain Water")) {
        ImGui::End();
        return;
    }
    const auto lifecycle = external_snapshot_->header.lifecycle;
    const auto health = external_health();
    const bool terminal = lifecycle == Fluid25DLifecycle::Completed ||
                          lifecycle == Fluid25DLifecycle::Failed ||
                          lifecycle == Fluid25DLifecycle::Stopped;
    const bool busy = managed_held_ || external_session_->command_pending() ||
                      !external_error_.empty() || terminal ||
                      health != fluid_25d_project_config_detail::ExternalSessionHealth::Healthy;
    auto* control = demo ? demo->control() : nullptr;
    const bool loading = control && control->busy();
    const char* state = loading         ? "LOADING"
                        : managed_held_ ? "HELD - NOT LIVE"
                                        : lifecycle_name(lifecycle);
    ImGui::TextColored(ImVec4(.25F, .85F, 1.F, 1.F), "LIVE SynxFlow | %s", state);
    if (control) {
        const auto& current = control->case_by_id(demo->current_case_id());
        ImGui::Text("%s | %u x %u | %.0f m cells", current.site_label.c_str(), current.width,
                    current.height, current.spacing_m);
        ImGui::Text("Simulation patch %.3f km square",
                    current.width * current.spacing_m / 1000);
        ImGui::Text("Simulated: %s / %s", fluid_25d_demo_time_label(shown_->time_s).c_str(),
                    fluid_25d_demo_time_label(current.duration_s).c_str());
    } else {
        ImGui::Text("%u x %u | %.0f m cells | simulated %s", simulation_.grid_width,
                    simulation_.grid_height, simulation_.cell_size_m,
                    fluid_25d_demo_time_label(shown_->time_s).c_str());
    }
    const bool guide_site =
        control && fluid_25d_rain_guide_site(control->case_by_id(demo->current_case_id()));
    if (guide_site) {
        ImGui::BeginDisabled(!demo->guide_available() && !demo->guide_active());
        if (ImGui::RadioButton("Guided rain response", demo->guide_preferred()))
            demo->set_guide_preferred(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::RadioButton("Manual", !demo->guide_preferred()))
            demo->set_guide_preferred(false);
    }
    const bool guided_start = demo && demo->guided_start_selected();
    ImGui::BeginDisabled(busy || (guided_start && !demo->guide_available()));
    if ((lifecycle == Fluid25DLifecycle::Ready || lifecycle == Fluid25DLifecycle::Paused) &&
        backend_metadata_.capabilities.solver.resume &&
        ImGui::Button(guided_start          ? "Start demo [Space]"
                      : shown_->time_s == 0 ? "Start [Space]"
                                            : "Resume [Space]")) {
        if (demo)
            demo->start_or_resume();
        else
            send_external_command(Fluid25DCommandKind::Resume);
    }
    if (lifecycle == Fluid25DLifecycle::Running &&
        backend_metadata_.capabilities.solver.pause && ImGui::Button("Pause [Space]"))
        send_external_command(Fluid25DCommandKind::Pause);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(control ? !demo->restart_allowed()
                                 : busy || !backend_metadata_.capabilities.solver.reset);
    if (ImGui::Button("Reset water [R]")) {
        if (control)
            demo->request_case(demo->current_case_id());
        else if (backend_metadata_.capabilities.solver.reset)
            send_external_command(Fluid25DCommandKind::Reset);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Fit simulation patch")) {
        select_camera("overview");
    }
    if (loading)
        ImGui::TextWrapped("Loading: %s. Previous scene and motion held.",
                           demo->stage().empty() ? control->status().phase.c_str()
                                                 : demo->stage().c_str());
    else if (managed_held_)
        ImGui::TextWrapped("Last scene held. No running producer is attached; retry or restart "
                           "in Scene setup.");
    else if (lifecycle == Fluid25DLifecycle::Completed)
        ImGui::TextWrapped("Simulation horizon reached. The final water state is held; Reset "
                           "water starts a new run.");
    else if ((lifecycle == Fluid25DLifecycle::Paused ||
              lifecycle == Fluid25DLifecycle::Ready) &&
             shown_->time_s == 0)
        ImGui::TextWrapped(
            guided_start ? "Dry and paused. Start demo follows rain, runoff and drainage."
            : external_snapshot_->rain_m_per_s == 0
                ? "Dry and paused with rain off. Choose a rain preset, then Start."
                : "Dry and paused. Start supplies the selected rainfall across the terrain.");
    if (external_session_->command_pending())
        ImGui::TextDisabled("Applying solver command...");
    if (!external_error_.empty())
        ImGui::TextWrapped("Service/viewer error: %s", external_error_.c_str());
    if (control && !control->status().error.empty())
        ImGui::TextWrapped("Selection/restart error: %s", control->status().error.c_str());
    if (!loading && !managed_held_ &&
        (health == fluid_25d_project_config_detail::ExternalSessionHealth::Delayed ||
         health == fluid_25d_project_config_detail::ExternalSessionHealth::Disconnected))
        ImGui::TextWrapped(
            "Published field frozen; producer heartbeat is delayed/disconnected.");
    if (shown_->stats.water_volume_m3 >= 1.0e6)
        ImGui::Text("Water %.2f million m3 | max depth %.2f m",
                    shown_->stats.water_volume_m3 / 1.0e6, shown_->stats.max_depth_m);
    else
        ImGui::Text("Water %.0f m3 | max depth %.2f m", shown_->stats.water_volume_m3,
                    shown_->stats.max_depth_m);

    if (guide_site) {
        ImGui::SeparatorText("Rain-response demonstration");
        if (demo->guide_active()) {
            ImGui::TextWrapped(
                "%s",
                lifecycle == Fluid25DLifecycle::Running
                    ? fluid_25d_rain_guide_caption(shown_->time_s,
                                                   external_snapshot_->rain_m_per_s > 0)
                    : "Preparing acknowledged rain/pace settings, then starting the solver.");
            if (ImGui::Button("Leave guided demonstration"))
                demo->manual_control(
                    "Guide stopped. Rain/pace stay as acknowledged; solver continues.");
        } else {
            if (!demo->guide_note().empty())
                ImGui::TextWrapped("%s", demo->guide_note().c_str());
            else
                ImGui::TextWrapped(
                    "Guided Start: 512 mm/h at 60x, rain off after 2 simulated hours, "
                    "drainage through 6 hours. About 6 wall minutes. Manual keeps your "
                    "rain/pace and camera choices, with no scheduled rain shutoff.");
            if (!demo->guide_available() && !demo->guide_finished())
                ImGui::TextDisabled(
                    "Reset water to dry/paused first; wait for a healthy worker.");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(
            "Physical-time milestones; manual solver/camera input exits the guide.");
        ImGui::TextWrapped(
            "End of guide does not pause the simulation. Rain/foam are visual cues.");
        ImGui::PopStyleColor();
    }

    if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_external_rain_ui(busy);
        ImGui::Text("Requested pace: %.1fx", external_snapshot_->pacing);
        if (const auto achieved = pace_meter_.achieved())
            ImGui::Text("Achieved: %.1fx (recent simulated seconds / wall seconds)", *achieved);
        else
            ImGui::TextDisabled("Achieved pace: paused / collecting a running window");
        if (backend_metadata_.capabilities.solver.set_time_scale) {
            ImGui::BeginDisabled(busy);
            if (ImGui::BeginCombo("Pace preset", "Choose")) {
                for (const int pace : {1, 10, 30, 60, 120}) {
                    const auto label = std::to_string(pace) + "x";
                    if (ImGui::Selectable(label.c_str()))
                        send_external_command(Fluid25DCommandKind::SetTimeScale, pace);
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputFloat("Custom pace (x)", &external_time_scale_input_, .125F, 1.F,
                                  "%.2f"))
                external_time_scale_dirty_ = true;
            ImGui::BeginDisabled(
                !external_time_scale_dirty_ || !std::isfinite(external_time_scale_input_) ||
                external_time_scale_input_ < .125F || external_time_scale_input_ > 300);
            if (ImGui::Button("Apply pace"))
                send_external_command(Fluid25DCommandKind::SetTimeScale,
                                      external_time_scale_input_);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (external_time_scale_dirty_)
                ImGui::TextDisabled("Custom pace edit is not applied yet.");
        }
        ImGui::TextWrapped("Pace accelerates simulation time, not rain intensity or FPS. Fine "
                           "grids may not keep up.");
        if ((external_rain_dirty_ || external_time_scale_dirty_) &&
            !external_session_->command_pending() && ImGui::Button("Discard control edits")) {
            external_rain_input_mm_per_hour_ = float(external_snapshot_->rain_m_per_s * 3.6e6);
            external_time_scale_input_ = float(external_snapshot_->pacing);
            external_rain_dirty_ = external_time_scale_dirty_ = false;
        }
    }

    if (control && ImGui::CollapsingHeader("Scene setup")) {
        draw_terrain_selection_ui(*demo);
        ImGui::TextDisabled("Closing this managed demo stops only its owned worker.");
    }
    if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
        int reading = static_cast<int>(catchment_view_);
        if (ImGui::Combo("3D view", &reading,
                         "Scenic composite\0Water isolation\0Flow inspection\0")) {
            catchment_view_ = static_cast<Fluid25DCatchmentView>(reading);
            view_ = Fluid25DPresentationView::Catchment;
            quiver_reset_ = true;
        }
        bool diagnostic = view_ == Fluid25DPresentationView::Diagnostics;
        if (ImGui::Checkbox("2D diagnostic map [A]", &diagnostic))
            view_ = diagnostic ? Fluid25DPresentationView::Diagnostics
                               : Fluid25DPresentationView::Catchment;
        if (diagnostic) {
            int map = static_cast<int>(debug_view_);
            if (ImGui::Combo("Map [D]", &map,
                             "Terrain\0Depth\0Surface\0Speed\0Direction\0Wet/dry\0"))
                debug_view_ = static_cast<Fluid25DDebugView>(map);
            draw_native_color_legend();
        }
        // Existing runoff/collection observation points were authored for
        // the mountain crop. Do not pretend they identify another site's basin.
        if (!control || control->case_by_id(demo->current_case_id()).site == "mountain") {
            for (const char* camera : {"overview", "runoff", "collection"}) {
                const char* label = camera == std::string_view("overview") ? "Whole patch"
                                    : camera == std::string_view("runoff") ? "Downhill runoff"
                                                                           : "Basin collection";
                if (ImGui::RadioButton(label, config_.recording_camera == camera)) {
                    select_camera(camera);
                }
            }
        }
        ImGui::BeginDisabled(!markers_available_);
        if (ImGui::Checkbox("Dots / trails [W]", &config_.motion_markers))
            marker_reset_ = true;
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "Dots/trails and foam illustrate motion; they are not native particles.");
        if (ImGui::CollapsingHeader("Appearance - render only"))
            draw_native_presentation_ui();
    }
    if (ImGui::CollapsingHeader("How to read this")) {
        ImGui::TextWrapped(
            "Only the full-resolution simulation patch is rendered. Rain supplies that "
            "rectangle. Water follows "
            "slopes, collects in depressions and "
            "can leave through the open perimeter. There is no authored source/drain here.");
        ImGui::TextWrapped(
            "Rain off stops new supply; existing water still flows. Small streams can appear "
            "before large pools. Visible rain density/fall speed only affect rendering.");
        ImGui::TextWrapped("Live runs the solver. Replay elsewhere only displays saved fields. "
                           "A healthy paused heartbeat does not mean water is advancing.");
    }
    if (ImGui::CollapsingHeader("Diagnostics / advanced")) {
        ImGui::Text("Backend %s | %s", backend_metadata_.id.c_str(),
                    backend_profile_name(backend_metadata_.profile));
        ImGui::Text("Heartbeat %s | field age %.3f s", external_health_name(health),
                    external_publication_age_s());
        ImGui::Text(
            "Generation %llu | sequence %llu",
            static_cast<unsigned long long>(external_snapshot_->header.reset_generation),
            static_cast<unsigned long long>(external_snapshot_->header.sequence));
        ImGui::Text("Session %.16s", backend_metadata_.session.session_id.c_str());
        ImGui::Text("Input %.12s | bed %.12s", backend_metadata_.grid.input_sha256.c_str(),
                    backend_metadata_.grid.solver_bed_sha256.c_str());
        ImGui::Text("Max speed %.3f m/s", shown_->stats.max_speed_m_per_s);
        if (!external_session_->last_command_message().empty())
            ImGui::TextWrapped("Last acknowledgement: %s",
                               external_session_->last_command_message().c_str());
        if (external_snapshot_->acknowledgement)
            ImGui::Text("Command %llu: %s",
                        static_cast<unsigned long long>(
                            external_snapshot_->acknowledgement->command_id),
                        acknowledgement_name(external_snapshot_->acknowledgement->state));
        ImGui::BeginDisabled(busy);
        if (backend_metadata_.capabilities.solver.step && ImGui::Button("Step solver"))
            send_external_command(Fluid25DCommandKind::Step);
        if (backend_metadata_.capabilities.solver.stop && ImGui::Button("Stop producer"))
            send_external_command(Fluid25DCommandKind::Stop);
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "No native ledger/tracer claim. Viewer performs no hydraulic solve.");
        ImGui::Text("Renderer cap %u Hz (0 = unpaced)", config_.common.render_fps);
        host::draw_performance_ui(
            {.frame_stats = frame_stats_snapshot_,
             .process = process_stats_,
             .device_memory_budget = device_memory_budget_,
             .gpu_timings = resources_.latest_timings(),
             .config = {.label = "Renderer performance", .default_open = false}});
        if (!control)
            ImGui::TextWrapped("Standalone viewer: Esc closes/detaches; the launcher owns the "
                               "producer. Closing alone does not stop it.");
    }
    ImGui::End();
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail
