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

    cubey::host::imgui_checkbox("Paused", &ui.paused, "Pause smoke simulation time.");
    ImGui::SameLine();
    if (cubey::host::imgui_button("Reset", "Restart the smoke field and injector state.")) {
        ui.reset_requested = true;
        ui.reset_injectors_requested = true;
    }

    cubey::host::imgui_enum_combo("Debug view", ui.debug_view, kDebugViews,
                                  smoke_2d_debug_view_name);

    ImGui::Spacing();
    if (const cubey::host::ScopedImGuiGroup group{
            "Simulation",
            {.help = "Advection, pressure, and dissipation controls for the smoke solve."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Simulation");
        if (cubey::host::imgui_enum_combo("Pressure solver", ui.config.pressure_solver,
                                          kPressureSolvers, smoke_2d_pressure_solver_name)) {
            ui.reset_requested = true;
        }
        cubey::host::imgui_slider_uint32("Pressure iterations", &ui.config.pressure_iterations, 1U,
                                         96U);
        cubey::host::imgui_slider_float("Vorticity", &ui.config.vorticity_strength, 0.0F, 40.0F);
        cubey::host::imgui_slider_float("Advection scale", &ui.config.advection_strength, 0.04F,
                                        0.32F, "%.3f");
        cubey::host::imgui_slider_float("Dye decay", &ui.config.dye_decay_per_second, 0.950F,
                                        1.0F, "%.4f");
        cubey::host::imgui_slider_float("Velocity decay", &ui.config.velocity_decay_per_second,
                                        0.950F, 1.0F, "%.4f");
        cubey::host::imgui_slider_float("Cleanup strength",
                                        &ui.config.low_energy_cleanup_strength, 0.0F, 0.35F,
                                        "%.3f");
        cubey::host::imgui_slider_float("Cleanup start", &ui.config.low_energy_cleanup_start, 0.0F,
                                        ui.config.low_energy_cleanup_end - 0.001F, "%.3f");
        cubey::host::imgui_slider_float("Cleanup end", &ui.config.low_energy_cleanup_end,
                                        ui.config.low_energy_cleanup_start + 0.001F, 0.5F,
                                        "%.3f");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Injectors",
            {.default_open = false,
             .help = "Procedural smoke source placement and force controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Injectors");
        if (cubey::host::imgui_slider_uint32("Injector count",
                                             &ui.config.procedural_injector_count, 1U,
                                             kMaxProceduralInjectorCount)) {
            ui.reset_injectors_requested = true;
        }
        cubey::host::imgui_slider_float("Injector radius", &ui.config.injector_injection_radius,
                                        0.005F, 0.080F, "%.3f");
        cubey::host::imgui_slider_float("Injection force",
                                        &ui.config.injector_injection_strength, 0.0F, 20.0F,
                                        "%.1f");
        cubey::host::imgui_slider_float("Propulsion", &ui.config.injector_propulsion_strength,
                                        0.0F, 3.0F, "%.2f");
        cubey::host::imgui_slider_float("Orbit radius", &ui.config.injector_orbit_radius, 0.04F,
                                        0.42F, "%.3f");
        cubey::host::imgui_slider_float("Radius spread",
                                        &ui.config.injector_orbit_radius_spread, 0.0F, 0.50F,
                                        "%.3f");
        cubey::host::imgui_slider_float("Angular speed",
                                        &ui.config.injector_orbit_angular_speed, -2.0F, 2.0F,
                                        "%.2f");
        cubey::host::imgui_slider_float("Speed spread",
                                        &ui.config.injector_orbit_angular_speed_spread, 0.0F, 4.0F,
                                        "%.2f");
        cubey::host::imgui_slider_float("Phase spread",
                                        &ui.config.injector_orbit_phase_spread, 0.0F, 1.0F,
                                        "%.2f");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Rendering", {.default_open = false, .help = "Smoke debug rendering state."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Rendering");
        ImGui::Text("Debug view: %s", smoke_2d_debug_view_name(ui.debug_view));
    }

    const std::array<cubey::host::PerformanceCounter, 2> performance_counters{
        cubey::host::PerformanceCounter{
            "Grid cells",
            static_cast<std::uint64_t>(ui.config.grid_width) *
                static_cast<std::uint64_t>(ui.config.grid_height),
            nullptr},
        cubey::host::PerformanceCounter{"Injectors", ui.config.procedural_injector_count, nullptr},
    };
    cubey::host::PerformanceUiContext performance = ui.performance;
    performance.counters = performance_counters;
    performance.gpu_timings = ui.resources.latest_timings();
    cubey::host::draw_performance_ui(performance);

    if (const cubey::host::ScopedImGuiGroup group{
            "Diagnostics",
            {.default_open = false,
             .help = "Read-only smoke grid and GPU timing statistics."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        ImGui::Text("Grid: %u x %u", ui.config.grid_width, ui.config.grid_height);
        ImGui::Text("Injectors: %u", ui.config.procedural_injector_count);
    }

    ImGui::End();
}

} // namespace cubey::projects::fluid::smoke_2d
