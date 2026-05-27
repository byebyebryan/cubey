#include "smoke_2d_ui.h"

#include "smoke_2d_gpu_resources.h"

#include <cubey/host/imgui_helpers.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::smoke_2d {
namespace {

constexpr std::array<Smoke2DDebugView, 6> kDebugViews{
    Smoke2DDebugView::Dye,      Smoke2DDebugView::Velocity, Smoke2DDebugView::Divergence,
    Smoke2DDebugView::Pressure, Smoke2DDebugView::Speed,    Smoke2DDebugView::Vorticity,
};

constexpr std::array<Smoke2DPressureSolver, 2> kPressureSolvers{
    Smoke2DPressureSolver::Jacobi,
    Smoke2DPressureSolver::RedBlackGaussSeidel,
};

} // namespace

void draw_smoke_2d_ui(Smoke2DUiContext ui) {
    if (!cubey::host::begin_control_panel(ui.title)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Paused", &ui.paused);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.reset_requested = true;
        ui.reset_injectors_requested = true;
    }

    cubey::host::imgui_enum_combo("Debug view", ui.debug_view, kDebugViews,
                                  smoke_2d_debug_view_name);

    ImGui::Spacing();
    if (cubey::host::imgui_section("Simulation", true)) {
        const cubey::host::ScopedImGuiId section_id("Simulation");
        if (cubey::host::imgui_enum_combo("Pressure solver", ui.config.pressure_solver,
                                          kPressureSolvers, smoke_2d_pressure_solver_name)) {
            ui.reset_requested = true;
        }
        int pressure_iterations = static_cast<int>(ui.config.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 96)) {
            ui.config.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        ImGui::SliderFloat("Vorticity", &ui.config.vorticity_strength, 0.0F, 40.0F);
        ImGui::SliderFloat("Advection scale", &ui.config.advection_strength, 0.04F, 0.32F, "%.3f");
        ImGui::SliderFloat("Dye decay", &ui.config.dye_decay_per_second, 0.950F, 1.0F, "%.4f");
        ImGui::SliderFloat("Velocity decay", &ui.config.velocity_decay_per_second, 0.950F, 1.0F,
                           "%.4f");
        ImGui::SliderFloat("Cleanup strength", &ui.config.low_energy_cleanup_strength, 0.0F, 0.35F,
                           "%.3f");
        ImGui::SliderFloat("Cleanup start", &ui.config.low_energy_cleanup_start, 0.0F,
                           ui.config.low_energy_cleanup_end - 0.001F, "%.3f");
        ImGui::SliderFloat("Cleanup end", &ui.config.low_energy_cleanup_end,
                           ui.config.low_energy_cleanup_start + 0.001F, 0.5F, "%.3f");
    }

    if (cubey::host::imgui_section("Injectors", true)) {
        const cubey::host::ScopedImGuiId section_id("Injectors");
        int injector_count = static_cast<int>(ui.config.procedural_injector_count);
        if (ImGui::SliderInt("Injector count", &injector_count, 1,
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

    if (cubey::host::imgui_section("Rendering", false)) {
        const cubey::host::ScopedImGuiId section_id("Rendering");
        ImGui::Text("Debug view: %s", smoke_2d_debug_view_name(ui.debug_view));
    }

    if (cubey::host::imgui_section("Diagnostics", true)) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        ImGui::Text("Grid: %u x %u", ui.config.grid_width, ui.config.grid_height);
        ImGui::Text("Injectors: %u", ui.config.procedural_injector_count);
        cubey::host::draw_gpu_timings(ui.resources.latest_timings());
    }

    ImGui::End();
}

} // namespace cubey::projects::fluid::smoke_2d
