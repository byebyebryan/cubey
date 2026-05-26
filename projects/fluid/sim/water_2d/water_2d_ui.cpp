#include "water_2d_ui.h"

#include "water_2d_gpu_resources.h"

#include <cubey/vulkan/device.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

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

[[nodiscard]] double bytes_to_mib(VkDeviceSize bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void reset_simulation(Water2DUiContext& ui) {
    ui.reset_requested = true;
    ui.runtime_state = {};
}

} // namespace

void draw_water_2d_ui(Water2DUiContext ui) {
    ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ui.title)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Paused", &ui.paused);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        reset_simulation(ui);
    }

    if (ImGui::BeginCombo("Debug view", water_2d_debug_view_name(ui.debug_view))) {
        for (Water2DDebugView view : kDebugViews) {
            const bool selected = view == ui.debug_view;
            if (ImGui::Selectable(water_2d_debug_view_name(view), selected)) {
                ui.debug_view = view;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const auto section = [](const char* label, bool default_open) {
        const ImGuiTreeNodeFlags flags =
            default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        return ImGui::CollapsingHeader(label, flags);
    };

    ImGui::Spacing();
    if (section("Simulation", true)) {
        if (ImGui::BeginCombo("Scenario", water_2d_scenario_name(ui.config.scenario))) {
            for (Water2DScenario scenario : kScenarios) {
                const bool selected = scenario == ui.config.scenario;
                if (ImGui::Selectable(water_2d_scenario_name(scenario), selected)) {
                    ui.config.scenario = scenario;
                    apply_water_2d_scenario_defaults(ui.config);
                    reset_simulation(ui);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Transfer", water_2d_transfer_mode_name(ui.config.transfer_mode))) {
            for (Water2DTransferMode mode : kTransferModes) {
                const bool selected = mode == ui.config.transfer_mode;
                if (ImGui::Selectable(water_2d_transfer_mode_name(mode), selected)) {
                    ui.config.transfer_mode = mode;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

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

    if (section("Initial volume", false)) {
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

    if (section("Sources and forces", true)) {
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
        ImGui::PopID();
    }

    if (section("Obstacles", false)) {
        if (ImGui::BeginCombo("Shape", water_2d_obstacle_shape_name(ui.config.obstacle_shape))) {
            for (Water2DObstacleShape shape : kObstacleShapes) {
                const bool selected = shape == ui.config.obstacle_shape;
                if (ImGui::Selectable(water_2d_obstacle_shape_name(shape), selected)) {
                    ui.config.obstacle_shape = shape;
                    reset_simulation(ui);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
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

    if (section("Surface", false)) {
        ImGui::SliderFloat("Particle radius", &ui.config.particle_radius, 0.0025F, 0.025F, "%.4f");
        ImGui::SliderFloat("Surface threshold", &ui.config.surface_threshold, 0.20F, 1.60F, "%.2f");
        ImGui::SliderFloat("Edge strength", &ui.config.edge_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam strength", &ui.config.foam_strength, 0.0F, 1.5F, "%.2f");
    }

    if (section("Diagnostics", true)) {
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
        if (ui.latest_frame_stats.has_value()) {
            ImGui::Text("Frame: %.1f fps / %.2f ms avg (%.2f ms last)", ui.latest_frame_stats->fps,
                        ui.latest_frame_stats->frame_ms, ui.latest_frame_ms);
        } else if (ui.latest_fps > 0.0) {
            ImGui::Text("Frame: %.1f fps / %.2f ms", ui.latest_fps, ui.latest_frame_ms);
        } else {
            ImGui::TextUnformatted("Frame: collecting...");
        }

        const VkDeviceSize water_bytes = ui.resources.allocated_buffer_bytes();
        const cubey::vulkan::DeviceMemoryBudgetInfo memory_budget =
            ui.device.device_memory_budget();
        ImGui::Text("Water GPU buffers: %.1f MiB", bytes_to_mib(water_bytes));
        if (memory_budget.available && memory_budget.device_local_budget > 0) {
            ImGui::Text("VRAM: %.0f / %.0f MiB used",
                        bytes_to_mib(memory_budget.device_local_usage),
                        bytes_to_mib(memory_budget.device_local_budget));
        } else {
            ImGui::Text("VRAM heap: %.0f MiB (usage unavailable)",
                        bytes_to_mib(memory_budget.device_local_heap_size));
        }
    }
    ImGui::End();
}

} // namespace cubey::projects::fluid::water_2d
