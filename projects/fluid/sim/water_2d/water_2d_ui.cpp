#include "water_2d_ui.h"

#include "water_2d_gpu_resources.h"

#include <cubey/host/imgui_helpers.h>
#include <cubey/vulkan/device.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::water_2d {
namespace {

constexpr std::array<Water2DDebugView, 8> kDebugViews{
    Water2DDebugView::Surface,  Water2DDebugView::Particles,  Water2DDebugView::Cells,
    Water2DDebugView::Velocity, Water2DDebugView::Divergence, Water2DDebugView::Pressure,
    Water2DDebugView::Solid,    Water2DDebugView::Foam,
};

constexpr std::array<Water2DScenario, 4> kScenarios{
    Water2DScenario::DamBreak,
    Water2DScenario::ObstacleSplash,
    Water2DScenario::WaveSlab,
    Water2DScenario::HoseFill,
};

constexpr std::array<Water2DObstacleShape, 3> kObstacleShapes{
    Water2DObstacleShape::None,
    Water2DObstacleShape::Circle,
    Water2DObstacleShape::Box,
};

constexpr std::array<Water2DTransferMode, 2> kTransferModes{
    Water2DTransferMode::Apic,
    Water2DTransferMode::PicFlip,
};

void reset_simulation(Water2DUiContext& ui) {
    ui.reset_requested = true;
    ui.runtime_state = {};
}

} // namespace

