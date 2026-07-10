#include "atmosphere_ui.h"

#include <cubey/host/cloud_environment_ui.h>
#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

namespace cubey::projects::atmosphere {
namespace {

[[nodiscard]] bool has_loading_status(const AtmosphereLoadingStatus& status) {
    return status.moon_pending || status.night_sky_pending || status.moon_placeholder ||
           status.night_sky_placeholder || !status.moon_error.empty() ||
           !status.night_sky_error.empty();
}

void draw_loading_overlay(const AtmosphereLoadingStatus& status) {
    if (!has_loading_status(status)) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 position{
        viewport->WorkPos.x + viewport->WorkSize.x - 18.0F,
        viewport->WorkPos.y + 18.0F,
    };
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2{1.0F, 0.0F});
    ImGui::SetNextWindowBgAlpha(0.84F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("Atmosphere asset status", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Atmosphere assets");
    if (status.moon_pending) {
        ImGui::BulletText("Generating moon surface");
    } else if (status.moon_placeholder) {
        ImGui::BulletText("Moon surface placeholder");
    }
    if (status.night_sky_pending) {
        ImGui::BulletText("Generating night sky atlas");
    } else if (status.night_sky_placeholder) {
        ImGui::BulletText("Night sky placeholder");
    }
    if (!status.moon_error.empty()) {
        ImGui::TextColored(ImVec4{1.0F, 0.42F, 0.32F, 1.0F}, "%s", status.moon_error.c_str());
    }
    if (!status.night_sky_error.empty()) {
        ImGui::TextColored(ImVec4{1.0F, 0.42F, 0.32F, 1.0F}, "%s", status.night_sky_error.c_str());
    }
    ImGui::End();
}

} // namespace

void draw_atmosphere_ui(AtmosphereUiContext ui) {
    if (!cubey::host::begin_control_panel("Atmosphere", {.width = 390.0F})) {
        ImGui::End();
        draw_loading_overlay(ui.loading_status);
        return;
    }

    AtmospherePreset selected_preset = ui.config.preset;
    if (cubey::host::imgui_enum_combo(
            "Preset", selected_preset, kAtmospherePresets, atmosphere_preset_name,
            "Replace the editable atmosphere state with a known preset.")) {
        const AtmosphereRenderView preserved_view = ui.render_view;
        ui.config = atmosphere_config_for_preset(selected_preset);
        ui.config.render_view = preserved_view;
        ui.render_view = preserved_view;
    }
    ImGui::SameLine();
    if (cubey::host::imgui_button("Reset",
                                  "Restart atmosphere time and rebuild generated atlases.")) {
        ui.reset_requested = true;
    }

    cubey::host::imgui_enum_combo(
        "Render view", ui.render_view, kAtmosphereRenderViews, atmosphere_render_view_name,
        "Inspect final sky, transmittance, scattering, night sky, or lighting buffers.");

    if (const cubey::host::ScopedImGuiGroup group{
            "Time", {.help = "Solar-clock controls for dynamic day and night lighting."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Time");
        SunControlMode selected_mode = ui.config.time_of_day.mode;
        if (cubey::host::imgui_enum_combo("Mode", selected_mode, kSunControlModes,
                                          sun_control_mode_name,
                                          "Manual uses sun angles directly; solar computes them "
                                          "from time, date, and latitude.")) {
            ui.config.time_of_day.mode = selected_mode;
        }
        cubey::host::imgui_slider_float("Time", &ui.config.time_of_day.time_hours, 0.0F, 24.0F,
                                        "%.2f h", "Local solar-clock time in hours.");
        cubey::host::imgui_slider_float("Day", &ui.config.time_of_day.day_of_year, 1.0F, 366.0F,
                                        "%.0f", "Day of year used by the solar position model.");
        cubey::host::imgui_slider_float("Latitude", &ui.config.time_of_day.latitude_degrees, -90.0F,
                                        90.0F, "%.1f deg", "Observer latitude used by solar mode.");
        cubey::host::imgui_slider_float(
            "Azimuth offset", &ui.config.time_of_day.azimuth_offset_degrees, -180.0F, 180.0F,
            "%.1f deg", "Rotation offset applied to the computed solar azimuth.");
        cubey::host::imgui_checkbox("Play", &ui.config.time_of_day.playing,
                                    "Advance time every frame.");
        cubey::host::imgui_slider_float("Speed", &ui.config.time_of_day.speed_hours_per_second,
                                        0.0F, 6.0F, "%.2f h/s",
                                        "Simulated hours advanced per real second.");

        const SolarPosition solar_position = atmosphere_solar_position(ui.config.time_of_day);
        ImGui::Text("Solar elevation: %.1f deg", solar_position.elevation_degrees);
        ImGui::Text("Solar azimuth: %.1f deg", solar_position.azimuth_degrees);
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Sun",
            {.default_open = false, .help = "Manual sun disk and light direction controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Sun");
        if (ui.config.time_of_day.mode == SunControlMode::SolarClock) {
            ImGui::BeginDisabled();
        }
        cubey::host::imgui_slider_float("Elevation", &ui.config.sun_elevation_degrees, -90.0F,
                                        90.0F, "%.1f deg",
                                        "Sun angle above the horizon in manual mode.");
        cubey::host::imgui_slider_float("Azimuth", &ui.config.sun_azimuth_degrees, -180.0F, 180.0F,
                                        "%.1f deg", "Horizontal sun direction in manual mode.");
        if (ui.config.time_of_day.mode == SunControlMode::SolarClock) {
            ImGui::EndDisabled();
        }
        cubey::host::imgui_slider_float("Angular radius", &ui.config.sun_angular_radius, 0.001F,
                                        0.012F, "%.4f rad", "Apparent size of the sun disk.");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Medium",
            {.default_open = false, .help = "Atmospheric scattering density and ground response."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Medium");
        cubey::host::imgui_slider_float("Camera altitude", &ui.config.camera_altitude_km, 0.0F,
                                        80.0F, "%.2f km", "Observer altitude above sea level.");
        cubey::host::imgui_slider_float("Camera yaw", &ui.config.camera_yaw_offset_degrees, -180.0F,
                                        180.0F, "%.1f deg",
                                        "View yaw offset from the default review direction.");
        cubey::host::imgui_slider_float("Camera pitch", &ui.config.camera_pitch_offset_degrees,
                                        -60.0F, 60.0F, "%.1f deg",
                                        "View pitch offset from the default review direction.");
        cubey::host::imgui_slider_float("Rayleigh density", &ui.config.rayleigh_density_scale, 0.0F,
                                        2.0F, "%.2f", "Multiplier for molecular sky scattering.");
        cubey::host::imgui_slider_float("Mie density", &ui.config.mie_density_scale, 0.0F, 5.0F,
                                        "%.2f", "Multiplier for aerosol scattering.");
        cubey::host::imgui_slider_float("Mie anisotropy", &ui.config.mie_anisotropy, 0.0F, 0.95F,
                                        "%.2f", "Forward-scattering bias for aerosols.");
        cubey::host::imgui_slider_float("Ozone width", &ui.config.ozone_half_width_km, 4.0F, 30.0F,
                                        "%.1f km", "Half-width of the ozone absorption layer.");
        cubey::host::imgui_slider_float("Ground albedo", &ui.config.ground_albedo, 0.0F, 1.0F,
                                        "%.2f", "Diffuse reflectance used by the ground term.");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Reference",
            {.default_open = false,
             .help = "Optional ground grid used to judge scale and horizon color."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Reference");
        cubey::host::imgui_enum_combo(
            "Ground mode", ui.config.ground_mode, kAtmosphereGroundModes,
            atmosphere_ground_mode_name,
            "Controls whether sky rays stop at the ground horizon or ignore ground occlusion for "
            "sky/cloud review.");
        cubey::host::imgui_checkbox("Ground reference", &ui.config.reference_geometry_enabled,
                                    "Show the reference grid/ground plane.");
        cubey::host::imgui_slider_float("Grid scale", &ui.config.reference_grid_km, 0.25F, 10.0F,
                                        "%.2f km", "Spacing scale for the reference ground grid.");
        cubey::host::imgui_slider_float("Grid intensity", &ui.config.reference_intensity, 0.0F,
                                        1.5F, "%.2f", "Brightness of the reference geometry.");
    }

    static_cast<void>(cubey::host::draw_cloud_environment_controls(
        ui.config.clouds,
        {.help = "Shared surface Cloud V1 layer rendered over the atmosphere background."}));

    if (const cubey::host::ScopedImGuiGroup group{
            "Night sky",
            {.default_open = false,
             .help = "Twilight, stars, Milky Way, and night-sky visibility controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Night sky");
        cubey::host::imgui_enum_combo(
            "Mode", ui.config.night_sky.visual_mode, kNightSkyVisualModes,
            night_sky_visual_mode_name,
            "Human mode keeps night features legible; camera mode is more exposure-driven.");
        cubey::host::imgui_enum_combo("Layer", ui.config.night_sky.layer, kNightSkyLayerViews,
                                      night_sky_layer_view_name,
                                      "Debug individual generated night-sky atlas layers.");
        cubey::host::imgui_slider_float("Twilight", &ui.config.night_sky.twilight_strength, 0.0F,
                                        4.0F, "%.2f", "Brightness of the twilight band.");
        cubey::host::imgui_slider_float("Horizon warmth",
                                        &ui.config.night_sky.twilight_horizon_warmth, 0.0F, 2.0F,
                                        "%.2f", "Warm color bias near the horizon.");
        cubey::host::imgui_slider_float("Stars", &ui.config.night_sky.star_intensity, 0.0F, 4.0F,
                                        "%.2f", "Brightness of procedural stars.");
        cubey::host::imgui_slider_float("Star density", &ui.config.night_sky.star_density, 0.0F,
                                        1.0F, "%.2f", "Density of procedural stars.");
        cubey::host::imgui_slider_float("Milky Way", &ui.config.night_sky.milky_way_intensity, 0.0F,
                                        4.0F, "%.2f",
                                        "Brightness of the generated Milky Way layer.");
        cubey::host::imgui_slider_float("MW contrast", &ui.config.night_sky.milky_way_contrast,
                                        0.0F, 4.0F, "%.2f", "Contrast of the Milky Way layer.");
        cubey::host::imgui_slider_float("Light pollution", &ui.config.night_sky.light_pollution,
                                        0.0F, 1.0F, "%.2f",
                                        "Amount of skyglow that suppresses stars.");
        cubey::host::imgui_slider_float("MW variation", &ui.config.night_sky.procedural_variation,
                                        0.0F, 16.0F, "%.1f",
                                        "Procedural variation seed/phase for the Milky Way.");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Moon",
            {.default_open = false, .help = "Visible moon and moonlight contribution controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Moon");
        cubey::host::imgui_checkbox("Moon", &ui.config.moon.enabled,
                                    "Enable the visible moon and moonlight.");
        cubey::host::imgui_slider_float("Moon", &ui.config.moon.disk_intensity, 0.0F, 4.0F, "%.2f",
                                        "Brightness of the visible moon.");
        cubey::host::imgui_slider_float("Moonlight", &ui.config.moon.moonlight_intensity, 0.0F,
                                        4.0F, "%.2f", "Brightness of indirect moonlight.");
        cubey::host::imgui_slider_float("Phase offset", &ui.config.moon.phase_offset_days, 0.0F,
                                        29.530588F, "%.2f d",
                                        "Offset in days through the lunar phase cycle.");
        cubey::host::imgui_slider_float("Size", &ui.config.moon.angular_radius_scale, 0.25F, 8.0F,
                                        "%.2f", "Visual scale of the moon.");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Display",
            {.default_open = false,
             .help = "Exposure controls for mapping HDR sky lighting to display."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Display");
        cubey::host::imgui_checkbox("Auto exposure", &ui.config.time_of_day.auto_exposure_enabled,
                                    "Adapt exposure across daylight, twilight, and night.");
        cubey::host::imgui_slider_float("Exposure bias", &ui.config.time_of_day.exposure_bias,
                                        -4.0F, 4.0F, "%.2f",
                                        "Bias applied to automatic exposure in stops.");
        if (ui.config.time_of_day.auto_exposure_enabled) {
            ImGui::Text("Exposure: %.2f", ui.config.exposure);
        } else {
            cubey::host::imgui_slider_float("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f",
                                            "Manual exposure in stops.");
        }
    }

    cubey::host::draw_performance_ui(ui.performance);

    if (const cubey::host::ScopedImGuiGroup group{
            "Diagnostics",
            {.default_open = false, .help = "Read-only atmosphere runtime and frame statistics."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        ImGui::Text("Radii: %.0f km / %.0f km", ui.config.bottom_radius_km,
                    ui.config.top_radius_km);
        ImGui::Text("Scale heights: R %.1f km / M %.1f km", ui.config.rayleigh_scale_height_km,
                    ui.config.mie_scale_height_km);
    }

    ImGui::End();
    draw_loading_overlay(ui.loading_status);
}

} // namespace cubey::projects::atmosphere
