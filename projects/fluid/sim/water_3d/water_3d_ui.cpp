#include "water_3d_ui.h"

#include "water_3d_gpu_resources.h"

#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/cloud_environment_ui.h>
#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

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

void reset_simulation(Water3DUiContext& ui) {
    ui.reset_requested = true;
    ui.runtime_state = {};
}

} // namespace

Water3DUiResult draw_water_3d_ui(Water3DUiContext ui) {
    Water3DUiResult result;
    if (!cubey::host::begin_control_panel(ui.title)) {
        ImGui::End();
        return result;
    }

    cubey::host::imgui_checkbox("Paused", &ui.paused, "Pause water simulation time.");
    ImGui::SameLine();
    if (cubey::host::imgui_button("Reset",
                                  "Restart the water simulation from the current setup.")) {
        reset_simulation(ui);
    }

    cubey::host::imgui_enum_combo("Render view", ui.render_view, kRenderViews,
                                  water_3d_render_view_name);

    ImGui::Spacing();
    if (const cubey::host::ScopedImGuiGroup group{
            "Simulation", {.help = "FLIP/PIC transfer, pressure, damping, and boundary controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Simulation");
        cubey::host::imgui_enum_combo("Transfer", ui.config.transfer_mode, kTransferModes,
                                      water_3d_transfer_mode_name);

        cubey::host::imgui_slider_uint32("Pressure iterations", &ui.config.pressure_iterations, 1U,
                                         128U);
        cubey::host::imgui_slider_uint32("Substeps", &ui.config.substeps, 1U, 4U);
        cubey::host::imgui_slider_float("PIC/FLIP blend", &ui.config.flip_ratio, 0.0F, 1.0F,
                                        "%.2f");
        cubey::host::imgui_slider_float("Velocity limit", &ui.config.velocity_limit, 1.0F, 8.0F,
                                        "%.2f");
        cubey::host::imgui_slider_float("Particle damping", &ui.config.particle_damping, 0.980F,
                                        1.000F, "%.3f");
        cubey::host::imgui_slider_float("Particle volume strength",
                                        &ui.config.particle_volume_strength, 0.0F, 48.0F, "%.1f");
        cubey::host::imgui_slider_uint32("Transfer limit/cell", &ui.config.max_particles_per_cell,
                                         8U, 256U);
        cubey::host::imgui_slider_float("Gravity", &ui.config.gravity, -4.0F, 0.0F, "%.2f");
        cubey::host::imgui_slider_float("Boundary bounce", &ui.config.boundary_restitution, 0.0F,
                                        0.8F, "%.2f");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Initial volume",
            {.default_open = false, .help = "Initial particle fill dimensions for reset."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Initial volume");
        if (cubey::host::imgui_slider_float("Fill width", &ui.config.initial_fill_width,
                                            kWater3DMinFillFraction, kWater3DMaxFillFraction,
                                            "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
        if (cubey::host::imgui_slider_float("Fill height", &ui.config.initial_fill_height,
                                            kWater3DMinFillFraction, kWater3DMaxFillFraction,
                                            "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
        if (cubey::host::imgui_slider_float("Fill depth", &ui.config.initial_fill_depth,
                                            kWater3DMinFillFraction, kWater3DMaxFillFraction,
                                            "%.2f")) {
            refresh_particle_counts(ui.config);
            reset_simulation(ui);
        }
        bool fill_center_changed = false;
        fill_center_changed |= cubey::host::imgui_slider_float(
            "Fill center X", &ui.config.initial_fill_center[0], 0.05F, 0.95F, "%.2f");
        fill_center_changed |= cubey::host::imgui_slider_float(
            "Fill center Z", &ui.config.initial_fill_center[1], 0.05F, 0.95F, "%.2f");
        if (fill_center_changed) {
            reset_simulation(ui);
        }
        cubey::host::imgui_slider_float3("Domain scale", ui.config.domain.scale.data(), 0.25F, 3.0F,
                                         "%.2f");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Sources and forces",
            {.default_open = false,
             .help = "Particle emitters, drains, rain, and wave forcing controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Sources and forces");
        if (const cubey::host::ScopedImGuiGroup hose_group{
                "Hose",
                {.default_open = false, .level = 1U, .help = "Continuous particle emitter."}};
            hose_group) {
            const cubey::host::ScopedImGuiId hose_id("hose");
            cubey::host::imgui_checkbox("Enabled", &ui.config.hose.enabled);
            cubey::host::imgui_slider_float3("Position", ui.config.hose.position.data(), 0.02F,
                                             0.98F, "%.2f");
            cubey::host::imgui_slider_float("Yaw", &ui.config.hose.yaw_degrees, -180.0F, 180.0F,
                                            "%.1f deg");
            cubey::host::imgui_slider_float("Pitch", &ui.config.hose.pitch_degrees, -70.0F, 20.0F,
                                            "%.1f deg");
            cubey::host::imgui_slider_float("Speed", &ui.config.hose.speed, 0.2F, 6.0F, "%.2f");
            cubey::host::imgui_slider_float("Radius", &ui.config.hose.radius, 0.004F, 0.080F,
                                            "%.3f");
            cubey::host::imgui_slider_float("Rate", &ui.config.hose.particles_per_second, 0.0F,
                                            60000.0F, "%.0f/s");
            cubey::host::imgui_slider_float("Spread", &ui.config.hose.spread_degrees, 0.0F, 45.0F,
                                            "%.1f deg");
        }

        if (const cubey::host::ScopedImGuiGroup drain_group{
                "Drain",
                {.default_open = false,
                 .level = 1U,
                 .help = "Particle removal and pull-force volume."}};
            drain_group) {
            const cubey::host::ScopedImGuiId drain_id("drain");
            cubey::host::imgui_checkbox("Enabled", &ui.config.drain.enabled);
            cubey::host::imgui_slider_float3("Center", ui.config.drain.center.data(), 0.02F, 0.98F,
                                             "%.2f");
            cubey::host::imgui_slider_float3("Half size", ui.config.drain.half_size.data(), 0.005F,
                                             0.35F, "%.3f");
            cubey::host::imgui_slider_float("Pull speed", &ui.config.drain.pull_speed, 0.0F, 6.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Pull radius", &ui.config.drain.pull_radius, 0.05F,
                                            1.5F, "%.2f");
        }

        if (const cubey::host::ScopedImGuiGroup wave_group{
                "Wave",
                {.default_open = false,
                 .level = 1U,
                 .help = "Moving force volume for surface motion."}};
            wave_group) {
            const cubey::host::ScopedImGuiId wave_id("wave");
            cubey::host::imgui_checkbox("Enabled", &ui.config.wave.enabled);
            cubey::host::imgui_slider_float3("Center", ui.config.wave.center.data(), 0.02F, 0.98F,
                                             "%.2f");
            cubey::host::imgui_slider_float3("Half size", ui.config.wave.half_size.data(), 0.01F,
                                             0.55F, "%.2f");
            cubey::host::imgui_slider_float("Amplitude", &ui.config.wave.amplitude, 0.0F, 4.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Frequency", &ui.config.wave.frequency_hz, 0.0F, 2.0F,
                                            "%.2f Hz");
        }

        if (const cubey::host::ScopedImGuiGroup rain_group{
                "Rain",
                {.default_open = false,
                 .level = 1U,
                 .help = "Volume emitter for falling particles."}};
            rain_group) {
            const cubey::host::ScopedImGuiId rain_id("rain");
            cubey::host::imgui_checkbox("Enabled", &ui.config.rain.enabled);
            cubey::host::imgui_slider_float3("Center", ui.config.rain.center.data(), 0.02F, 0.98F,
                                             "%.2f");
            cubey::host::imgui_slider_float3("Half size", ui.config.rain.half_size.data(), 0.005F,
                                             0.50F, "%.2f");
            cubey::host::imgui_slider_float("Speed", &ui.config.rain.speed, 0.2F, 6.0F, "%.2f");
            cubey::host::imgui_slider_float("Radius", &ui.config.rain.radius, 0.002F, 0.060F,
                                            "%.3f");
            cubey::host::imgui_slider_float("Rate", &ui.config.rain.particles_per_second, 0.0F,
                                            30000.0F, "%.0f/s");
            cubey::host::imgui_slider_float("Spread", &ui.config.rain.spread_degrees, 0.0F, 30.0F,
                                            "%.1f deg");
        }
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Surface and lighting",
            {.default_open = false,
             .help = "Screen-space surface reconstruction, lighting, and debug slice controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Surface and lighting");
        cubey::host::imgui_slider_float("Particle radius", &ui.config.particle_radius, 0.004F,
                                        0.040F, "%.4f");
        cubey::host::imgui_slider_float("Particle max px",
                                        &ui.config.surface_particle_max_radius_px, 4.0F,
                                        kWater3DSurfaceMaxParticleRadiusPx, "%.0f");
        cubey::host::imgui_slider_float("Surface thickness", &ui.config.surface_thickness_scale,
                                        0.1F, 4.0F, "%.2f");
        cubey::host::imgui_slider_float("Surface fill px", &ui.config.surface_gap_fill_radius_px,
                                        0.0F, 3.0F, "%.1f");
        cubey::host::imgui_slider_float(
            "Surface smooth world", &ui.config.surface_smoothing_radius_world, 0.0F, 0.04F, "%.3f");
        cubey::host::imgui_slider_float("Surface smooth max px",
                                        &ui.config.surface_smoothing_max_radius_px, 1.0F,
                                        kWater3DSurfaceMaxSmoothRadiusPx, "%.0f");
        cubey::host::imgui_slider_uint32("Surface smooth passes",
                                         &ui.config.surface_smoothing_iterations, 0U, 8U);
        cubey::host::imgui_slider_float("Surface depth sigma", &ui.config.surface_depth_sigma,
                                        0.005F, 0.120F, "%.3f");
        cubey::host::imgui_slider_float("Thickness smoothing",
                                        &ui.config.surface_thickness_smoothing, 0.0F, 1.0F, "%.2f");
        cubey::host::imgui_slider_float("Surface absorption", &ui.config.surface_absorption, 0.0F,
                                        5.0F, "%.2f");
        cubey::host::imgui_slider_float(
            "Surface refraction", &ui.config.surface_refraction_strength, 0.0F, 0.12F, "%.3f");
        cubey::host::imgui_slider_float("Environment intensity", &ui.config.environment_intensity,
                                        0.0F, 4.0F, "%.2f");
        cubey::host::imgui_slider_float("Environment rotation",
                                        &ui.config.environment_rotation_degrees, -180.0F, 180.0F,
                                        "%.0f deg");
        cubey::host::imgui_slider_float("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f");
        cubey::host::imgui_slider_float("Slice depth", &ui.config.slice_depth, 0.02F, 0.98F,
                                        "%.2f");
    }

    if (ui.atmosphere != nullptr) {
        result.atmosphere_changed |= cubey::host::draw_atmosphere_environment_controls(
            *ui.atmosphere,
            {.label = "Environment",
             .default_open = false,
             .help = "Shared procedural atmosphere driving water lighting, reflection, and "
                     "exposure."});
    }
    if (ui.clouds != nullptr) {
        result.clouds_changed |= cubey::host::draw_cloud_environment_controls(
            *ui.clouds,
            {.label = "Cloud Environment",
             .default_open = false,
             .help = "Shared surface clouds composed behind refractive water and cached for PBR "
                     "environment reflections.",
             .enabled_help = "Render direct clouds and include them in water environment "
                             "reflections."});
    }
    if (ui.terrain != nullptr) {
        if (const cubey::host::ScopedImGuiGroup group{
                "Terrain Backdrop",
                {.default_open = false,
                 .help = "Shared terrain rendered behind the simulation tank."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Terrain Backdrop");
            cubey::host::imgui_checkbox("Visible", &ui.terrain->visible);
            cubey::host::imgui_slider_float("Foreground height", &ui.terrain->foreground_height_m,
                                            0.25F, 200.0F, "%.1f m");
            const bool filtered =
                ui.terrain->material == cubey::render::TerrainBackdropMaterialMode::FilteredDetail;
            int material = filtered ? 1 : 0;
            if (ImGui::RadioButton("Flat", &material, 0)) {
                ui.terrain->material = cubey::render::TerrainBackdropMaterialMode::Flat;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Filtered detail", &material, 1)) {
                ui.terrain->material = cubey::render::TerrainBackdropMaterialMode::FilteredDetail;
            }
            cubey::host::imgui_checkbox("Shadows", &ui.terrain->shadows);
        }
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Foam and whitewater",
            {.default_open = false, .help = "Foam shading and whitewater particle controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Foam and whitewater");
        cubey::host::imgui_slider_float("Foam amount", &ui.config.foam_amount, 0.0F, 1.0F, "%.2f");
        cubey::host::imgui_slider_float("Foam sharpness", &ui.config.foam_sharpness, 0.2F, 4.0F,
                                        "%.2f");
        cubey::host::imgui_checkbox("Whitewater", &ui.config.whitewater_enabled);
        cubey::host::imgui_slider_float("Whitewater intensity", &ui.config.whitewater_intensity,
                                        0.0F, 3.0F, "%.2f");
        cubey::host::imgui_slider_uint32("Whitewater emit/frame",
                                         &ui.config.whitewater_max_emit_per_frame, 0U, 8192U);
        cubey::host::imgui_slider_float("Whitewater speed", &ui.config.whitewater_speed_threshold,
                                        0.05F, 3.5F, "%.2f");
        cubey::host::imgui_slider_float("Whitewater radius", &ui.config.whitewater_radius, 0.002F,
                                        0.035F, "%.3f");
        cubey::host::imgui_slider_float("Whitewater blur px", &ui.config.whitewater_blur_radius_px,
                                        0.0F, kWater3DWhitewaterMaxBlurPx, "%.2f");
        cubey::host::imgui_slider_float("Whitewater lifetime", &ui.config.whitewater_lifetime,
                                        0.15F, 5.0F, "%.2f");
        cubey::host::imgui_slider_float("Whitewater drag", &ui.config.whitewater_drag, 0.50F, 1.0F,
                                        "%.2f");
        cubey::host::imgui_slider_float("Whitewater gravity", &ui.config.whitewater_gravity_scale,
                                        0.0F, 1.5F, "%.2f");
    }

    const std::uint32_t scanned_particles =
        water_3d_runtime_particle_scan_count(ui.config, ui.runtime_state);
    const std::uint32_t touched_emitter_particles =
        scanned_particles > ui.config.active_particle_count
            ? scanned_particles - ui.config.active_particle_count
            : 0U;
    const std::array<cubey::host::PerformanceCounter, 5> performance_counters{
        cubey::host::PerformanceCounter{"Reset particles", ui.config.active_particle_count,
                                        nullptr},
        cubey::host::PerformanceCounter{"Compute particles", scanned_particles, nullptr},
        cubey::host::PerformanceCounter{"Grid voxels",
                                        static_cast<std::uint64_t>(ui.config.grid_width) *
                                            static_cast<std::uint64_t>(ui.config.grid_height) *
                                            static_cast<std::uint64_t>(ui.config.grid_depth),
                                        nullptr},
        cubey::host::PerformanceCounter{"Emitter pool", touched_emitter_particles, nullptr},
        cubey::host::PerformanceCounter{"Whitewater capacity", ui.config.whitewater_capacity,
                                        nullptr},
    };
    cubey::host::PerformanceUiContext performance = ui.performance;
    performance.owned_gpu_bytes = ui.resources.allocated_buffer_bytes();
    performance.owned_gpu_label = "Water GPU buffers";
    performance.counters = performance_counters;
    performance.gpu_timings = ui.resources.latest_timings();
    cubey::host::draw_performance_ui(performance);

    if (const cubey::host::ScopedImGuiGroup group{
            "Diagnostics",
            {.default_open = false,
             .help = "Read-only particle, frame, and GPU-memory statistics."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        ImGui::Text("Grid: %u x %u x %u", ui.config.grid_width, ui.config.grid_height,
                    ui.config.grid_depth);
        ImGui::Text("Particles: %u reset / %u capacity", ui.config.active_particle_count,
                    ui.config.particle_capacity);
        ImGui::Text("Compute particles: %u scanned", scanned_particles);
        ImGui::Text("Emitter pool: %u touched / %u available", touched_emitter_particles,
                    emitter_particle_pool_capacity_for_config(ui.config));
        ImGui::Text("Whitewater: %u capacity / %u max emit", ui.config.whitewater_capacity,
                    ui.config.whitewater_max_emit_per_frame);
    }
    ImGui::End();
    return result;
}

} // namespace cubey::projects::fluid::water_3d
