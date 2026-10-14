#pragma once
#include <cubey/engine/terrain_surface_environment.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <imgui.h>

namespace cubey::host {
inline void draw_terrain_surface_environment_controls(TerrainSurfaceEnvironment& environment) {
    ImGui::TextWrapped("Rendering only: environment time does not change water, rainfall "
                       "or solver pace.");
    for (const auto* name : {"daylight", "noon", "sunset", "night"}) {
        ImGui::PushID(name);
        if (ImGui::Button(name))
            environment.select_preset(name);
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    auto& atmosphere = environment.atmosphere();
    if (!atmosphere.solar_time_enabled)
        ImGui::TextDisabled("Manual reference sun; editing solar time switches to day/night.");
    if (ImGui::SliderFloat("Solar time (hours)", &atmosphere.environment.time_of_day.time_hours,
                           0.0F, 24.0F))
        atmosphere.solar_time_enabled = true;
    if (ImGui::Checkbox("Cycle day/night", &atmosphere.time_playing))
        atmosphere.solar_time_enabled = true;
    ImGui::SliderFloat("Hours per wall second", &atmosphere.time_speed_hours_per_second, 0.0F,
                       1.0F);
    ImGui::TextDisabled(
        "Clear sky only; surface-view clouds are excluded from this elevated view.");
    if (ImGui::TreeNode("Advanced shared environment controls")) {
        (void)draw_atmosphere_environment_controls(atmosphere, {.default_open = false});
        ImGui::TreePop();
    }
}
} // namespace cubey::host
