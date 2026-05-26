#include "water_3d_ui.h"

#include "water_3d_gpu_resources.h"

#include <cubey/vulkan/device.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::water_3d {
namespace {

constexpr std::array<Water3DRenderView, 12> kRenderViews{
    Water3DRenderView::Surface,
    Water3DRenderView::Particles,
    Water3DRenderView::Cells,
    Water3DRenderView::Velocity,
    Water3DRenderView::Pressure,
    Water3DRenderView::Solid,
    Water3DRenderView::Overpack,
    Water3DRenderView::SurfaceDepth,
    Water3DRenderView::SurfaceThickness,
    Water3DRenderView::SurfaceNormals,
    Water3DRenderView::SurfaceFoam,
    Water3DRenderView::Whitewater,
};

constexpr std::array<Water3DTransferMode, 2> kTransferModes{
    Water3DTransferMode::Apic,
    Water3DTransferMode::PicFlip,
};

[[nodiscard]] double bytes_to_mib(VkDeviceSize bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void reset_simulation(Water3DUiContext& ui) {
    ui.reset_requested = true;
    ui.runtime_state = {};
}

} // namespace

void draw_water_3d_ui(Water3DUiContext ui) {
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

    if (ImGui::BeginCombo("Render view", water_3d_render_view_name(ui.render_view))) {
        for (Water3DRenderView view : kRenderViews) {
            const bool selected = view == ui.render_view;
            if (ImGui::Selectable(water_3d_render_view_name(view), selected)) {
                ui.render_view = view;
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
        if (ImGui::BeginCombo("Transfer", water_3d_transfer_mode_name(ui.config.transfer_mode))) {
            for (Water3DTransferMode mode : kTransferModes) {
                const bool selected = mode == ui.config.transfer_mode;
                if (ImGui::Selectable(water_3d_transfer_mode_name(mode), selected)) {
                    ui.config.transfer_mode = mode;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        int pressure_iterations = static_cast<int>(ui.config.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 128)) {
            ui.config.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        int substeps = static_cast<int>(ui.config.substeps);
        if (ImGui::SliderInt("Substeps", &substeps, 1, 4)) {
            ui.config.substeps = static_cast<std::uint32_t>(substeps);
        }
        ImGui::SliderFloat("PIC/FLIP blend", &ui.config.flip_ratio, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Velocity limit", &ui.config.velocity_limit, 1.0F, 8.0F, "%.2f");
        ImGui::SliderFloat("Particle damping", &ui.config.particle_damping, 0.980F, 1.000F, "%.3f");
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
        if (ImGui::SliderFloat("Fill width", &ui.config.initial_fill_width, kWater3DMinFillFraction,
                               kWater3DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
        if (ImGui::SliderFloat("Fill height", &ui.config.initial_fill_height,
                               kWater3DMinFillFraction, kWater3DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
        if (ImGui::SliderFloat("Fill depth", &ui.config.initial_fill_depth, kWater3DMinFillFraction,
                               kWater3DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
        bool fill_center_changed = false;
        fill_center_changed |= ImGui::SliderFloat(
            "Fill center X", &ui.config.initial_fill_center[0], 0.05F, 0.95F, "%.2f");
        fill_center_changed |= ImGui::SliderFloat(
            "Fill center Z", &ui.config.initial_fill_center[1], 0.05F, 0.95F, "%.2f");
        if (fill_center_changed) {
            reset_simulation(ui);
        }
        ImGui::SliderFloat3("Domain scale", ui.config.domain.scale.data(), 0.25F, 3.0F, "%.2f");
    }

    if (section("Sources and forces", true)) {
        ImGui::SeparatorText("Hose");
        ImGui::PushID("hose");
        ImGui::Checkbox("Enabled", &ui.config.hose.enabled);
        ImGui::SliderFloat3("Position", ui.config.hose.position.data(), 0.02F, 0.98F, "%.2f");
        ImGui::SliderFloat("Yaw", &ui.config.hose.yaw_degrees, -180.0F, 180.0F, "%.1f deg");
        ImGui::SliderFloat("Pitch", &ui.config.hose.pitch_degrees, -70.0F, 20.0F, "%.1f deg");
        ImGui::SliderFloat("Speed", &ui.config.hose.speed, 0.2F, 6.0F, "%.2f");
        ImGui::SliderFloat("Radius", &ui.config.hose.radius, 0.004F, 0.080F, "%.3f");
        ImGui::SliderFloat("Rate", &ui.config.hose.particles_per_second, 0.0F, 60000.0F, "%.0f/s");
        ImGui::SliderFloat("Spread", &ui.config.hose.spread_degrees, 0.0F, 45.0F, "%.1f deg");
        ImGui::PopID();

        ImGui::SeparatorText("Drain");
        ImGui::PushID("drain");
        ImGui::Checkbox("Enabled", &ui.config.drain.enabled);
        ImGui::SliderFloat3("Center", ui.config.drain.center.data(), 0.02F, 0.98F, "%.2f");
        ImGui::SliderFloat3("Half size", ui.config.drain.half_size.data(), 0.005F, 0.35F, "%.3f");
        ImGui::SliderFloat("Pull speed", &ui.config.drain.pull_speed, 0.0F, 6.0F, "%.2f");
        ImGui::SliderFloat("Pull radius", &ui.config.drain.pull_radius, 0.05F, 1.5F, "%.2f");
        ImGui::PopID();

        ImGui::SeparatorText("Wave");
        ImGui::PushID("wave");
        ImGui::Checkbox("Enabled", &ui.config.wave.enabled);
        ImGui::SliderFloat3("Center", ui.config.wave.center.data(), 0.02F, 0.98F, "%.2f");
        ImGui::SliderFloat3("Half size", ui.config.wave.half_size.data(), 0.01F, 0.55F, "%.2f");
        ImGui::SliderFloat("Amplitude", &ui.config.wave.amplitude, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Frequency", &ui.config.wave.frequency_hz, 0.0F, 2.0F, "%.2f Hz");
        ImGui::PopID();

        ImGui::SeparatorText("Rain");
        ImGui::PushID("rain");
        ImGui::Checkbox("Enabled", &ui.config.rain.enabled);
        ImGui::SliderFloat3("Center", ui.config.rain.center.data(), 0.02F, 0.98F, "%.2f");
        ImGui::SliderFloat3("Half size", ui.config.rain.half_size.data(), 0.005F, 0.50F, "%.2f");
        ImGui::SliderFloat("Speed", &ui.config.rain.speed, 0.2F, 6.0F, "%.2f");
        ImGui::SliderFloat("Radius", &ui.config.rain.radius, 0.002F, 0.060F, "%.3f");
        ImGui::SliderFloat("Rate", &ui.config.rain.particles_per_second, 0.0F, 30000.0F, "%.0f/s");
        ImGui::SliderFloat("Spread", &ui.config.rain.spread_degrees, 0.0F, 30.0F, "%.1f deg");
        ImGui::PopID();
    }

    if (section("Surface and lighting", false)) {
        ImGui::SliderFloat("Particle radius", &ui.config.particle_radius, 0.004F, 0.040F, "%.4f");
        ImGui::SliderFloat("Surface thickness", &ui.config.surface_thickness_scale, 0.1F, 4.0F,
                           "%.2f");
        ImGui::SliderFloat("Surface fill px", &ui.config.surface_gap_fill_radius_px, 0.0F, 3.0F,
                           "%.1f");
        ImGui::SliderFloat("Surface smooth world", &ui.config.surface_smoothing_radius_world, 0.0F,
                           0.04F, "%.3f");
        int surface_smoothing_iterations = static_cast<int>(ui.config.surface_smoothing_iterations);
        if (ImGui::SliderInt("Surface smooth passes", &surface_smoothing_iterations, 0, 8)) {
            ui.config.surface_smoothing_iterations =
                static_cast<std::uint32_t>(surface_smoothing_iterations);
        }
        ImGui::SliderFloat("Surface depth sigma", &ui.config.surface_depth_sigma, 0.005F, 0.120F,
                           "%.3f");
        ImGui::SliderFloat("Thickness smoothing", &ui.config.surface_thickness_smoothing, 0.0F,
                           1.0F, "%.2f");
        ImGui::SliderFloat("Surface absorption", &ui.config.surface_absorption, 0.0F, 5.0F, "%.2f");
        ImGui::SliderFloat("Surface refraction", &ui.config.surface_refraction_strength, 0.0F,
                           0.12F, "%.3f");
        ImGui::SliderFloat("Environment intensity", &ui.config.environment_intensity, 0.0F, 4.0F,
                           "%.2f");
        ImGui::SliderFloat("Environment rotation", &ui.config.environment_rotation_degrees, -180.0F,
                           180.0F, "%.0f deg");
        ImGui::SliderFloat("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Slice depth", &ui.config.slice_depth, 0.02F, 0.98F, "%.2f");
    }

    if (section("Foam and whitewater", false)) {
        ImGui::SliderFloat("Foam amount", &ui.config.foam_amount, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Foam sharpness", &ui.config.foam_sharpness, 0.2F, 4.0F, "%.2f");
        ImGui::Checkbox("Whitewater", &ui.config.whitewater_enabled);
        ImGui::SliderFloat("Whitewater intensity", &ui.config.whitewater_intensity, 0.0F, 3.0F,
                           "%.2f");
        int whitewater_max_emit = static_cast<int>(ui.config.whitewater_max_emit_per_frame);
        if (ImGui::SliderInt("Whitewater emit/frame", &whitewater_max_emit, 0, 8192)) {
            ui.config.whitewater_max_emit_per_frame =
                static_cast<std::uint32_t>(whitewater_max_emit);
        }
        ImGui::SliderFloat("Whitewater speed", &ui.config.whitewater_speed_threshold, 0.05F, 3.5F,
                           "%.2f");
        ImGui::SliderFloat("Whitewater radius", &ui.config.whitewater_radius, 0.002F, 0.035F,
                           "%.3f");
        ImGui::SliderFloat("Whitewater lifetime", &ui.config.whitewater_lifetime, 0.15F, 5.0F,
                           "%.2f");
        ImGui::SliderFloat("Whitewater drag", &ui.config.whitewater_drag, 0.50F, 1.0F, "%.2f");
        ImGui::SliderFloat("Whitewater gravity", &ui.config.whitewater_gravity_scale, 0.0F, 1.5F,
                           "%.2f");
    }

    if (section("Diagnostics", true)) {
        ImGui::Text("Grid: %u x %u x %u", ui.config.grid_width, ui.config.grid_height,
                    ui.config.grid_depth);
        const std::uint32_t scanned_particles =
            water_3d_runtime_particle_scan_count(ui.config, ui.runtime_state);
        ImGui::Text("Particles: %u reset / %u capacity", ui.config.active_particle_count,
                    ui.config.particle_capacity);
        ImGui::Text("Compute particles: %u scanned", scanned_particles);
        ImGui::Text("Emitter pool: %u touched / %u available",
                    scanned_particles - ui.config.active_particle_count,
                    emitter_particle_pool_capacity_for_config(ui.config));
        ImGui::Text("Whitewater: %u capacity / %u max emit", ui.config.whitewater_capacity,
                    ui.config.whitewater_max_emit_per_frame);
        if (ui.latest_frame_stats.has_value()) {
            ImGui::Text("Frame: %.1f fps / %.2f ms avg (%.2f ms last)", ui.latest_frame_stats->fps,
                        ui.latest_frame_stats->frame_ms, ui.latest_frame_ms);
        } else if (ui.latest_fps > 0.0) {
            ImGui::Text("Frame: %.1f fps / %.2f ms", ui.latest_fps, ui.latest_frame_ms);
        } else {
            ImGui::TextUnformatted("Frame: collecting...");
        }

        const std::vector<cubey::vulkan::GpuPassTiming>& timings = ui.resources.latest_timings();
        if (!timings.empty()) {
            ImGui::SeparatorText("GPU timings");
            for (const cubey::vulkan::GpuPassTiming& timing : timings) {
                ImGui::Text("%s: %.3f ms", timing.label.c_str(), timing.milliseconds);
            }
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

} // namespace cubey::projects::fluid::water_3d
