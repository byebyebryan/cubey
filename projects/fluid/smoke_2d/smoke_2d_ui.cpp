#include "smoke_2d_ui.h"

#include <imgui.h>

#include <array>
#include <cstdint>

namespace cubey::projects::fluid::smoke_2d {
namespace {

constexpr std::array<Smoke2DDebugView, 7> kDebugViews{
    Smoke2DDebugView::Dye,      Smoke2DDebugView::Velocity, Smoke2DDebugView::Divergence,
    Smoke2DDebugView::Pressure, Smoke2DDebugView::Speed,    Smoke2DDebugView::Vorticity,
    Smoke2DDebugView::Obstacle,
};

[[nodiscard]] bool section(const char* label, bool default_open) {
    const ImGuiTreeNodeFlags flags =
        default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
    return ImGui::CollapsingHeader(label, flags);
}

} // namespace

void draw_smoke_2d_ui(Smoke2DUiContext ui) {
    ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ui.title)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Paused", &ui.paused);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.reset_requested = true;
        ui.reset_injectors_requested = true;
    }

    if (ImGui::BeginCombo("Debug view", smoke_2d_debug_view_name(ui.debug_view))) {
        for (Smoke2DDebugView view : kDebugViews) {
            const bool selected = view == ui.debug_view;
            if (ImGui::Selectable(smoke_2d_debug_view_name(view), selected)) {
                ui.debug_view = view;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    if (section("Simulation", true)) {
        int pressure_iterations = static_cast<int>(ui.config.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 96)) {
            ui.config.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        ImGui::SliderFloat("Vorticity", &ui.config.vorticity_strength, 0.0F, 40.0F);
        ImGui::SliderFloat("Dye decay", &ui.config.dye_decay_per_second, 0.950F, 1.0F, "%.4f");
        ImGui::SliderFloat("Velocity decay", &ui.config.velocity_decay_per_second, 0.950F, 1.0F,
                           "%.4f");
    }

    if (section("Injectors", true)) {
        int injector_count = static_cast<int>(ui.config.procedural_injector_count);
        if (ImGui::SliderInt("Injectors", &injector_count, 1,
                             static_cast<int>(kMaxProceduralInjectorCount))) {
            ui.config.procedural_injector_count = static_cast<std::uint32_t>(injector_count);
            ui.reset_injectors_requested = true;
        }
        ImGui::SliderFloat("Injector radius", &ui.config.injector_injection_radius, 0.005F, 0.080F,
                           "%.3f");
        ImGui::SliderFloat("Injection force", &ui.config.injector_injection_strength, 0.0F, 20.0F,
                           "%.1f");
        ImGui::SliderFloat("Propulsion", &ui.config.injector_propulsion_strength, 0.0F, 3.0F,
                           "%.2f");
        ImGui::SliderFloat("Orbit radius", &ui.config.injector_orbit_radius, 0.04F, 0.42F, "%.3f");
        ImGui::SliderFloat("Radius spread", &ui.config.injector_orbit_radius_spread, 0.0F, 0.50F,
                           "%.3f");
        ImGui::SliderFloat("Angular speed", &ui.config.injector_orbit_angular_speed, -2.0F, 2.0F,
                           "%.2f");
        ImGui::SliderFloat("Speed spread", &ui.config.injector_orbit_angular_speed_spread, 0.0F,
                           4.0F, "%.2f");
        ImGui::SliderFloat("Phase spread", &ui.config.injector_orbit_phase_spread, 0.0F, 1.0F,
                           "%.2f");
    }

    if (section("Obstacles", false)) {
        ImGui::Text("Enabled: %s", ui.config.obstacles_enabled ? "yes" : "no");
    }

    if (section("Rendering", false)) {
        ImGui::Text("Debug view: %s", smoke_2d_debug_view_name(ui.debug_view));
    }

    if (section("Diagnostics", true)) {
        ImGui::Text("Grid: %u x %u", ui.config.grid_width, ui.config.grid_height);
        ImGui::Text("Injectors: %u", ui.config.procedural_injector_count);
    }

    ImGui::End();
}

} // namespace cubey::projects::fluid::smoke_2d
