#include "ocean_ui.h"

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace cubey::projects::ocean {

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
        if (ImGui::SliderInt("Mesh cells", &mesh_cells,
                             static_cast<int>(kOceanMinMeshCells),
                             static_cast<int>(kOceanMaxMeshCells))) {
            ui.config.mesh_cells = static_cast<std::uint32_t>(
                std::clamp(mesh_cells, static_cast<int>(kOceanMinMeshCells),
                           static_cast<int>(kOceanMaxMeshCells)));
        }
        ImGui::SliderFloat("Extent", &ui.config.mesh_extent, 400.0F, 6000.0F, "%.0f");
        ImGui::SliderFloat("Snap", &ui.config.mesh_snap, 1.0F, 64.0F, "%.0f");
        ImGui::SliderFloat("Horizon fog", &ui.config.horizon_fog, 0.0F, 1.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Waves", true)) {
        const cubey::host::ScopedImGuiId section_id("Waves");
        ImGui::SliderFloat("Wind direction", &ui.config.wind_direction_degrees, -180.0F, 180.0F,
                           "%.0f deg");
        ImGui::SliderFloat("Wind speed", &ui.config.wind_speed, 0.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Amplitude", &ui.config.wave_amplitude, 0.0F, 3.0F, "%.2f");
        ImGui::SliderFloat("Swell scale", &ui.config.swell_scale, 0.35F, 2.5F, "%.2f");
        ImGui::SliderFloat("Chop", &ui.config.chop, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Normal strength", &ui.config.normal_strength, 0.0F, 2.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Shading", true)) {
        const cubey::host::ScopedImGuiId section_id("Shading");
        ImGui::SliderFloat("Foam amount", &ui.config.foam_amount, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam threshold", &ui.config.foam_threshold, 0.1F, 1.5F, "%.2f");
        ImGui::SliderFloat("Absorption", &ui.config.absorption, 0.0F, 0.35F, "%.3f");
        ImGui::SliderFloat("Refraction", &ui.config.refraction_strength, 0.0F, 0.18F, "%.3f");
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
        ImGui::Text("Mesh: %u cells / %u tris", ui.config.mesh_cells,
                    ui.config.mesh_cells * ui.config.mesh_cells * 2U);
    }

    ImGui::End();
}

} // namespace cubey::projects::ocean
