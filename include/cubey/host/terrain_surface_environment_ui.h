#pragma once
#include <cubey/engine/terrain_surface_environment.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/cloud_environment_ui.h>
#include <imgui.h>

namespace cubey::host {
inline void draw_terrain_surface_environment_controls(TerrainSurfaceEnvironment& environment) {
    ImGui::TextWrapped("Rendering only: environment time and weather do not change water, rainfall "
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
    if (ImGui::SliderFloat("Time of day (hours)", &atmosphere.environment.time_of_day.time_hours,
                           0.0F, 24.0F))
        atmosphere.solar_time_enabled = true;
    if (ImGui::Checkbox("Cycle day/night", &atmosphere.time_playing))
        atmosphere.solar_time_enabled = true;
    ImGui::SliderFloat("Hours per wall second", &atmosphere.time_speed_hours_per_second, 0.0F,
                       1.0F);
    ImGui::Checkbox("Cloud background and reflections", &environment.clouds().enabled);
    if (environment.clouds().enabled) {
        ImGui::Checkbox("Visible background clouds", &environment.cloud_background());
        ImGui::Checkbox("Cached cloudy reflections", &environment.cloud_reflections());
        ImGui::SliderFloat("Approximate cloud shadows", &environment.shadow_strength(), 0.0F, 1.0F);
    }
    ImGui::TextDisabled(
        "Clouds are background-only. Shadows use a bounded receiver-plane approximation.");
    if (ImGui::TreeNode("Advanced shared environment controls")) {
        (void)draw_atmosphere_environment_controls(atmosphere, {.default_open = false});
        (void)draw_cloud_environment_controls(
            environment.clouds(), {.default_open = false, .scene_depth_occlusion_enabled = true});
        ImGui::TreePop();
    }
}
} // namespace cubey::host
