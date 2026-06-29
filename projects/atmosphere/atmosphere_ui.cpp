#include "atmosphere_ui.h"

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>

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
            {.default_open = false,
             .help = "Atmospheric scattering density and ground response."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Medium");
        cubey::host::imgui_slider_float("Camera altitude", &ui.config.camera_altitude_km, 0.0F,
                                        80.0F, "%.2f km", "Observer altitude above sea level.");
        cubey::host::imgui_slider_float("Camera yaw", &ui.config.camera_yaw_offset_degrees,
                                        -180.0F, 180.0F, "%.1f deg",
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
        cubey::host::imgui_checkbox("Ground reference", &ui.config.reference_geometry_enabled,
                                    "Show the reference grid/ground plane.");
        cubey::host::imgui_slider_float("Grid scale", &ui.config.reference_grid_km, 0.25F, 10.0F,
                                        "%.2f km", "Spacing scale for the reference ground grid.");
        cubey::host::imgui_slider_float("Grid intensity", &ui.config.reference_intensity, 0.0F,
                                        1.5F, "%.2f", "Brightness of the reference geometry.");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Clouds",
            {.default_open = false,
             .help = "Shared cloud layer rendered over the atmosphere background."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Clouds");
        AtmosphereCloudConfig& clouds = ui.config.clouds;
        cubey::render::CloudLayerConfig& layer = clouds.layer;
        cubey::host::imgui_checkbox("Clouds", &clouds.enabled,
                                    "Composite the shared cloud layer in final view.");
        if (!clouds.enabled) {
            ImGui::BeginDisabled();
        }
        cubey::host::imgui_enum_combo("Debug view", layer.debug_view,
                                      cubey::render::kCloudLayerDebugViews,
                                      cubey::render::cloud_layer_debug_view_name,
                                      "Inspect the cloud product and diagnostic buffers.");
        if (cubey::host::imgui_enum_combo("Weather preset", clouds.weather_preset,
                                          kAtmosphereCloudWeatherPresets,
                                          atmosphere_cloud_weather_preset_name,
                                          "Apply a tuned cloud coverage and layer preset.")) {
            apply_atmosphere_cloud_weather_preset(clouds, clouds.weather_preset);
        }
        if (const cubey::host::ScopedImGuiGroup subgroup{
                "Sampling",
                {.default_open = true,
                 .level = 1,
                 .help = "Resolution, sampling, and distance regime controls."}};
            subgroup) {
            cubey::host::imgui_enum_combo("Quality", layer.quality, kAtmosphereCloudQualities,
                                          atmosphere_cloud_quality_name,
                                          "Cloud render resolution and ray-step budget.");
            cubey::host::imgui_enum_combo("Sampling", layer.sampling_mode,
                                          kAtmosphereCloudSamplingModes,
                                          atmosphere_cloud_sampling_mode_name,
                                          "Ray-start pattern. Bayer is stable; blue noise is diagnostic until temporal reconstruction improves.");
            cubey::host::imgui_enum_combo(
                "Density model", layer.density_model, kAtmosphereCloudDensityModels,
                atmosphere_cloud_density_model_name,
                "Cloud density and placement path. Ref-density isolates the old reference density branch; cloud-ref-compatible bypasses macro cloud layers for diagnostics.");
            cubey::host::imgui_enum_combo(
                "Distance mode", layer.distance_mode, kAtmosphereCloudDistanceModes,
                atmosphere_cloud_distance_mode_name,
                "Local, orbit shell, or blended cloud distance behavior.");
            cubey::host::imgui_enum_combo(
                "Orbit repr.", layer.orbit_representation, kAtmosphereCloudOrbitRepresentations,
                atmosphere_cloud_orbit_representation_name,
                "Representation used when the camera is above the local volume regime.");
            cubey::host::imgui_checkbox("Temporal", &layer.temporal_enabled,
                                        "Enable experimental temporal reconstruction for the cloud product.");
            cubey::host::imgui_checkbox("Local volume", &layer.local_volume_enabled,
                                        "Render near and overhead volumetric cloud detail.");
            cubey::host::imgui_checkbox("Horizon layer", &layer.horizon_layer_enabled,
                                        "Add far cloud support near the horizon.");
        }
        if (const cubey::host::ScopedImGuiGroup subgroup{
                "Layer",
                {.default_open = true,
                 .level = 1,
                 .help = "Cloud shell bounds and broad coverage controls."}};
            subgroup) {
            cubey::host::imgui_slider_float("Base altitude", &layer.bottom_altitude_m, 0.0F,
                                            14000.0F, "%.0f m",
                                            "Lower boundary of the cloud layer.");
            cubey::host::imgui_slider_float("Top altitude", &layer.top_altitude_m, 1000.0F,
                                            30000.0F, "%.0f m",
                                            "Upper boundary of the cloud layer.");
            cubey::host::imgui_slider_float("Coverage", &layer.coverage, 0.0F, 1.0F, "%.2f",
                                            "Base cloud coverage before weather/detail carving.");
            cubey::host::imgui_slider_float("Density", &layer.density, 0.0F, 0.08F, "%.3f",
                                            "Volume density multiplier.");
            cubey::host::imgui_slider_float("Weather scale", &layer.weather_scale_km, 40.0F,
                                            500.0F, "%.0f km",
                                            "Broad weather organization scale.");
            cubey::host::imgui_slider_float("Shape domain", &layer.shape_domain_km, 120.0F,
                                            2400.0F, "%.0f km",
                                            "Local cloud density texture domain scale.");
            cubey::host::imgui_slider_float("Wind", &clouds.wind_speed_mps, 0.0F, 900.0F,
                                            "%.0f m/s", "Advection speed for cloud sampling.");
        }
        if (const cubey::host::ScopedImGuiGroup subgroup{
                "Shape",
                {.default_open = false,
                 .level = 1,
                 .help = "Weather mask, erosion, and local cloud form controls."}};
            subgroup) {
            cubey::host::imgui_slider_float("Weather softness", &layer.weather_softness, 0.02F,
                                            0.60F, "%.2f",
                                            "Softness of broad weather transitions.");
            cubey::host::imgui_slider_float("Weather influence", &layer.weather_influence, 0.0F,
                                            1.0F, "%.2f",
                                            "How strongly the weather map biases density.");
            cubey::host::imgui_slider_float("Weather fronts", &layer.weather_fronts, 0.0F, 1.0F,
                                            "%.2f", "Frontal weather feature contribution.");
            cubey::host::imgui_slider_float("Weather cells", &layer.weather_cells, 0.0F, 1.0F,
                                            "%.2f", "Cellular weather feature contribution.");
            cubey::host::imgui_slider_float("Weather streaks", &layer.weather_streaks, 0.0F,
                                            1.0F, "%.2f",
                                            "Wind-aligned weather feature contribution.");
            cubey::host::imgui_slider_float("Detail erosion", &layer.detail_erosion, 0.0F, 1.0F,
                                            "%.2f", "High-frequency erosion contribution.");
            cubey::host::imgui_slider_float("Footprint filter",
                                            &layer.footprint_filter_strength, 0.0F, 2.0F,
                                            "%.2f",
                                            "Deterministic mip filtering for distant or grazing cloud detail.");
            cubey::host::imgui_slider_float("Edge softness", &layer.edge_softness, 0.0F, 2.0F,
                                            "%.2f",
                                            "Footprint-aware softening applied to unresolved cloud density edges.");
            cubey::host::imgui_slider_float("Edge detail fade", &layer.edge_detail_fade, 0.0F,
                                            2.0F, "%.2f",
                                            "Fade high-frequency erosion where the edge is under-resolved.");
            cubey::host::imgui_slider_float("Edge resolve", &layer.edge_resolve_strength, 0.0F,
                                            1.0F, "%.2f",
                                            "Final-composite resolve strength for cloud edge pixels.");
            cubey::host::imgui_slider_float("Crispiness", &layer.crispiness, 1.0F, 80.0F,
                                            "%.1f", "Base density texture frequency/sharpness.");
            cubey::host::imgui_slider_float("Curliness", &layer.curliness, 0.0F, 1.0F, "%.2f",
                                            "Detail density frequency/distortion multiplier.");
            cubey::host::imgui_slider_float("Vertical shear", &layer.vertical_shear_fraction,
                                            0.0F, 0.5F, "%.2f",
                                            "Altitude-dependent shift as a weather-scale fraction.");
            cubey::host::imgui_checkbox("Powder", &layer.powder_enabled,
                                        "Enable powder-style brightening on thin cloud edges.");
        }
        if (const cubey::host::ScopedImGuiGroup subgroup{
                "Lighting",
                {.default_open = false,
                 .level = 1,
                 .help = "Cloud lighting, contrast, and final composite controls."}};
            subgroup) {
            cubey::host::imgui_slider_float("Shadow", &layer.shadow_strength, 0.0F, 2.0F,
                                            "%.2f", "Cloud self-shadow and prototype shadow weight.");
            cubey::host::imgui_slider_float("Horizon", &layer.horizon_strength, 0.0F, 2.0F,
                                            "%.2f", "Dedicated far-horizon cloud layer strength.");
            cubey::host::imgui_slider_float("Absorption", &layer.absorption, 0.0F, 2.0F,
                                            "%.2f", "Light absorption through dense cloud.");
            cubey::host::imgui_slider_float("Ambient", &layer.ambient_strength, 0.0F, 3.0F,
                                            "%.2f", "Cloud ambient-light multiplier.");
            cubey::host::imgui_slider_float("Direct", &layer.direct_strength, 0.0F, 3.0F,
                                            "%.2f", "Direct sunlight multiplier.");
            cubey::host::imgui_slider_float("Phase", &layer.phase_strength, 0.0F, 3.0F,
                                            "%.2f", "Forward/rim phase-light multiplier.");
            cubey::host::imgui_slider_float("Contrast", &layer.final_contrast, 0.0F, 3.0F,
                                            "%.2f", "Final cloud contrast multiplier.");
            cubey::host::imgui_slider_float("Saturation", &layer.final_saturation, 0.0F, 3.0F,
                                            "%.2f", "Final cloud saturation multiplier.");
            cubey::host::imgui_enum_combo(
                "Resolve mode", layer.resolve_mode, kAtmosphereCloudResolveModes,
                atmosphere_cloud_resolve_mode_name,
                "Final cloud resolve filter. Terrain-post follows the reference post blur; metadata-bilateral keeps the previous guarded filter.");
            cubey::host::imgui_slider_float("Resolve", &layer.resolve_strength, 0.0F, 1.0F,
                                            "%.2f", "Alpha-aware cloud resolve strength.");
            cubey::host::imgui_slider_float("Horizon glow", &layer.horizon_glow_strength, 0.0F,
                                            3.0F, "%.2f",
                                            "Final composite horizon fill/glow multiplier.");
            cubey::host::imgui_slider_float("Sun glare", &layer.sun_glare_strength, 0.0F, 3.0F,
                                            "%.2f", "Final composite sun halo multiplier.");
            cubey::host::imgui_slider_float("Jitter", &layer.jitter_strength, 0.0F, 1.0F,
                                            "%.2f", "Ray-start jitter amount.");
        }
        if (const cubey::host::ScopedImGuiGroup subgroup{
                "Transition",
                {.default_open = false,
                 .level = 1,
                 .help = "High-altitude and orbit shell transition controls."}};
            subgroup) {
            cubey::host::imgui_slider_float("Orbit start", &layer.orbit_transition_start_m, 0.0F,
                                            300000.0F, "%.0f m",
                                            "Altitude where orbit shell blend starts.");
            cubey::host::imgui_slider_float("Orbit end", &layer.orbit_transition_end_m, 0.0F,
                                            500000.0F, "%.0f m",
                                            "Altitude where orbit shell blend is complete.");
            cubey::host::imgui_slider_float("Far start", &layer.far_shell_start_m, 0.0F,
                                            300000.0F, "%.0f m",
                                            "View distance where far shell contribution starts.");
            cubey::host::imgui_slider_float("Far end", &layer.far_shell_end_m, 0.0F, 500000.0F,
                                            "%.0f m",
                                            "View distance where far shell contribution is full.");
            cubey::host::imgui_slider_float("Far strength", &layer.far_shell_strength, 0.0F,
                                            1.5F, "%.2f", "Far shell blend contribution.");
            cubey::host::imgui_slider_float("Orbit detail", &layer.orbit_detail_strength, 0.0F,
                                            1.0F, "%.2f",
                                            "High-frequency detail retained by orbit clouds.");
            cubey::host::imgui_slider_float("Orbit density", &layer.orbit_density_scale, 0.0F,
                                            2.0F, "%.2f", "Orbit shell density multiplier.");
            cubey::host::imgui_slider_float("Orbit fill", &layer.orbit_fill, 0.0F, 2.0F,
                                            "%.2f", "Broad orbit cloud fill bias.");
            cubey::host::imgui_slider_float("Orbit motion", &layer.orbit_motion_strength, 0.0F,
                                            4.0F, "%.2f",
                                            "Motion multiplier for orbit weather advection.");
            cubey::host::imgui_slider_float("Orbit extinction", &layer.orbit_shell_extinction,
                                            0.0F, 8.0F, "%.2f",
                                            "Cloud-top shell optical depth multiplier.");
        }
        layer.top_altitude_m = std::max(layer.top_altitude_m, layer.bottom_altitude_m + 1.0F);
        layer.orbit_transition_end_m =
            std::max(layer.orbit_transition_end_m, layer.orbit_transition_start_m);
        layer.far_shell_end_m = std::max(layer.far_shell_end_m, layer.far_shell_start_m);
        if (!clouds.enabled) {
            ImGui::EndDisabled();
        }
    }

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
        cubey::host::imgui_slider_float("Moon", &ui.config.moon.disk_intensity, 0.0F, 4.0F,
                                        "%.2f", "Brightness of the visible moon.");
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
            {.default_open = false,
             .help = "Read-only atmosphere runtime and frame statistics."}};
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
