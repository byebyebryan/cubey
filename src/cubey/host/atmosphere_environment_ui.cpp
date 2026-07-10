#include <cubey/host/atmosphere_environment_ui.h>

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

namespace cubey::host {
namespace {

void mark_changed(bool changed, bool& dirty) {
    dirty = dirty || changed;
}

} // namespace

bool draw_atmosphere_environment_look_controls(
    cubey::render::AtmosphereEnvironmentConfig& environment,
    AtmosphereEnvironmentLookUiConfig config) {
    bool changed = false;
    if (const ScopedImGuiGroup group{
            config.label,
            {.default_open = config.default_open, .level = config.level, .help = config.help}};
        group) {
        const ScopedImGuiId section_id(config.label);
        mark_changed(imgui_slider_float("Rayleigh", &environment.rayleigh_density_scale, 0.0F,
                                        2.0F, "%.2f",
                                        "Molecular scattering density multiplier."),
                     changed);
        mark_changed(imgui_slider_float("Mie", &environment.mie_density_scale, 0.0F, 3.0F,
                                        "%.2f", "Aerosol scattering density multiplier."),
                     changed);
        mark_changed(imgui_slider_float("Ozone", &environment.ozone_density_scale, 0.0F, 2.0F,
                                        "%.2f", "Ozone absorption density multiplier."),
                     changed);
        mark_changed(imgui_slider_float("Twilight", &environment.night_sky.twilight_strength,
                                        0.0F, 4.0F, "%.2f",
                                        "Brightness of the twilight band."),
                     changed);
        mark_changed(imgui_slider_float("Horizon Warmth",
                                        &environment.night_sky.twilight_horizon_warmth, 0.0F,
                                        2.0F, "%.2f",
                                        "Warm color bias near the twilight horizon."),
                     changed);
    }
    return changed;
}

bool draw_atmosphere_environment_controls(cubey::AtmosphereEnvironmentRunState& atmosphere,
                                          AtmosphereEnvironmentUiConfig config) {
    bool changed = false;
    auto& environment = atmosphere.environment;
    if (const ScopedImGuiGroup group{
            config.label,
            {.default_open = config.default_open, .level = config.level, .help = config.help}};
        group) {
        const ScopedImGuiId section_id(config.label);

        mark_changed(
            imgui_checkbox("Solar clock", &atmosphere.solar_time_enabled,
                           "Compute sun position from time, date, latitude, and azimuth offset."),
            changed);

        if (atmosphere.solar_time_enabled) {
            mark_changed(imgui_slider_float("Time", &environment.time_of_day.time_hours, 0.0F,
                                            24.0F, "%.2f h", "Local solar-clock time in hours."),
                         changed);
            mark_changed(imgui_slider_float("Day", &environment.time_of_day.day_of_year, 1.0F,
                                            366.0F, "%.0f",
                                            "Day of year used by the solar position model."),
                         changed);
            mark_changed(imgui_slider_float("Latitude", &environment.time_of_day.latitude_degrees,
                                            -90.0F, 90.0F, "%.1f deg",
                                            "Observer latitude used by solar mode."),
                         changed);
            mark_changed(imgui_slider_float(
                             "Azimuth offset", &environment.time_of_day.azimuth_offset_degrees,
                             -180.0F, 180.0F, "%.1f deg",
                             "Rotation offset applied to the computed solar azimuth."),
                         changed);
            mark_changed(imgui_checkbox("Play", &atmosphere.time_playing,
                                        "Advance atmosphere time every frame."),
                         changed);
            mark_changed(imgui_slider_float("Speed", &atmosphere.time_speed_hours_per_second, 0.0F,
                                            6.0F, "%.2f h/s",
                                            "Simulated hours advanced per real second."),
                         changed);
            const cubey::render::AtmosphereEnvironmentSolarPosition solar =
                cubey::render::atmosphere_environment_solar_position(environment.time_of_day);
            ImGui::Text("Solar: %.1f deg / %.1f deg", solar.elevation_degrees,
                        solar.azimuth_degrees);
        } else {
            mark_changed(imgui_slider_float("Sun elevation", &environment.sun_elevation_degrees,
                                            -90.0F, 90.0F, "%.1f deg",
                                            "Manual sun angle above the horizon."),
                         changed);
            mark_changed(imgui_slider_float("Sun azimuth", &environment.sun_azimuth_degrees,
                                            -180.0F, 180.0F, "%.1f deg",
                                            "Manual horizontal sun direction."),
                         changed);
        }

        mark_changed(draw_atmosphere_environment_look_controls(environment), changed);

        if (const ScopedImGuiGroup night_group{
                "Night sky",
                {.default_open = false,
                 .level = 1U,
                 .help = "Night-sky visibility used by the atmosphere background."}};
            night_group) {
            const ScopedImGuiId night_id("Night sky");
            mark_changed(imgui_checkbox(
                             "Camera response", &environment.night_sky.camera_visual_mode,
                             "Expose the faint star population and photographic Milky Way response."),
                         changed);
            mark_changed(imgui_slider_float("Stars", &environment.night_sky.star_intensity, 0.0F,
                                            4.0F, "%.2f", "Brightness of procedural stars."),
                         changed);
            mark_changed(imgui_slider_float("Star density", &environment.night_sky.star_density,
                                            0.0F, 1.0F, "%.2f", "Density of procedural stars."),
                         changed);
            mark_changed(imgui_slider_float("Milky Way", &environment.night_sky.milky_way_intensity,
                                            0.0F, 4.0F, "%.2f",
                                            "Brightness of the generated Milky Way layer."),
                         changed);
            mark_changed(imgui_slider_float("Light pollution",
                                            &environment.night_sky.light_pollution, 0.0F, 1.0F,
                                            "%.2f", "Skyglow amount that suppresses stars."),
                         changed);
        }

        if (const ScopedImGuiGroup moon_group{
                "Moon",
                {.default_open = false,
                 .level = 1U,
                 .help = "Visible moon and moonlight contribution for night lighting."}};
            moon_group) {
            const ScopedImGuiId moon_id("Moon");
            mark_changed(imgui_checkbox("Moon", &environment.moon.enabled,
                                        "Enable visible moon and moonlight."),
                         changed);
            mark_changed(imgui_slider_float("Moon", &environment.moon.disk_intensity, 0.0F, 4.0F,
                                            "%.2f", "Brightness of the visible moon."),
                         changed);
            mark_changed(imgui_slider_float("Moonlight", &environment.moon.moonlight_intensity,
                                            0.0F, 4.0F, "%.2f",
                                            "Brightness of indirect moonlight."),
                         changed);
            mark_changed(imgui_slider_float("Phase offset", &environment.moon.phase_offset_days,
                                            0.0F, 29.530588F, "%.2f d",
                                            "Offset in days through the lunar phase cycle."),
                         changed);
        }

        if (const ScopedImGuiGroup exposure_group{
                "Exposure",
                {.default_open = false,
                 .level = 1U,
                 .help = "Environment-driven exposure used by post shading."}};
            exposure_group) {
            const ScopedImGuiId exposure_id("Exposure");
            mark_changed(imgui_checkbox("Auto exposure", &atmosphere.auto_exposure_enabled,
                                        "Adapt exposure across day, twilight, and night."),
                         changed);
            mark_changed(imgui_slider_float("Exposure bias", &atmosphere.exposure_bias, -4.0F, 4.0F,
                                            "%.2f", "Bias applied to automatic exposure in stops."),
                         changed);
            ImGui::Text("Resolved exposure: %.2f", atmosphere.resolved_exposure);
        }
    }

    if (changed) {
        cubey::atmosphere_environment_resolve_run_state(atmosphere);
    }
    return changed;
}

} // namespace cubey::host
