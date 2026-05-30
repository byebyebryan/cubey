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

    if (cubey::host::imgui_section("Time", true)) {
        const cubey::host::ScopedImGuiId section_id("Time");
        SunControlMode selected_mode = ui.config.time_of_day.mode;
        if (cubey::host::imgui_enum_combo("Mode", selected_mode, kSunControlModes,
                                          sun_control_mode_name)) {
            ui.config.time_of_day.mode = selected_mode;
            ui.config.time_of_day.auto_exposure_enabled =
                selected_mode == SunControlMode::SolarClock;
        }
        ImGui::SliderFloat("Time", &ui.config.time_of_day.time_hours, 0.0F, 24.0F, "%.2f h");
        ImGui::SliderFloat("Day", &ui.config.time_of_day.day_of_year, 1.0F, 366.0F, "%.0f");
        ImGui::SliderFloat("Latitude", &ui.config.time_of_day.latitude_degrees, -90.0F, 90.0F,
                           "%.1f deg");
        ImGui::SliderFloat("Azimuth offset", &ui.config.time_of_day.azimuth_offset_degrees, -180.0F,
                           180.0F, "%.1f deg");
        ImGui::Checkbox("Play", &ui.config.time_of_day.playing);
        ImGui::SliderFloat("Speed", &ui.config.time_of_day.speed_hours_per_second, 0.0F, 6.0F,
                           "%.2f h/s");

        const SolarPosition solar_position = atmosphere_solar_position(ui.config.time_of_day);
        ImGui::Text("Solar elevation: %.1f deg", solar_position.elevation_degrees);
        ImGui::Text("Solar azimuth: %.1f deg", solar_position.azimuth_degrees);
    }

    if (cubey::host::imgui_section("Sun", true)) {
        const cubey::host::ScopedImGuiId section_id("Sun");
        if (ui.config.time_of_day.mode == SunControlMode::SolarClock) {
            ImGui::BeginDisabled();
        }
        ImGui::SliderFloat("Elevation", &ui.config.sun_elevation_degrees, -90.0F, 90.0F,
                           "%.1f deg");
        ImGui::SliderFloat("Azimuth", &ui.config.sun_azimuth_degrees, -180.0F, 180.0F, "%.1f deg");
        if (ui.config.time_of_day.mode == SunControlMode::SolarClock) {
            ImGui::EndDisabled();
        }
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

    if (cubey::host::imgui_section("Night sky", true)) {
        const cubey::host::ScopedImGuiId section_id("Night sky");
        cubey::host::imgui_enum_combo("Source", ui.config.night_sky.source, kNightSkySources,
                                      night_sky_source_name);
        cubey::host::imgui_enum_combo("Mode", ui.config.night_sky.visual_mode,
                                      kNightSkyVisualModes, night_sky_visual_mode_name);
        ImGui::SliderFloat("Twilight", &ui.config.night_sky.twilight_strength, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Horizon warmth", &ui.config.night_sky.twilight_horizon_warmth, 0.0F,
                           2.0F, "%.2f");
        ImGui::SliderFloat("Stars", &ui.config.night_sky.star_intensity, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Star density", &ui.config.night_sky.star_density, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Milky Way", &ui.config.night_sky.milky_way_intensity, 0.0F, 4.0F,
                           "%.2f");
        ImGui::SliderFloat("MW contrast", &ui.config.night_sky.milky_way_contrast, 0.0F, 4.0F,
                           "%.2f");
        ImGui::SliderFloat("Light pollution", &ui.config.night_sky.light_pollution, 0.0F, 1.0F,
                           "%.2f");
        ImGui::SliderFloat("MW variation", &ui.config.night_sky.procedural_variation, 0.0F, 16.0F,
                           "%.1f");
    }

    if (cubey::host::imgui_section("Moon", true)) {
        const cubey::host::ScopedImGuiId section_id("Moon");
        ImGui::Checkbox("Moon", &ui.config.moon.enabled);
        ImGui::SliderFloat("Disk", &ui.config.moon.disk_intensity, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Moonlight", &ui.config.moon.moonlight_intensity, 0.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Phase offset", &ui.config.moon.phase_offset_days, 0.0F, 29.530588F,
                           "%.2f d");
        ImGui::SliderFloat("Size", &ui.config.moon.angular_radius_scale, 0.25F, 8.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Display", true)) {
        const cubey::host::ScopedImGuiId section_id("Display");
        ImGui::Checkbox("Auto exposure", &ui.config.time_of_day.auto_exposure_enabled);
        ImGui::SliderFloat("Exposure bias", &ui.config.time_of_day.exposure_bias, -4.0F, 4.0F,
                           "%.2f");
        if (ui.config.time_of_day.auto_exposure_enabled) {
            ImGui::Text("Exposure: %.2f", ui.config.exposure);
        } else {
            ImGui::SliderFloat("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f");
        }
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
