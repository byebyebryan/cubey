#include <cubey/host/cloud_environment_ui.h>

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>

namespace cubey::host {
namespace {

void mark_changed(bool changed, bool& dirty) {
    dirty = dirty || changed;
}

[[nodiscard]] const char* cloud_view_regime_name(float camera_mode) {
    if (camera_mode >= 3.5F) {
        return "orbit";
    }
    if (camera_mode >= 0.5F) {
        return "high-oblique";
    }
    return "local";
}

} // namespace

bool draw_cloud_environment_controls(cubey::CloudEnvironmentConfig& clouds,
                                     CloudEnvironmentUiConfig config) {
    bool changed = false;
    if (const ScopedImGuiGroup group{
            config.label,
            {.default_open = config.default_open, .level = config.level, .help = config.help}};
        group) {
        const ScopedImGuiId section_id(config.label);
        cubey::render::CloudLayerConfig& layer = clouds.layer;

        mark_changed(imgui_checkbox("Clouds", &clouds.enabled,
                                    "Composite the shared cloud layer in final view."),
                     changed);
        if (!clouds.enabled) {
            ImGui::BeginDisabled();
        }

        mark_changed(imgui_enum_combo("Debug view", layer.debug_view,
                                      cubey::render::kCloudLayerDebugViews,
                                      cubey::render::cloud_layer_debug_view_name,
                                      "Inspect the cloud product and diagnostic buffers."),
                     changed);
        if (imgui_enum_combo("Weather preset", clouds.weather_preset,
                             kCloudEnvironmentWeatherPresets,
                             cloud_environment_weather_preset_name,
                             "Apply a tuned cloud coverage and layer preset.")) {
            apply_cloud_environment_weather_preset(clouds, clouds.weather_preset);
            changed = true;
        }
        if (config.show_regime_status) {
            ImGui::Text("Regime: %s  altitude %.0f m",
                        cloud_view_regime_name(config.regime.camera_mode),
                        config.regime.altitude_m);
            ImGui::Text("Blend: orbit %.0f%%  grazing %.0f%%",
                        config.regime.altitude_blend * 100.0F,
                        config.regime.horizon_grazing * 100.0F);
            ImGui::Text("Depth mask: %s  fade %.0f m",
                        config.scene_depth_occlusion_enabled ? "on" : "off",
                        config.scene_depth_fade_m);
        }

        if (const ScopedImGuiGroup subgroup{
                "Sampling",
                {.default_open = true,
                 .level = 1,
                 .help = "Resolution, sampling, and distance regime controls."}};
            subgroup) {
            mark_changed(imgui_enum_combo("Quality", layer.quality, kCloudEnvironmentQualities,
                                          cloud_environment_quality_name,
                                          "Cloud render resolution and ray-step budget."),
                         changed);
            mark_changed(imgui_enum_combo("Sampling", layer.sampling_mode,
                                          kCloudEnvironmentSamplingModes,
                                          cloud_environment_sampling_mode_name,
                                          "Ray-start pattern. Bayer is stable; blue noise is "
                                          "diagnostic until temporal reconstruction improves."),
                         changed);
            mark_changed(imgui_slider_int(
                             "View steps", &layer.view_steps_override, 0, 128,
                             "Ray-march step override. 0 uses the selected quality preset."),
                         changed);
            mark_changed(imgui_slider_int("View samples", &layer.view_samples, 1, 4,
                                          "Ray-start samples per pixel. 1, 2, and 4 are valid; "
                                          "higher values are diagnostic."),
                         changed);
            if (layer.view_samples != 1 && layer.view_samples != 2 && layer.view_samples != 4) {
                layer.view_samples = layer.view_samples < 2 ? 1 : 4;
                changed = true;
            }
            mark_changed(imgui_enum_combo(
                             "Sample mode", layer.view_sample_mode,
                             kCloudEnvironmentViewSampleModes,
                             cloud_environment_view_sample_mode_name,
                             "Single-frame marches all selected samples now; temporal-phased "
                             "alternates one deterministic phase per frame through temporal "
                             "history."),
                         changed);
            mark_changed(imgui_enum_combo(
                             "Density model", layer.density_model,
                             kCloudEnvironmentDensityModels, cloud_environment_density_model_name,
                             "Cloud density and placement path. Surface-volume is the production "
                             "local cloud path; experimental-aerial-orbit preserves unfinished "
                             "high/orbit scaffolding."),
                         changed);
            if (config.show_aerial_orbit_controls) {
                mark_changed(imgui_enum_combo(
                                 "Distance mode", layer.distance_mode,
                                 kCloudEnvironmentDistanceModes,
                                 cloud_environment_distance_mode_name,
                                 "Local, orbit shell, or blended cloud distance behavior."),
                             changed);
                mark_changed(imgui_enum_combo(
                                 "Orbit repr.", layer.orbit_representation,
                                 kCloudEnvironmentOrbitRepresentations,
                                 cloud_environment_orbit_representation_name,
                                 "Representation used when the camera is above the local volume "
                                 "regime."),
                             changed);
            }
            mark_changed(imgui_checkbox(
                             "Temporal", &layer.temporal_enabled,
                             "Enable experimental temporal reconstruction for the cloud product."),
                         changed);
            mark_changed(imgui_checkbox("Local volume", &layer.local_volume_enabled,
                                        "Render near and overhead volumetric cloud detail."),
                         changed);
            mark_changed(imgui_checkbox("Horizon layer", &layer.horizon_layer_enabled,
                                        "Add far cloud support near the horizon."),
                         changed);
        }

        if (const ScopedImGuiGroup subgroup{
                "Layer",
                {.default_open = true,
                 .level = 1,
                 .help = "Cloud shell bounds and broad coverage controls."}};
            subgroup) {
            mark_changed(imgui_slider_float("Base altitude", &layer.bottom_altitude_m, 0.0F,
                                            14000.0F, "%.0f m",
                                            "Lower boundary of the cloud layer."),
                         changed);
            mark_changed(imgui_slider_float("Top altitude", &layer.top_altitude_m, 1000.0F,
                                            30000.0F, "%.0f m",
                                            "Upper boundary of the cloud layer."),
                         changed);
            mark_changed(imgui_slider_float("Coverage", &layer.coverage, 0.0F, 1.0F, "%.2f",
                                            "Base cloud coverage before weather/detail carving."),
                         changed);
            mark_changed(imgui_slider_float("Density", &layer.density, 0.0F, 0.08F, "%.3f",
                                            "Volume density multiplier."),
                         changed);
            mark_changed(imgui_slider_float("Weather scale", &layer.weather_scale_km, 40.0F,
                                            500.0F, "%.0f km",
                                            "Broad weather organization scale."),
                         changed);
            mark_changed(imgui_slider_float("Shape domain", &layer.shape_domain_km, 120.0F,
                                            2400.0F, "%.0f km",
                                            "Local cloud density texture domain scale."),
                         changed);
            mark_changed(imgui_slider_float("Wind", &clouds.wind_speed_mps, 0.0F, 900.0F,
                                            "%.0f m/s",
                                            "Advection speed for cloud sampling."),
                         changed);
        }

        if (const ScopedImGuiGroup subgroup{
                "Shape",
                {.default_open = false,
                 .level = 1,
                 .help = "Weather mask, erosion, and local cloud form controls."}};
            subgroup) {
            mark_changed(imgui_slider_float("Weather softness", &layer.weather_softness, 0.02F,
                                            0.60F, "%.2f",
                                            "Softness of broad weather transitions."),
                         changed);
            mark_changed(imgui_slider_float("Weather influence", &layer.weather_influence, 0.0F,
                                            1.0F, "%.2f",
                                            "How strongly the weather map biases density."),
                         changed);
            mark_changed(imgui_slider_float("Weather fronts", &layer.weather_fronts, 0.0F, 1.0F,
                                            "%.2f", "Frontal weather feature contribution."),
                         changed);
            mark_changed(imgui_slider_float("Weather cells", &layer.weather_cells, 0.0F, 1.0F,
                                            "%.2f", "Cellular weather feature contribution."),
                         changed);
            mark_changed(imgui_slider_float("Weather streaks", &layer.weather_streaks, 0.0F,
                                            1.0F, "%.2f",
                                            "Wind-aligned weather feature contribution."),
                         changed);
            mark_changed(imgui_slider_float("Detail erosion", &layer.detail_erosion, 0.0F, 1.0F,
                                            "%.2f", "High-frequency erosion contribution."),
                         changed);
            mark_changed(imgui_slider_float(
                             "Footprint filter", &layer.footprint_filter_strength, 0.0F, 2.0F,
                             "%.2f",
                             "Deterministic mip filtering for distant or grazing cloud detail."),
                         changed);
            mark_changed(imgui_slider_float(
                             "Edge softness", &layer.edge_softness, 0.0F, 2.0F, "%.2f",
                             "Footprint-aware softening applied to unresolved cloud density edges."),
                         changed);
            mark_changed(imgui_slider_float(
                             "Edge detail fade", &layer.edge_detail_fade, 0.0F, 2.0F, "%.2f",
                             "Fade high-frequency erosion where the edge is under-resolved."),
                         changed);
            mark_changed(imgui_slider_float(
                             "Edge resolve", &layer.edge_resolve_strength, 0.0F, 1.0F, "%.2f",
                             "Final-composite resolve strength for cloud edge pixels."),
                         changed);
            mark_changed(imgui_slider_float("Crispiness", &layer.crispiness, 1.0F, 80.0F,
                                            "%.1f",
                                            "Base density texture frequency/sharpness."),
                         changed);
            mark_changed(imgui_slider_float("Curliness", &layer.curliness, 0.0F, 1.0F, "%.2f",
                                            "Detail density frequency/distortion multiplier."),
                         changed);
            mark_changed(imgui_slider_float(
                             "Vertical shear", &layer.vertical_shear_fraction, 0.0F, 0.5F,
                             "%.2f", "Altitude-dependent shift as a weather-scale fraction."),
                         changed);
            mark_changed(imgui_checkbox(
                             "Powder", &layer.powder_enabled,
                             "Enable powder-style brightening on thin cloud edges."),
                         changed);
        }

        if (const ScopedImGuiGroup subgroup{
                "Lighting",
                {.default_open = false,
                 .level = 1,
                 .help = "Cloud lighting, contrast, and final composite controls."}};
            subgroup) {
            mark_changed(imgui_slider_float("Shadow", &layer.shadow_strength, 0.0F, 2.0F, "%.2f",
                                            "Cloud self-shadow and prototype shadow weight."),
                         changed);
            mark_changed(imgui_slider_float("Horizon", &layer.horizon_strength, 0.0F, 2.0F,
                                            "%.2f",
                                            "Dedicated far-horizon cloud layer strength."),
                         changed);
            mark_changed(imgui_slider_float("Absorption", &layer.absorption, 0.0F, 2.0F, "%.2f",
                                            "Light absorption through dense cloud."),
                         changed);
            mark_changed(imgui_slider_float("Ambient", &layer.ambient_strength, 0.0F, 3.0F,
                                            "%.2f", "Cloud ambient-light multiplier."),
                         changed);
            mark_changed(imgui_slider_float("Direct", &layer.direct_strength, 0.0F, 3.0F,
                                            "%.2f", "Direct sunlight multiplier."),
                         changed);
            mark_changed(imgui_slider_float("Phase", &layer.phase_strength, 0.0F, 3.0F, "%.2f",
                                            "Forward/rim phase-light multiplier."),
                         changed);
            mark_changed(imgui_slider_float("Twilight color", &layer.twilight_color_strength,
                                            0.0F, 2.0F, "%.2f",
                                            "Warm low-sun cloud color from sun and horizon sky "
                                            "radiance."),
                         changed);
            mark_changed(imgui_slider_float("Twilight edge", &layer.twilight_edge_strength, 0.0F,
                                            2.0F, "%.2f",
                                            "Warm low-sun boost for cloud optical edges and rim "
                                            "response."),
                         changed);
            mark_changed(imgui_slider_float("Twilight saturation",
                                            &layer.twilight_saturation_strength, 0.0F, 2.0F,
                                            "%.2f",
                                            "Twilight color saturation preserved in final "
                                            "composite."),
                         changed);
            mark_changed(imgui_slider_float(
                             "Afterglow", &layer.afterglow_strength, 0.0F, 2.0F, "%.2f",
                             "Art-directed red, pink, or purple cloud accent near low sun."),
                         changed);
            mark_changed(imgui_slider_float("Contrast", &layer.final_contrast, 0.0F, 3.0F,
                                            "%.2f", "Final cloud contrast multiplier."),
                         changed);
            mark_changed(imgui_slider_float("Saturation", &layer.final_saturation, 0.0F, 3.0F,
                                            "%.2f", "Final cloud saturation multiplier."),
                         changed);
            mark_changed(imgui_enum_combo(
                             "Resolve mode", layer.resolve_mode, kCloudEnvironmentResolveModes,
                             cloud_environment_resolve_mode_name,
                             "Final cloud resolve filter. Terrain-post follows the reference post "
                             "blur; metadata-bilateral keeps the previous guarded filter."),
                         changed);
            mark_changed(imgui_slider_float("Resolve", &layer.resolve_strength, 0.0F, 1.0F,
                                            "%.2f", "Alpha-aware cloud resolve strength."),
                         changed);
            mark_changed(imgui_slider_float("Horizon glow", &layer.horizon_glow_strength, 0.0F,
                                            3.0F, "%.2f",
                                            "Final composite horizon fill/glow multiplier."),
                         changed);
            mark_changed(imgui_slider_float("Sun glare", &layer.sun_glare_strength, 0.0F, 3.0F,
                                            "%.2f", "Final composite sun halo multiplier."),
                         changed);
            mark_changed(imgui_slider_float("Jitter", &layer.jitter_strength, 0.0F, 1.0F,
                                            "%.2f", "Ray-start jitter amount."),
                         changed);
        }

        if (config.show_aerial_orbit_controls) {
            if (const ScopedImGuiGroup subgroup{
                    "Transition",
                    {.default_open = false,
                     .level = 1,
                     .help = "High-altitude and orbit shell transition controls."}};
                subgroup) {
                mark_changed(imgui_slider_float("Orbit start", &layer.orbit_transition_start_m,
                                                0.0F, 300000.0F, "%.0f m",
                                                "Altitude where orbit shell blend starts."),
                             changed);
                mark_changed(imgui_slider_float("Orbit end", &layer.orbit_transition_end_m, 0.0F,
                                                500000.0F, "%.0f m",
                                                "Altitude where orbit shell blend is complete."),
                             changed);
                mark_changed(imgui_slider_float("Far start", &layer.far_shell_start_m, 0.0F,
                                                300000.0F, "%.0f m",
                                                "View distance where far shell contribution "
                                                "starts."),
                             changed);
                mark_changed(imgui_slider_float("Far end", &layer.far_shell_end_m, 0.0F,
                                                500000.0F, "%.0f m",
                                                "View distance where far shell contribution is "
                                                "full."),
                             changed);
                mark_changed(imgui_slider_float("Far strength", &layer.far_shell_strength, 0.0F,
                                                1.5F, "%.2f",
                                                "Far shell blend contribution."),
                             changed);
                mark_changed(imgui_slider_float("Orbit detail", &layer.orbit_detail_strength,
                                                0.0F, 1.0F, "%.2f",
                                                "High-frequency detail retained by orbit clouds."),
                             changed);
                mark_changed(imgui_slider_float("Orbit density", &layer.orbit_density_scale, 0.0F,
                                                2.0F, "%.2f",
                                                "Orbit shell density multiplier."),
                             changed);
                mark_changed(imgui_slider_float("Orbit fill", &layer.orbit_fill, 0.0F, 2.0F,
                                                "%.2f", "Broad orbit cloud fill bias."),
                             changed);
                mark_changed(imgui_slider_float("Orbit motion", &layer.orbit_motion_strength,
                                                0.0F, 4.0F, "%.2f",
                                                "Motion multiplier for orbit weather advection."),
                             changed);
                mark_changed(imgui_slider_float("Orbit extinction",
                                                &layer.orbit_shell_extinction, 0.0F, 8.0F,
                                                "%.2f",
                                                "Cloud-top shell optical depth multiplier."),
                             changed);
            }
        }

        const float previous_top = layer.top_altitude_m;
        const float previous_orbit_end = layer.orbit_transition_end_m;
        const float previous_far_end = layer.far_shell_end_m;
        layer.top_altitude_m = std::max(layer.top_altitude_m, layer.bottom_altitude_m + 1.0F);
        layer.orbit_transition_end_m =
            std::max(layer.orbit_transition_end_m, layer.orbit_transition_start_m);
        layer.far_shell_end_m = std::max(layer.far_shell_end_m, layer.far_shell_start_m);
        changed = changed || layer.top_altitude_m != previous_top ||
                  layer.orbit_transition_end_m != previous_orbit_end ||
                  layer.far_shell_end_m != previous_far_end;

        if (!clouds.enabled) {
            ImGui::EndDisabled();
        }
    }
    return changed;
}

} // namespace cubey::host
