#include "ocean_ui.h"

#include "ocean_mesh.h"

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace cubey::projects::ocean {
namespace {

void draw_spectrum_resolution_combo(OceanConfig& config) {
    constexpr std::array<std::uint32_t, 4> kResolutions{64, 128, 256, 512};
    const char* preview = "256";
    switch (config.spectrum_resolution) {
    case 64:
        preview = "64";
        break;
    case 128:
        preview = "128";
        break;
    case 256:
        preview = "256";
        break;
    case 512:
        preview = "512";
        break;
    default:
        preview = "custom";
        break;
    }
    if (ImGui::BeginCombo("Spectrum res", preview)) {
        for (std::uint32_t resolution : kResolutions) {
            const bool selected = config.spectrum_resolution == resolution;
            char label[16]{};
            std::snprintf(label, sizeof(label), "%u", resolution);
            if (ImGui::Selectable(label, selected)) {
                config.spectrum_resolution = resolution;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

void draw_ocean_ui(OceanUiContext ui) {
    if (!cubey::host::begin_control_panel("Ocean", {.width = 390.0F})) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Paused", &ui.paused);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.reset_requested = true;
    }
    cubey::host::imgui_enum_combo("Render view", ui.render_view, kOceanRenderViews,
                                  ocean_render_view_name);

    if (cubey::host::imgui_section("Scale", true)) {
        const cubey::host::ScopedImGuiId section_id("Scale");
        int mesh_cells = static_cast<int>(ui.config.mesh_cells);
        if (ImGui::SliderInt("Base cells", &mesh_cells, static_cast<int>(kOceanMinMeshCells),
                             static_cast<int>(kOceanMaxMeshCells))) {
            ui.config.mesh_cells = static_cast<std::uint32_t>(
                std::clamp(mesh_cells, static_cast<int>(kOceanMinMeshCells),
                           static_cast<int>(kOceanMaxMeshCells)));
        }
        int mesh_lod_levels = static_cast<int>(ui.config.mesh_lod_levels);
        if (ImGui::SliderInt("LOD levels", &mesh_lod_levels,
                             static_cast<int>(kOceanMinMeshLodLevels),
                             static_cast<int>(kOceanMaxMeshLodLevels))) {
            ui.config.mesh_lod_levels = static_cast<std::uint32_t>(
                std::clamp(mesh_lod_levels, static_cast<int>(kOceanMinMeshLodLevels),
                           static_cast<int>(kOceanMaxMeshLodLevels)));
        }
        ImGui::SliderFloat("Extent", &ui.config.mesh_extent, 400.0F, 9000.0F, "%.0f");
        ImGui::SliderFloat("Horizon fog", &ui.config.horizon_fog, 0.0F, 1.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Waves", true)) {
        const cubey::host::ScopedImGuiId section_id("Waves");
        ImGui::SliderFloat("Wind direction", &ui.config.wind_direction_degrees, -180.0F, 180.0F,
                           "%.0f deg");
        ImGui::SliderFloat("Wind speed", &ui.config.wind_speed_mps, 2.0F, 32.0F, "%.1f m/s");
        ImGui::SliderFloat("Animation speed", &ui.config.animation_speed, 0.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Amplitude", &ui.config.wave_amplitude, 0.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Swell scale", &ui.config.swell_scale, 0.35F, 2.5F, "%.2f");
        ImGui::SliderFloat("Swell overlay", &ui.config.macro_swell, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Chop", &ui.config.chop, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Spectral geometry", &ui.config.spectral_geometry, 0.0F, 1.5F,
                           "%.2f");
        ImGui::SliderFloat("Normal strength", &ui.config.normal_strength, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Detail chop", &ui.config.detail_chop, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Detail spread", &ui.config.detail_spread, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Detail geometry", &ui.config.detail_geometry, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Crest sharpness", &ui.config.crest_sharpness, 0.0F, 1.5F, "%.2f");
    }

    if (cubey::host::imgui_section("Spectrum", true)) {
        const cubey::host::ScopedImGuiId section_id("Spectrum");
        draw_spectrum_resolution_combo(ui.config);
        ImGui::SliderFloat("Near patch", &ui.config.spectrum_patch_length_near, 8.0F, 256.0F,
                           "%.0f m");
        ImGui::SliderFloat("Mid patch", &ui.config.spectrum_patch_length_mid, 16.0F, 1024.0F,
                           "%.0f m");
        ImGui::SliderFloat("Far patch", &ui.config.spectrum_patch_length_far, 32.0F, 4096.0F,
                           "%.0f m");
        ui.config.spectrum_patch_length_mid = std::max(ui.config.spectrum_patch_length_mid,
                                                       ui.config.spectrum_patch_length_near + 1.0F);
        ui.config.spectrum_patch_length_far = std::max(ui.config.spectrum_patch_length_far,
                                                       ui.config.spectrum_patch_length_mid + 1.0F);
        ImGui::SliderFloat("Energy", &ui.config.spectrum_energy, 0.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Fetch", &ui.config.fetch_km, 10.0F, 1200.0F, "%.0f km");
        ImGui::SliderFloat("Spread", &ui.config.spectrum_spread, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Short waves", &ui.config.small_wave_detail, 0.0F, 1.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Shading", true)) {
        const cubey::host::ScopedImGuiId section_id("Shading");
        ImGui::SliderFloat("Foam amount", &ui.config.foam_amount, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam threshold", &ui.config.foam_threshold, 0.1F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam generation", &ui.config.foam_generation, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Foam breakup", &ui.config.foam_breakup, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Foam coverage", &ui.config.foam_coverage, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam dispersion", &ui.config.foam_dispersion, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Foam decay", &ui.config.foam_decay, 0.0F, 1.0F, "%.3f");
        ImGui::SliderFloat("Foam drift", &ui.config.foam_drift, 0.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Absorption", &ui.config.absorption, 0.0F, 0.35F, "%.3f");
        ImGui::SliderFloat("Refraction px", &ui.config.refraction_pixels, 0.0F, 36.0F, "%.1f");
        ImGui::SliderFloat("Water opacity", &ui.config.water_opacity, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Scattering", &ui.config.scattering_strength, 0.0F, 1.5F, "%.2f");
        ImGui::Checkbox("Seafloor", &ui.config.seafloor_enabled);
        ImGui::SliderFloat("Seafloor depth", &ui.config.seafloor_depth, 2.0F, 80.0F, "%.1f m");
        ImGui::SliderFloat("Seafloor brightness", &ui.config.seafloor_brightness, 0.0F, 2.0F,
                           "%.2f");
        ImGui::SliderFloat("Seafloor detail", &ui.config.seafloor_detail, 0.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Interaction hooks", false)) {
        const cubey::host::ScopedImGuiId section_id("Interaction hooks");
        ImGui::SliderFloat("Shoreline influence", &ui.config.shoreline_influence, 0.0F, 1.0F,
                           "%.2f");
        ImGui::SliderFloat("Disturbance radius", &ui.config.disturbance_radius, 4.0F, 160.0F,
                           "%.1f");
        ImGui::SliderFloat("Disturbance strength", &ui.config.disturbance_strength, 0.0F, 1.5F,
                           "%.2f");
    }

    if (cubey::host::imgui_section("Diagnostics", true)) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        cubey::host::draw_frame_stats(ui.latest_frame_stats, ui.latest_fps, ui.latest_frame_ms);
        ImGui::Text("Mesh: %u LOD / %u patches / %u tris", ui.config.mesh_lod_levels,
                    ocean_mesh_patch_count(ui.config), ocean_mesh_total_triangle_count(ui.config));
        ImGui::Text("Near cell: %.2f m", ocean_mesh_near_cell_size(ui.config));
    }

    ImGui::End();
}

} // namespace cubey::projects::ocean
