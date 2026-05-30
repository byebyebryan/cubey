#include "atmosphere_ui.h"

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>

namespace cubey::projects::atmosphere {

void draw_atmosphere_ui(AtmosphereUiContext ui) {
    if (!cubey::host::begin_control_panel("Atmosphere", {.width = 390.0F})) {
        ImGui::End();
        return;
    }

    AtmospherePreset selected_preset = ui.config.preset;
    if (cubey::host::imgui_enum_combo("Preset", selected_preset, kAtmospherePresets,
                                      atmosphere_preset_name)) {
        const AtmosphereRenderView preserved_view = ui.render_view;
        ui.config = atmosphere_config_for_preset(selected_preset);
        ui.config.render_view = preserved_view;
        ui.render_view = preserved_view;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.reset_requested = true;
    }

    cubey::host::imgui_enum_combo("Render view", ui.render_view, kAtmosphereRenderViews,
                                  atmosphere_render_view_name);

    if (cubey::host::imgui_section("Sun", true)) {
        const cubey::host::ScopedImGuiId section_id("Sun");
        ImGui::SliderFloat("Elevation", &ui.config.sun_elevation_degrees, -4.0F, 90.0F, "%.1f deg");
        ImGui::SliderFloat("Azimuth", &ui.config.sun_azimuth_degrees, -180.0F, 180.0F, "%.1f deg");
        ImGui::SliderFloat("Angular radius", &ui.config.sun_angular_radius, 0.001F, 0.012F,
                           "%.4f rad");
    }

    if (cubey::host::imgui_section("Medium", true)) {
        const cubey::host::ScopedImGuiId section_id("Medium");
        ImGui::SliderFloat("Camera altitude", &ui.config.camera_altitude_km, 0.0F, 80.0F,
                           "%.2f km");
        ImGui::SliderFloat("Rayleigh density", &ui.config.rayleigh_density_scale, 0.0F, 2.0F,
                           "%.2f");
        ImGui::SliderFloat("Mie density", &ui.config.mie_density_scale, 0.0F, 5.0F, "%.2f");
        ImGui::SliderFloat("Mie anisotropy", &ui.config.mie_anisotropy, 0.0F, 0.95F, "%.2f");
        ImGui::SliderFloat("Ozone width", &ui.config.ozone_half_width_km, 4.0F, 30.0F, "%.1f km");
        ImGui::SliderFloat("Ground albedo", &ui.config.ground_albedo, 0.0F, 1.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Reference", true)) {
        const cubey::host::ScopedImGuiId section_id("Reference");
        ImGui::Checkbox("Ground reference", &ui.config.reference_geometry_enabled);
        ImGui::SliderFloat("Grid scale", &ui.config.reference_grid_km, 0.25F, 10.0F, "%.2f km");
        ImGui::SliderFloat("Grid intensity", &ui.config.reference_intensity, 0.0F, 1.5F, "%.2f");
    }

    if (cubey::host::imgui_section("Display", true)) {
        const cubey::host::ScopedImGuiId section_id("Display");
        ImGui::SliderFloat("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Diagnostics", true)) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        cubey::host::draw_frame_stats(ui.latest_frame_stats, ui.latest_fps, ui.latest_frame_ms);
        ImGui::Text("Radii: %.0f km / %.0f km", ui.config.bottom_radius_km,
                    ui.config.top_radius_km);
        ImGui::Text("Scale heights: R %.1f km / M %.1f km", ui.config.rayleigh_scale_height_km,
                    ui.config.mie_scale_height_km);
    }

    ImGui::End();
}

} // namespace cubey::projects::atmosphere
