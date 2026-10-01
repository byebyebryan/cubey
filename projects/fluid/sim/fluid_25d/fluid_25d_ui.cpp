#include "fluid_25d_ui.h"
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
    if (!cubey::host::begin_control_panel(ui.title, {.width = 360.0F})) {
        ImGui::End();
        return;
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

    ImGui::BeginDisabled(ui.inspection_advance.remaining_steps() > 0U);
    if (cubey::host::imgui_button(ui.paused ? "Resume" : "Pause",
                                  "Pause or resume windowed simulation time.")) {
        ui.paused = !ui.paused;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (cubey::host::imgui_button("Reset", "Restart the deterministic scenario.")) {
        request_reset(ui);
    }

    if (cubey::host::imgui_slider_float(
            "Playback speed", &ui.presentation_time_scale,
            kFluid25DMinWindowedPresentationTimeScale, kFluid25DMaxWindowedPresentationTimeScale,
            "%.3gx",
            "Windowed pacing only; solver fixed delta and headless timing are unchanged.")) {
        ui.windowed_pacing.set_presentation_time_scale(ui.presentation_time_scale);
    }

    ImGui::SeparatorText("How to read it");
    if (ui.scenario == Fluid25DScenario::HillsideFlowStudy) {
        ImGui::Text("Physical time: %.1f min (%.0f s)", ui.simulation_elapsed_seconds / 60.0F,
                    ui.simulation_elapsed_seconds);
        ImGui::Text("Input: %.1f m3/s | no prescribed drain", ui.continuous_source_m3_per_s);
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
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "SOURCE  green cubes: continuous upland supply tiles");
        const std::string selected =
            ui.hillside_camera.empty()
                ? (ui.hillside_source_context ? "source (legacy)" : "overview")
                : ui.hillside_camera;
        if (ImGui::BeginCombo("Camera", selected.c_str())) {
            for (const char* camera : {"source", "branch", "overview"}) {
                if (ImGui::Selectable(camera, ui.hillside_camera == camera))
                    ui.hillside_camera = camera;
            }
            ImGui::EndCombo();
        }
        if (ui.motion_markers_available) {
            ImGui::Checkbox("Moving markers", &ui.show_motion_markers);
            ImGui::TextWrapped(
                "Pale dots and trails follow the simulated depth-averaged velocity. "
                "Their movement slows in pools. Release continues at the green source.");
        }
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
