#include "fluid_25d_ui.h"

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
}

} // namespace

void draw_fluid_25d_ui(Fluid25DUiContext ui) {
    if (!cubey::host::begin_control_panel(ui.title, {.width = 360.0F})) {
        ImGui::End();
        return;
    }

    cubey::host::imgui_enum_combo(
        "Presentation", ui.presentation_view, kPresentationViews,
        fluid_25d_presentation_view_name,
        "Choose the oblique catchment surface or the top-down diagnostics surface.");
    if (ui.presentation_view == Fluid25DPresentationView::Catchment) {
        cubey::host::imgui_enum_combo(
            "Catchment mode", ui.catchment_view, kCatchmentViews,
            fluid_25d_catchment_view_name,
            "Composite is the normal 3D view; the other modes are reading aids.");
    }
    if (ui.presentation_view == Fluid25DPresentationView::Diagnostics) {
        cubey::host::imgui_enum_combo("Diagnostic field", ui.debug_view, kDebugViews,
                                      fluid_25d_debug_view_name,
                                      "Top-down numerical field shown by diagnostics.");
    }

    if (cubey::host::imgui_button(ui.paused ? "Resume" : "Pause",
                                  "Pause or resume windowed simulation time.")) {
        ui.paused = !ui.paused;
    }
    ImGui::SameLine();
    if (cubey::host::imgui_button("Reset", "Restart the deterministic scenario.")) {
        request_reset(ui);
    }

    if (cubey::host::imgui_slider_float(
            "Playback speed", &ui.presentation_time_scale,
            kFluid25DMinWindowedPresentationTimeScale,
            kFluid25DMaxWindowedPresentationTimeScale, "%.3gx",
            "Windowed pacing only; solver fixed delta and headless timing are unchanged.")) {
        ui.windowed_pacing.set_presentation_time_scale(ui.presentation_time_scale);
    }

    ImGui::SeparatorText("How to read it");
    if (ui.scenario == Fluid25DScenario::SourceOutletDemo) {
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "SOURCE  green ring: continuous water input");
        ImGui::TextColored(ImVec4(1.00F, 0.56F, 0.08F, 1.0F),
                           "OUTLET  amber ring: explicit downstream sink");
        ImGui::TextWrapped("Read the connected ribbon from green to amber. The downstream "
                          "terrain shoulder makes the amber ring the terminal basin; water does "
                          "not continue off the far side of this closed scene.");
        ImGui::TextWrapped("The colored endpoint labels are terrain-draped rings in the scene; "
                          "their words stay here in the panel so they remain legible at every "
                          "camera distance. The shallow reset ribbon reveals the full route "
                          "immediately; it is not a claim that one newly injected parcel has "
                          "already crossed the whole route.");
    }
    if (ui.scenario == Fluid25DScenario::MountainSourceOutletDemo) {
        ImGui::TextColored(ImVec4(0.16F, 0.88F, 0.34F, 1.0F),
                           "SOURCE  green ring: 0.75 m3/s total input region");
        ImGui::TextColored(ImVec4(1.00F, 0.56F, 0.08F, 1.0F),
                           "OUTLET  amber ring: visible basin at cell (232,122)");
        ImGui::TextWrapped(
            "The explicit 0.75 m3/s drain is the three reviewed lowest cells near the closed "
            "boundary, not every cell inside the visible amber basin. Read the broad blue "
            "corridor from green to amber as a prewetted initial route.");
        ImGui::TextWrapped(
            "This is an immutable real-terrain crop with closed outer boundaries. It is a "
            "readable source-to-outlet demonstration, not proof that an individual water "
            "parcel, a mapped river, or a provenance claim follows this exact route.");
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
    ImGui::TextDisabled("Space pause/resume  R reset  A diagnostics  D field  drag orbit");
    ImGui::End();
}

} // namespace cubey::projects::fluid::fluid_25d
