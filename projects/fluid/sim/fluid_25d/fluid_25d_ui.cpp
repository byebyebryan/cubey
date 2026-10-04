#include "fluid_25d_ui.h"

#include "fluid_25d_backend_contract.h"
#include "fluid_25d_dye_palette.h"
#include "fluid_25d_forcing_cubes.h"

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <array>

namespace cubey::projects::fluid::fluid_25d {
namespace {

constexpr std::array<Fluid25DPresentationView, 2> kPresentationViews{
    Fluid25DPresentationView::Catchment,
    Fluid25DPresentationView::Diagnostics,
};

constexpr std::array<Fluid25DCatchmentView, 3> kCatchmentViews{
    Fluid25DCatchmentView::Composite,
    Fluid25DCatchmentView::WaterIsolation,
    Fluid25DCatchmentView::FlowInspection,
};

constexpr std::array<Fluid25DCatchmentView, 4> kTransportCatchmentViews{
    Fluid25DCatchmentView::Composite,
    Fluid25DCatchmentView::WaterIsolation,
    Fluid25DCatchmentView::FlowInspection,
    Fluid25DCatchmentView::TransportInspection,
};

constexpr std::array<Fluid25DDebugView, 6> kDebugViews{
    Fluid25DDebugView::Terrain,       Fluid25DDebugView::WaterDepth,
    Fluid25DDebugView::SurfaceHeight, Fluid25DDebugView::FlowMagnitude,
    Fluid25DDebugView::FlowDirection, Fluid25DDebugView::WetDry,
};

void request_reset(Fluid25DUiContext& ui) {
    ui.reset_requested = true;
    ui.presentation_cue_reset_requested = true;
    ui.quiver_reset_requested = true;
    ui.windowed_pacing.reset();
    ui.inspection_advance.reset();
}

} // namespace

void draw_fluid_25d_ui(Fluid25DUiContext ui) {
    const bool terminal = ui.stopped || ui.backend_failed;
    if (terminal) {
        ui.inspection_advance.reset();
        ui.windowed_pacing.reset();
        ui.resume_after_advance = false;
    }
    if (!cubey::host::begin_control_panel(ui.title, {.width = 360.0F})) {
        ImGui::End();
        return;
    }

    ImGui::Text("Backend: built-in | %s", fluid_25d_solver_name(ui.solver));
    ImGui::TextDisabled("GPU-resident simulation; controls below change this solver.");
    if (ui.backend_metadata && ImGui::CollapsingHeader("Backend details")) {
        const auto& backend = *ui.backend_metadata;
        ImGui::TextWrapped("Adapter: %s", backend.id.c_str());
        ImGui::Text("Input: %.12s | solver bed: %.12s", backend.grid.input_sha256.c_str(),
                    backend.grid.solver_bed_sha256.c_str());
        ImGui::Text("Generation %llu | confirmed GPU time %.6f s",
                    static_cast<unsigned long long>(backend.session.reset_generation),
                    backend.session.physical_time_s);
        ImGui::TextDisabled(
            "16-byte completion status only; hydraulic fields remain GPU-resident.");
        if (!backend.session.failure_message.empty())
            ImGui::TextWrapped("Solver failed: %s", backend.session.failure_message.c_str());
        ImGui::TextUnformatted(backend.fields.momentum_x_m2_per_s
                                   ? "Native momentum: h*u (m2/s)"
                                   : "Native directed face discharge (m3/s)");
        ImGui::Text("Water ledger: %s | tracer: %s",
                    backend.fields.water_ledger ? "available" : "unavailable",
                    backend.fields.tracer_depth_equivalent_m ? "available" : "unavailable");
        ImGui::TextDisabled("No generic rain-rate or single-step command adapter yet.");
    }

    cubey::host::imgui_enum_combo(
        "Presentation", ui.presentation_view, kPresentationViews, fluid_25d_presentation_view_name,
        "Choose the oblique catchment surface or the top-down diagnostics surface.");
    if (ui.presentation_view == Fluid25DPresentationView::Catchment) {
        if (ui.transport_inspection_available) {
            cubey::host::imgui_enum_combo(
                "Catchment mode", ui.catchment_view, kTransportCatchmentViews,
                fluid_25d_catchment_view_name,
                "Composite is the normal 3D view; the other modes are reading aids.");
        } else {
            // A dye-free or unrelated scene would make this mode look like a
            // broken, empty layer. Do not offer it unless its conservative
            // source/outlet transport contract is actually present.
            if (ui.catchment_view == Fluid25DCatchmentView::TransportInspection) {
                ui.catchment_view = Fluid25DCatchmentView::Composite;
            }
            cubey::host::imgui_enum_combo(
                "Catchment mode", ui.catchment_view, kCatchmentViews, fluid_25d_catchment_view_name,
                "Composite is the normal 3D view; the other modes are reading aids.");
            ImGui::TextDisabled(
                "Transport Inspection needs a finite-volume dye pulse on an eligible demo.");
        }
    }
    if (ui.presentation_view == Fluid25DPresentationView::Diagnostics) {
        cubey::host::imgui_enum_combo("Diagnostic field", ui.debug_view, kDebugViews,
                                      fluid_25d_debug_view_name,
                                      "Top-down numerical field shown by diagnostics.");
    }

    ImGui::BeginDisabled(ui.inspection_advance.remaining_steps() > 0U || ui.stopped ||
                         ui.backend_failed);
    if (cubey::host::imgui_button(ui.paused ? "Resume" : "Pause",
                                  "Pause or resume windowed simulation time.")) {
        ui.paused = !ui.paused;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (cubey::host::imgui_button("Reset", "Restart the deterministic scenario.")) {
        ui.stopped = false;
        request_reset(ui);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(ui.stopped || ui.backend_failed);
    if (ImGui::Button("Stop")) {
        ui.stopped = true;
        ui.paused = true;
        ui.inspection_advance.reset();
        ui.windowed_pacing.reset();
        ui.resume_after_advance = false;
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(terminal);
    if (cubey::host::imgui_slider_float(
            "Playback speed", &ui.presentation_time_scale,
            kFluid25DMinWindowedPresentationTimeScale, kFluid25DMaxWindowedPresentationTimeScale,
            "%.3gx",
            "Windowed pacing only; solver fixed delta and headless timing are unchanged.")) {
        ui.windowed_pacing.set_presentation_time_scale(ui.presentation_time_scale);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("How to read it");
    if (ui.scenario == Fluid25DScenario::HillsideFlowStudy) {
        ImGui::Text("Physical time: %.1f min (%.0f s)", ui.simulation_elapsed_seconds / 60.0F,
                    ui.simulation_elapsed_seconds);
        ImGui::Text("Last step input: %.1f m3/s | no drain", ui.continuous_source_m3_per_s);
        ImGui::TextDisabled(ui.hillside_supply.manual() ? "Manual supply holds until Reset."
                            : ui.hillside_supply.response()
                                ? "Script: Q100 / 150 / 50 / 100 at 0 / 60 / 90 / 120 min."
                                : "Reference: constant supply.");
        ImGui::BeginDisabled(terminal);
        for (float rate : {50.0F, 100.0F, 150.0F}) {
            if (rate != 50.0F)
                ImGui::SameLine();
            const char* label = rate == 50.0F    ? "Low: 50"
                                : rate == 100.0F ? "Base: 100"
                                                 : "High: 150";
            if (ImGui::Button(label))
                ui.hillside_supply.queue_preset(rate);
        }
        ImGui::EndDisabled();
        if (ui.hillside_supply.pending())
            ImGui::Text("Queued %.0f m3/s for next fixed step.", *ui.hillside_supply.pending());
        ImGui::Text("Playback: %.3gx | %s", ui.presentation_time_scale,
                    ui.inspection_advance.remaining_steps() > 0U ? "inspection advance"
                    : ui.paused                                  ? "paused"
                                                                 : "continuous");
        if (ui.inspection_advance.remaining_steps() > 0U) {
            ImGui::Text("Computing advance: %.1f s remaining",
                        static_cast<double>(ui.inspection_advance.remaining_steps()) *
                            ui.fixed_delta_seconds);
            ImGui::ProgressBar(ui.inspection_advance.progress());
            ImGui::TextDisabled(ui.resume_after_advance ? "Continuous playback resumes when ready."
                                                        : "Pauses for inspection when ready.");
            if (ImGui::Button("Cancel advance")) {
                ui.inspection_advance.reset();
                ui.windowed_pacing.reset();
                ui.resume_after_advance = false;
            }
        }
        ImGui::BeginDisabled(terminal);
        if (ui.inspection_advance.remaining_steps() == 0U &&
            ui.simulation_elapsed_seconds < 3300.0F &&
            ImGui::Button("Advance to 55 min and continue")) {
            ui.inspection_advance.request(3300.0F - ui.simulation_elapsed_seconds,
                                          ui.fixed_delta_seconds);
            ui.resume_after_advance = true;
            ui.paused = true;
            ui.windowed_pacing.reset();
        }
        if (ImGui::CollapsingHeader("Inspection tools")) {
            ImGui::BeginDisabled(ui.inspection_advance.remaining_steps() > 0U);
            if (ImGui::Button("Compute next 10 min, then pause")) {
                ui.inspection_advance.request(600.0F, ui.fixed_delta_seconds);
                ui.resume_after_advance = false;
                ui.paused = true;
                ui.windowed_pacing.reset();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "SOURCE  green cubes: continuous upland supply tiles");
        const std::string selected =
            ui.hillside_camera.empty()
                ? (ui.hillside_source_context ? "source (legacy)" : "overview")
                : ui.hillside_camera;
        if (ImGui::BeginCombo("Camera", selected.c_str())) {
            for (const char* camera : {"source", "travel", "collection", "branch", "overview"}) {
                if (!ui.downstream_hillside_cameras_available &&
                    (std::string_view(camera) == "travel" ||
                     std::string_view(camera) == "collection"))
                    continue;
                if (ImGui::Selectable(camera, ui.hillside_camera == camera))
                    ui.hillside_camera = camera;
            }
            ImGui::EndCombo();
        }
        if (ui.motion_markers_available) {
            ImGui::Checkbox("Moving markers", &ui.show_motion_markers);
            ImGui::TextWrapped(
                ui.local_motion_markers
                    ? "Local pale dots show movement HERE, seeded across wet terrain. They are not "
                      "parcels that travelled from the source; trails follow actual depth-averaged "
                      "velocity."
                    : "Source-released pale dots follow simulated depth-averaged velocity. Their "
                      "movement slows in pools; release continues at the green source.");
        }
        if (ui.hillside_depth_cues && ui.presentation_view == Fluid25DPresentationView::Catchment)
            ImGui::TextWrapped(
                "Depth: fixed log scale, 1 cm / 10 cm / 1 m / 10 m. Cyan is thin runoff; deep blue "
                "is accumulation. Not speed. Native 30 m geometric steps remain.");
        if (ui.presentation_view == Fluid25DPresentationView::Diagnostics &&
            ui.debug_view == Fluid25DDebugView::WaterDepth)
            ImGui::TextWrapped(
                "Diagnostic depth map: brighter cyan means deeper water; linear color saturates "
                "at about 8.3 cm. This map shows the footprint, not the 3D log-depth palette.");
        if (ui.dye_enabled) {
            const bool active = ui.simulation_elapsed_seconds >= ui.dye_start_seconds &&
                                ui.simulation_elapsed_seconds < ui.dye_end_seconds;
            ImGui::Text("Color input: %s (%.0f-%.0f physical min)",
                        active ? "active"
                               : (ui.simulation_elapsed_seconds < ui.dye_start_seconds
                                      ? "waiting"
                                      : "ended; clear supply continues"),
                        ui.dye_start_seconds / 60.0F, ui.dye_end_seconds / 60.0F);
            ImGui::TextWrapped(
                "Dye concentration uses a fixed log scale: %.1f%%, %.0f%%, %.0f%%, %.0f%% "
                "of the injected concentration. Below %.2f%% retains clear-water color.",
                100.0F * kFluid25DHillsideDyePaletteLegendConcentrations[0],
                100.0F * kFluid25DHillsideDyePaletteLegendConcentrations[1],
                100.0F * kFluid25DHillsideDyePaletteLegendConcentrations[2],
                100.0F * kFluid25DHillsideDyePaletteLegendConcentrations[3],
                kFluid25DHillsideDyePaletteClearFloorPercent);
        }
        ImGui::TextWrapped("Watch water descend and collect in the unchanged terrain. There is no "
                           "chosen drain or route. Crop edges permit outward flow, never inflow.");
        ImGui::TextWrapped(
            "Inspection advance executes every fixed solver step from the dry start; "
            "it does not teleport water, enlarge the time step, or change source strength.");
    }
    if (ui.scenario == Fluid25DScenario::SourceOutletDemo) {
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "SOURCE  green cubes: continuous water input tiles");
        ImGui::TextColored(ImVec4(1.00F, 0.56F, 0.08F, 1.0F),
                           "DRAIN  amber cubes: explicit water removal tiles");
        ImGui::TextWrapped("Read the connected ribbon from green to amber. The downstream "
                           "terrain shoulder makes the drain patch the terminal basin; water does "
                           "not continue off the far side of this closed scene.");
        ImGui::TextWrapped("Colored cubes highlight the actual source/drain cells; "
                           "their words stay here in the panel so they remain legible at every "
                           "camera distance. The shallow reset ribbon reveals the full route "
                           "immediately; it is not a claim that one newly injected parcel has "
                           "already crossed the whole route.");
    }
    if (ui.scenario == Fluid25DScenario::HillsideRainStudy) {
        ImGui::Text("Physical time: %.1f min (%.0f s)", ui.simulation_elapsed_seconds / 60.0F,
                    ui.simulation_elapsed_seconds);
        ImGui::Text("Rain: %.6g mm/h | %.6g m3/s", ui.rainfall_rate_mm_per_hour,
                    ui.rainfall_total_input_m3_per_s);
        ImGui::Text("Scheduled since reset: %.4f m | %.1f m3",
                    ui.rain_study.state().cumulative_depth_m,
                    ui.rain_study.state().scheduled_volume_m3);
        ImGui::BeginDisabled(terminal);
        if (cubey::host::imgui_button("Queue Rain Off",
                                      "Apply rain-off at the next fixed solver step.")) {
            ui.rain_study.queue_enabled(false);
        }
        ImGui::SameLine();
        if (cubey::host::imgui_button("Queue Rain On",
                                      "Apply rain-on at the next fixed solver step.")) {
            ui.rain_study.queue_enabled(true);
        }
        ImGui::EndDisabled();
        if (ui.rain_study.queued_enabled().has_value()) {
            ImGui::Text("Queued rain %s for the next fixed step.",
                        *ui.rain_study.queued_enabled() ? "On" : "Off");
        } else {
            ImGui::Text("Rain is %s.", ui.rain_study.enabled() ? "on" : "off");
        }
        ImGui::Text("Playback: %.3gx | %s", ui.presentation_time_scale,
                    ui.inspection_advance.remaining_steps() > 0U ? "inspection advance"
                    : ui.paused                                  ? "paused"
                                                                 : "continuous");
        if (ui.inspection_advance.remaining_steps() > 0U) {
            ImGui::Text("Computing advance: %.1f s remaining",
                        static_cast<double>(ui.inspection_advance.remaining_steps()) *
                            ui.fixed_delta_seconds);
            ImGui::ProgressBar(ui.inspection_advance.progress());
            if (ImGui::Button("Cancel advance")) {
                ui.inspection_advance.reset();
                ui.windowed_pacing.reset();
                ui.resume_after_advance = false;
            }
        }
        ImGui::BeginDisabled(terminal);
        if (ui.inspection_advance.remaining_steps() == 0U &&
            ui.simulation_elapsed_seconds < 7200.0F &&
            ImGui::Button("Advance to 2 h and continue")) {
            ui.inspection_advance.request(7200.0F - ui.simulation_elapsed_seconds,
                                          ui.fixed_delta_seconds);
            ui.resume_after_advance = true;
            ui.paused = true;
            ui.windowed_pacing.reset();
        }
        if (ui.inspection_advance.remaining_steps() == 0U &&
            ImGui::CollapsingHeader("Inspection tools")) {
            ImGui::BeginDisabled(ui.inspection_advance.remaining_steps() > 0U);
            if (ImGui::Button("Compute next 10 min, then pause")) {
                ui.inspection_advance.request(600.0F, ui.fixed_delta_seconds);
                ui.resume_after_advance = false;
                ui.paused = true;
                ui.windowed_pacing.reset();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        const std::string selected = ui.hillside_camera.empty() ? "overview" : ui.hillside_camera;
        if (ImGui::BeginCombo("Camera", selected.c_str())) {
            for (const char* camera : {"travel", "collection", "overview"}) {
                if (!ui.downstream_hillside_cameras_available &&
                    std::string_view(camera) != "overview")
                    continue;
                if (ImGui::Selectable(camera, ui.hillside_camera == camera))
                    ui.hillside_camera = camera;
            }
            ImGui::EndCombo();
        }
        if (ui.motion_markers_available) {
            ImGui::Checkbox("Moving markers", &ui.show_motion_markers);
            ImGui::TextWrapped(
                "Local pale dots show movement here, seeded across materially wet terrain. They "
                "are local motion indicators, not parcels from a source; trails follow actual "
                "depth-averaged velocity.");
        }
        if (ui.hillside_depth_cues && ui.presentation_view == Fluid25DPresentationView::Catchment)
            ImGui::TextWrapped(
                "Depth: fixed log scale, 1 cm / 10 cm / 1 m / 10 m. Cyan is thin runoff; deep "
                "blue is accumulation. Native 30 m geometric steps remain.");
        if (ui.presentation_view == Fluid25DPresentationView::Diagnostics &&
            ui.debug_view == Fluid25DDebugView::WaterDepth)
            ImGui::TextWrapped(
                "Diagnostic depth map: brighter cyan means deeper water; linear color saturates "
                "at about 8.3 cm. This map shows the footprint, not the 3D log-depth palette.");
        ImGui::TextWrapped(
            "Uniform rainfall adds surface water across the dry, unchanged crop. Every crop edge "
            "permits outward flow only; there is no selected source, sink, or outlet.");
        ImGui::TextWrapped(
            "Queued rain changes apply at the next fixed step. Pausing preserves a queued change; "
            "Reset returns to dry terrain with rain on at physical time zero.");
        ImGui::TextWrapped(
            "Inspection advance executes every fixed solver step; it does not teleport water, "
            "enlarge the time step, or change rainfall strength.");
    }
    if (ui.scenario == Fluid25DScenario::MountainSourceOutletDemo) {
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "SOURCE  green cubes: 0.75 m3/s total input region");
        ImGui::TextColored(ImVec4(1.00F, 0.56F, 0.08F, 1.0F),
                           "DRAIN  amber cubes: three actual removal cells");
        ImGui::TextWrapped(
            "The explicit 0.75 m3/s drain is the three reviewed lowest cells near the closed "
            "boundary; cubes show those exact cells, not the whole basin. Read the blue "
            "corridor from green to amber as a prewetted initial route.");
        ImGui::TextWrapped(
            "This is an immutable real-terrain crop with closed outer boundaries. It is a "
            "readable source-to-outlet demonstration, not proof that an individual water "
            "parcel, a mapped river, or a provenance claim follows this exact route.");
    }
    if (ui.scenario == Fluid25DScenario::SustainedHeadwatersDemo) {
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "GREEN CUBES  two continuous input patches; dry start");
        ImGui::TextColored(ImVec4(1.00F, 0.56F, 0.08F, 1.0F),
                           "OPEN EAST EDGE  boundary outflow, not a drain patch");
        ImGui::TextWrapped(
            "The tributaries join before the open outlet. No amber drain cubes are shown "
            "because this scene removes water only through boundary outflow.");
    }
    if (ui.scenario == Fluid25DScenario::NaturalFlowStudy) {
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "SOURCE  green cubes: five-cell continuous input");
        ImGui::TextColored(ImVec4(1.00F, 0.56F, 0.08F, 1.0F),
                           "EXPECTED EXIT  diagnostic observation window; no drain cubes");
        ImGui::TextWrapped(
            "The native elevation is unchanged and starts dry. Every crop edge is independently "
            "outflow-only. The recipe's expected exit is a diagnostic window, not an explicit "
            "drain region, and is not drawn as amber cubes.");
    }
    if (fluid_25d_uses_forcing_cubes(ui.scenario)) {
        ImGui::TextWrapped("Cubes are one-cell-wide highlights centered at each tile's terrain "
                           "height. Their height is not water depth or a physical emitter volume; "
                           "terrain hides buried portions.");
    }
    if (ui.presentation_view == Fluid25DPresentationView::Catchment)
        ImGui::TextWrapped("Terrain is the matte bed. Bright cyan is shallower water; "
                           "darker blue is deeper water.");
    ImGui::TextWrapped("Composite's moving highlight is a passive render-only marker advected "
                       "by velocity; it is not waves or a depth cue.");
    ImGui::TextWrapped("Water Isolation quiets the bed to expose the wet edge. "
                       "Flow Inspection adds fixed-grid arrows: their angle shows local flow "
                       "direction, while length and brightness show speed (blue is slower; "
                       "yellow is faster). They are pitch-scaled velocity samples, not water "
                       "particles or waves.");
    if (ui.transport_inspection_available) {
        if (ui.scenario == Fluid25DScenario::HillsideFlowStudy) {
            ImGui::TextWrapped(
                "Transport Inspection hides arrows and the moving surface cue. Magenta-to-violet "
                "is conserved dye concentration, not depth or speed. Clear supply continues after "
                "the timed source pulse; follow the parcel down the unchanged bed. There is no "
                "chosen outlet: water and dye can leave only across crop edges.");
        } else if (ui.scenario == Fluid25DScenario::NaturalFlowStudy) {
            ImGui::TextWrapped(
                "Transport Inspection hides arrows and the moving surface cue. Magenta-to-violet "
                "water is conserved dye concentration from the five-cell source. The expected "
                "exit is observation-only, not a sink or a prescribed outlet.");
        } else {
            ImGui::TextWrapped(
                "Transport Inspection hides arrows and the moving surface cue. Magenta-to-violet "
                "water is the conserved dye concentration: watch the pulse leave the green SOURCE, "
                "travel through the blue water, then disappear into the amber OUTLET. It is a "
                "transport marker, not a depth, velocity, or lighting change.");
        }
    }
    ImGui::TextDisabled("Space pause/resume  R reset  A diagnostics  D field  drag orbit");
    ImGui::End();
}

} // namespace cubey::projects::fluid::fluid_25d