void draw_water_2d_ui(Water2DUiContext ui) {
    if (!cubey::host::begin_control_panel(ui.title)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Paused", &ui.paused);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        reset_simulation(ui);
    }

    cubey::host::imgui_enum_combo("Debug view", ui.debug_view, kDebugViews,
                                  water_2d_debug_view_name);

    ImGui::Spacing();
    if (cubey::host::imgui_section("Simulation", true)) {
        const cubey::host::ScopedImGuiId section_id("Simulation");
        if (cubey::host::imgui_enum_combo("Scenario", ui.config.scenario, kScenarios,
                                          water_2d_scenario_name)) {
            apply_water_2d_scenario_defaults(ui.config);
            reset_simulation(ui);
        }

        cubey::host::imgui_enum_combo("Transfer", ui.config.transfer_mode, kTransferModes,
                                      water_2d_transfer_mode_name);

        int pressure_iterations = static_cast<int>(ui.config.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 512)) {
            ui.config.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        int substeps = static_cast<int>(ui.config.substeps);
        if (ImGui::SliderInt("Substeps", &substeps, 1, 4)) {
            ui.config.substeps = static_cast<std::uint32_t>(substeps);
        }
        ImGui::SliderFloat("PIC/FLIP blend", &ui.config.flip_ratio, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Velocity limit", &ui.config.velocity_limit, 1.0F, 8.0F, "%.2f");
        ImGui::SliderFloat("Particle damping", &ui.config.particle_damping, 0.980F, 1.000F, "%.3f");
        ImGui::SliderFloat("Particle separation radius", &ui.config.particle_separation_radius,
                           0.20F, 1.40F, "%.2f");
        ImGui::SliderFloat("Particle separation strength", &ui.config.particle_separation_strength,
                           0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Particle volume strength", &ui.config.particle_volume_strength, 0.0F,
                           48.0F, "%.1f");
        int transfer_limit = static_cast<int>(ui.config.max_particles_per_cell);
        if (ImGui::SliderInt("Transfer limit/cell", &transfer_limit, 8, 256)) {
            ui.config.max_particles_per_cell = static_cast<std::uint32_t>(transfer_limit);
        }
        ImGui::SliderFloat("Gravity", &ui.config.gravity, -4.0F, 0.0F, "%.2f");
        ImGui::SliderFloat("Boundary bounce", &ui.config.boundary_restitution, 0.0F, 0.8F, "%.2f");
    }

    if (cubey::host::imgui_section("Initial volume", false)) {
        const cubey::host::ScopedImGuiId section_id("Initial volume");
        if (ImGui::SliderFloat("Fill height", &ui.config.initial_fill_height,
                               kWater2DMinFillFraction, kWater2DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
        if (ImGui::SliderFloat("Fill width", &ui.config.initial_fill_width, kWater2DMinFillFraction,
                               kWater2DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
    }

    if (cubey::host::imgui_section("Sources and forces", true)) {
        const cubey::host::ScopedImGuiId section_id("Sources and forces");
        ImGui::SeparatorText("Hose");
        ImGui::PushID("hose");
        ImGui::Checkbox("Enabled", &ui.config.hose.enabled);
        ImGui::SliderFloat2("Position", ui.config.hose.position.data(), 0.04F, 0.96F, "%.2f");
        ImGui::SliderFloat("Angle", &ui.config.hose.angle_degrees, -90.0F, 20.0F, "%.1f deg");
        ImGui::SliderFloat("Speed", &ui.config.hose.speed, 0.2F, 5.0F, "%.2f");
        ImGui::SliderFloat("Radius", &ui.config.hose.radius, 0.005F, 0.080F, "%.3f");
        ImGui::SliderFloat("Rate", &ui.config.hose.particles_per_second, 0.0F, 60000.0F, "%.0f/s");
        ImGui::SliderFloat("Spread", &ui.config.hose.spread_degrees, 0.0F, 45.0F, "%.1f deg");
        ImGui::PopID();

        ImGui::SeparatorText("Drain");
        ImGui::PushID("drain");
        ImGui::Checkbox("Enabled", &ui.config.drain.enabled);
        ImGui::SliderFloat2("Center", ui.config.drain.center.data(), 0.04F, 0.96F, "%.2f");
        ImGui::SliderFloat2("Half size", ui.config.drain.half_size.data(), 0.01F, 0.24F, "%.3f");
        ImGui::SliderFloat("Pull speed", &ui.config.drain.pull_speed, 0.0F, 6.0F, "%.2f");
        ImGui::SliderFloat("Pull radius", &ui.config.drain.pull_radius, 0.02F, 1.0F, "%.2f");
        ImGui::PopID();

        ImGui::SeparatorText("Wave");
        ImGui::PushID("wave");
        ImGui::Checkbox("Enabled", &ui.config.wave.enabled);
        ImGui::SliderFloat2("Center", ui.config.wave.center.data(), 0.04F, 0.96F, "%.2f");
        ImGui::SliderFloat2("Half size", ui.config.wave.half_size.data(), 0.01F, 0.40F, "%.3f");
        ImGui::SliderFloat("Amplitude", &ui.config.wave.amplitude, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Frequency", &ui.config.wave.frequency_hz, 0.0F, 2.0F, "%.2f Hz");
        ImGui::PopID();
    }

    if (cubey::host::imgui_section("Obstacles", false)) {
        const cubey::host::ScopedImGuiId section_id("Obstacles");
        if (cubey::host::imgui_enum_combo("Shape", ui.config.obstacle_shape, kObstacleShapes,
                                          water_2d_obstacle_shape_name)) {
            reset_simulation(ui);
        }

        if (ui.config.obstacle_shape != Water2DObstacleShape::None &&
            ImGui::SliderFloat2("Center", ui.config.obstacle_center.data(), 0.10F, 0.90F, "%.2f")) {
            reset_simulation(ui);
        }
        if (ui.config.obstacle_shape == Water2DObstacleShape::Circle &&
            ImGui::SliderFloat("Radius", &ui.config.obstacle_radius, 0.02F, 0.22F, "%.3f")) {
            reset_simulation(ui);
        }
        if (ui.config.obstacle_shape == Water2DObstacleShape::Box &&
            ImGui::SliderFloat2("Half size", ui.config.obstacle_half_size.data(), 0.02F, 0.24F,
                                "%.3f")) {
            reset_simulation(ui);
        }
        ImGui::SliderFloat("Obstacle friction", &ui.config.obstacle_friction, 0.0F, 1.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Surface", false)) {
        const cubey::host::ScopedImGuiId section_id("Surface");
        ImGui::SliderFloat("Particle radius", &ui.config.particle_radius, 0.0025F, 0.025F, "%.4f");
        ImGui::SliderFloat("Splat radius", &ui.config.surface_splat_radius_scale, 0.50F, 3.00F,
                           "%.2f");
        ImGui::SliderFloat("Density scale", &ui.config.surface_density_scale, 0.10F, 2.00F, "%.2f");
        int smoothing_iterations = static_cast<int>(ui.config.surface_smoothing_iterations);
        if (ImGui::SliderInt("Smooth passes", &smoothing_iterations, 0, 8)) {
            ui.config.surface_smoothing_iterations =
                static_cast<std::uint32_t>(smoothing_iterations);
        }
        ImGui::SliderFloat("Smooth radius", &ui.config.surface_smoothing_radius_px, 0.0F, 18.0F,
                           "%.1f px");
        ImGui::SliderFloat("Surface threshold", &ui.config.surface_threshold, 0.20F, 1.60F, "%.2f");
        ImGui::SliderFloat("Edge strength", &ui.config.edge_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Refraction", &ui.config.surface_refraction_strength, 0.0F, 0.05F,
                           "%.3f");
        ImGui::SliderFloat("Caustics", &ui.config.surface_caustic_strength, 0.0F, 0.8F, "%.2f");
        ImGui::SliderFloat("Specular", &ui.config.surface_specular_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam strength", &ui.config.foam_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam sharpness", &ui.config.foam_sharpness, 0.35F, 4.0F, "%.2f");
        ImGui::SliderFloat("Foam breakup", &ui.config.foam_breakup, 0.0F, 1.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Diagnostics", true)) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        ImGui::Text("Grid: %u x %u", ui.config.grid_width, ui.config.grid_height);
        const std::uint32_t hose_pool_capacity = hose_particle_pool_capacity_for_config(ui.config);
        const std::uint32_t scanned_particles =
            water_2d_runtime_particle_scan_count(ui.config, ui.runtime_state);
        const std::uint32_t touched_hose_particles =
            scanned_particles > ui.config.active_particle_count
                ? scanned_particles - ui.config.active_particle_count
                : 0U;
        ImGui::Text("Particles: %u reset / %u hose pool / %u total",
                    ui.config.active_particle_count, hose_pool_capacity,
                    ui.config.particle_capacity);
        ImGui::Text("Compute particles: %u scanned / %u total", scanned_particles,
                    ui.config.particle_capacity);
        ImGui::Text("Hose touched: %u / %u", touched_hose_particles, hose_pool_capacity);
        cubey::host::draw_frame_stats(ui.latest_frame_stats, ui.latest_fps, ui.latest_frame_ms);

        cubey::host::draw_gpu_timings(ui.resources.latest_timings());

        const VkDeviceSize water_bytes = ui.resources.allocated_buffer_bytes();
        const cubey::vulkan::DeviceMemoryBudgetInfo memory_budget =
            ui.device.device_memory_budget();
        cubey::host::draw_device_memory_budget(water_bytes, memory_budget, "Water GPU buffers");
    }
    ImGui::End();
}

} // namespace cubey::projects::fluid::water_2d
