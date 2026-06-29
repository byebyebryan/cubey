#include "planet_ui.h"

#include "planet_local_detail.h"

#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace cubey::projects::planet {
namespace {

constexpr std::array<PlanetDebugView, 32> kDebugViews{
    PlanetDebugView::Final,
    PlanetDebugView::FaceId,
    PlanetDebugView::PatchId,
    PlanetDebugView::LodLevel,
    PlanetDebugView::ScreenError,
    PlanetDebugView::LodTransition,
    PlanetDebugView::Seams,
    PlanetDebugView::CellEdge,
    PlanetDebugView::TerrainHeight,
    PlanetDebugView::TerrainBandBase,
    PlanetDebugView::TerrainBandRelief,
    PlanetDebugView::TerrainBandDetail,
    PlanetDebugView::TerrainSlope,
    PlanetDebugView::TerrainMaterial,
    PlanetDebugView::Bathymetry,
    PlanetDebugView::Shoreline,
    PlanetDebugView::LandMask,
    PlanetDebugView::Moisture,
    PlanetDebugView::Temperature,
    PlanetDebugView::Roughness,
    PlanetDebugView::Wireframe,
    PlanetDebugView::CelestialPlanes,
    PlanetDebugView::LocalDetailWireframe,
    PlanetDebugView::LocalDetailBlend,
    PlanetDebugView::LocalDetailLod,
    PlanetDebugView::LocalDetailHeight,
    PlanetDebugView::LocalDetailFeatures,
    PlanetDebugView::LocalDetailFinal,
    PlanetDebugView::LocalDetailHorizon,
    PlanetDebugView::AtmosphereTransmittance,
    PlanetDebugView::AtmosphereInscatter,
    PlanetDebugView::AtmospherePathLength,
};

constexpr std::array<PlanetScalePreset, 2> kScalePresets{
    PlanetScalePreset::Earthlike,
    PlanetScalePreset::Mini,
};

constexpr std::array<PlanetAtmosphereMode, 2> kAtmosphereModes{
    PlanetAtmosphereMode::Analytic,
    PlanetAtmosphereMode::Physical,
};

struct CameraLocationReadout {
    float latitude_degrees = 0.0F;
    float longitude_degrees = 0.0F;
};

[[nodiscard]] CameraLocationReadout camera_location_readout(const PlanetFrame& frame) {
    const double x = frame.camera_world_position_m.x;
    const double y = frame.camera_world_position_m.y;
    const double z = frame.camera_world_position_m.z;
    const double radius = std::sqrt((x * x) + (y * y) + (z * z));
    if (radius <= 0.000001) {
        return {};
    }
    constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi;
    return {
        .latitude_degrees =
            static_cast<float>(std::asin(std::clamp(y / radius, -1.0, 1.0)) * kRadiansToDegrees),
        .longitude_degrees = static_cast<float>(std::atan2(x, z) * kRadiansToDegrees),
    };
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

void draw_panel_actions(PlanetUiContext& ui) {
    if (ImGui::Button("Revert Config")) {
        ui.edit_config = ui.active_config;
        ui.config_apply_pending = false;
        ui.rebuild_error.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera") && ui.reset_camera) {
        ui.reset_camera();
    }
    if (!ui.rebuild_error.empty()) {
        ImGui::Text("Config error: %s", ui.rebuild_error.c_str());
    }
}

void draw_planet_controls(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{"Planet"}; group) {
        const cubey::host::ScopedImGuiId section_id("Planet");
        PlanetScalePreset preset = ui.edit_config.scale_preset;
        if (cubey::host::imgui_enum_combo("Scale Preset", preset, kScalePresets,
                                          planet_scale_preset_name,
                                          "Applies scale-sensitive planet defaults.")) {
            apply_planet_scale_preset(ui.edit_config, preset);
        }
        ImGui::InputFloat("Radius (m)", &ui.edit_config.radius_m, 0.0F, 0.0F, "%.0f");
        ImGui::InputFloat("Camera Altitude (m)", &ui.edit_config.camera_altitude_m, 0.0F, 0.0F,
                          "%.0f");
        cubey::host::imgui_enum_combo("Debug View", ui.edit_config.debug_view, kDebugViews,
                                      planet_debug_view_name);
        ImGui::Checkbox("Wire Overlay", &ui.edit_config.wire_overlay);
        const CameraLocationReadout camera_location = camera_location_readout(ui.frame);
        ImGui::Text("Datum Height: %.0f m", ui.frame.camera_datum_altitude_m);
        ImGui::Text("Surface Clearance: %.0f m", ui.frame.camera_surface_clearance_m);
        ImGui::Text("Lat / Lon: %.3f / %.3f deg", camera_location.latitude_degrees,
                    camera_location.longitude_degrees);
    }
}

void draw_surface_controls(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{"Surface"}; group) {
        const cubey::host::ScopedImGuiId section_id("Surface");
        int patches_per_face = static_cast<int>(ui.edit_config.patches_per_face);
        if (ImGui::InputInt("Patches / Face", &patches_per_face)) {
            ui.edit_config.patches_per_face =
                static_cast<std::uint32_t>(std::max(patches_per_face, 0));
        }
        int patch_resolution = static_cast<int>(ui.edit_config.patch_resolution);
        if (ImGui::InputInt("Patch Grid Resolution", &patch_resolution)) {
            ui.edit_config.patch_resolution = static_cast<std::uint32_t>(
                std::clamp(patch_resolution, 0, static_cast<int>(kPlanetMaxPatchResolution)));
        }
        int max_lod_level = static_cast<int>(ui.edit_config.max_lod_level);
        if (ImGui::InputInt("Max LOD Level", &max_lod_level)) {
            ui.edit_config.max_lod_level = static_cast<std::uint32_t>(
                std::clamp(max_lod_level, 0, static_cast<int>(kPlanetMaxLiveLodLevel)));
        }
        ImGui::InputFloat("LOD Target Edge (px)", &ui.edit_config.lod_target_edge_px, 0.0F, 0.0F,
                          "%.1f");
        ImGui::InputFloat("LOD Hysteresis", &ui.edit_config.lod_hysteresis, 0.0F, 0.0F, "%.2f");
        ImGui::Checkbox("Patch Skirts", &ui.edit_config.skirts_enabled);
        ImGui::InputFloat("Skirt Depth Scale", &ui.edit_config.skirt_depth_scale, 0.0F, 0.0F,
                          "%.2f");
        ImGui::Checkbox("Terrain", &ui.edit_config.terrain_enabled);
        ImGui::InputFloat("Terrain Height (m)", &ui.edit_config.terrain_height_scale_m, 0.0F, 0.0F,
                          "%.0f");
        ImGui::InputFloat("Terrain Noise Scale", &ui.edit_config.terrain_noise_scale, 0.0F, 0.0F,
                          "%.2f");
        ImGui::InputFloat("Terrain Mid Detail", &ui.edit_config.terrain_mid_detail_strength, 0.0F,
                          0.0F, "%.2f");
        ImGui::InputFloat("Terrain Fine Detail", &ui.edit_config.terrain_fine_detail_strength, 0.0F,
                          0.0F, "%.2f");
        ImGui::InputFloat("Terrain Fine Scale", &ui.edit_config.terrain_fine_detail_scale, 0.0F,
                          0.0F, "%.2f");
        int terrain_seed = static_cast<int>(ui.edit_config.terrain_seed);
        if (ImGui::InputInt("Terrain Seed", &terrain_seed)) {
            ui.edit_config.terrain_seed = static_cast<std::uint32_t>(std::max(terrain_seed, 0));
        }
        ImGui::InputFloat("Sea Level (m)", &ui.edit_config.sea_level_m, 0.0F, 0.0F, "%.0f");
        ImGui::InputFloat("Bathymetry Depth (m)", &ui.edit_config.bathymetry_depth_scale_m, 0.0F,
                          0.0F, "%.0f");
        ImGui::InputFloat("Shoreline Width (m)", &ui.edit_config.shoreline_width_m, 0.0F, 0.0F,
                          "%.0f");
    }
}

void draw_local_detail_controls(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{"Local Detail", {.default_open = false}}; group) {
        const cubey::host::ScopedImGuiId section_id("LocalDetail");
        const PlanetLocalDetailDiagnostics local_detail_diagnostics =
            ui.local_detail_diagnostics;
        ImGui::Checkbox("Enabled", &ui.edit_config.local_detail_enabled);
        ImGui::Text("Surface Draw: %s",
                    ui.local_detail_surface_weight > 0.001F ? "active" : "inactive");
        ImGui::Text("Surface Weight: %.0f%%", ui.local_detail_surface_weight * 100.0F);
        ImGui::Text("Debug Render: %s",
                    planet_debug_view_is_local_detail(ui.edit_config.debug_view) ? "active"
                                                                                 : "inactive");
        int lod_levels = static_cast<int>(ui.edit_config.local_detail_lod_levels);
        if (ImGui::InputInt("LOD Levels", &lod_levels)) {
            ui.edit_config.local_detail_lod_levels = static_cast<std::uint32_t>(
                std::clamp(lod_levels, 1, static_cast<int>(kPlanetMaxLocalDetailLodLevels)));
        }
        int cells = static_cast<int>(ui.edit_config.local_detail_cells_per_axis);
        if (ImGui::InputInt("Cells / Axis", &cells)) {
            ui.edit_config.local_detail_cells_per_axis = static_cast<std::uint32_t>(
                std::clamp(cells, 1, static_cast<int>(kPlanetMaxLocalDetailCellsPerAxis)));
        }
        ImGui::InputFloat("Configured Outer Extent (m)",
                          &ui.edit_config.local_detail_outer_half_extent_m, 0.0F, 0.0F, "%.0f");
        ImGui::Text("Runtime Outer Extent: %.0f m", local_detail_diagnostics.outer_half_extent);
        ImGui::Text("Active Outer Extent: %.0f m",
                    local_detail_diagnostics.active_outer_half_extent);
        ImGui::Text("Active Levels: %u-%u (%u)", local_detail_diagnostics.active_first_level,
                    local_detail_diagnostics.active_last_level,
                    local_detail_diagnostics.active_level_count);
        ImGui::InputFloat("Height Strength (m)", &ui.edit_config.local_detail_height_strength_m,
                          0.0F, 0.0F, "%.0f");
        ImGui::InputFloat("Detail Scale (m)", &ui.edit_config.local_detail_scale_m, 0.0F, 0.0F,
                          "%.0f");
    }
}

void draw_atmosphere_controls(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{"Atmosphere", {.default_open = false}}; group) {
        const cubey::host::ScopedImGuiId section_id("Atmosphere");
        ImGui::InputFloat("Atmosphere Height (m)", &ui.edit_config.atmosphere_height_m, 0.0F, 0.0F,
                          "%.0f");
        cubey::host::imgui_enum_combo("Atmosphere Mode", ui.edit_config.atmosphere_mode,
                                      kAtmosphereModes, planet_atmosphere_mode_name);
        ImGui::SliderFloat("Surface Haze", &ui.edit_config.atmosphere_haze_strength, 0.0F, 1.0F,
                           "%.2f");
        ImGui::SliderFloat("Haze Start", &ui.edit_config.atmosphere_haze_start, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Haze End", &ui.edit_config.atmosphere_haze_end,
                           ui.edit_config.atmosphere_haze_start, 1.5F, "%.2f");
        ImGui::SliderFloat("Aerial Strength", &ui.edit_config.atmosphere_aerial_strength, 0.0F,
                           1.0F, "%.2f");
        static_cast<void>(cubey::host::draw_atmosphere_environment_look_controls(
            ui.atmosphere_look_config,
            {.label = "Sky Look",
             .default_open = true,
             .level = 1U,
             .help = "Shared atmosphere color controls used by planet sky and surface haze."}));
    }
}

void draw_cloud_controls(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{
            "Clouds",
            {.default_open = false,
             .help = "Shared cloud layer composited into the planet scene."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Clouds");
        cubey::projects::atmosphere::AtmosphereCloudConfig& clouds = ui.clouds_config;
        cubey::render::CloudLayerConfig& layer = clouds.layer;
        cubey::host::imgui_checkbox("Clouds", &clouds.enabled,
                                    "Composite the shared cloud layer in final view.");
        if (!clouds.enabled) {
            ImGui::BeginDisabled();
        }
        cubey::host::imgui_enum_combo("Debug view", layer.debug_view,
                                      cubey::render::kCloudLayerDebugViews,
                                      cubey::render::cloud_layer_debug_view_name,
                                      "Inspect cloud products and diagnostic buffers.");
        if (cubey::host::imgui_enum_combo(
                "Weather preset", clouds.weather_preset,
                cubey::projects::atmosphere::kAtmosphereCloudWeatherPresets,
                cubey::projects::atmosphere::atmosphere_cloud_weather_preset_name,
                "Apply tuned cloud coverage and layer settings.")) {
            cubey::projects::atmosphere::apply_atmosphere_cloud_weather_preset(
                clouds, clouds.weather_preset);
        }
        const cubey::render::CloudLayerViewRegime& regime = ui.cloud_view_regime;
        ImGui::Text("Regime: %s  altitude %.0f m", cloud_view_regime_name(regime.camera_mode),
                    regime.altitude_m);
        ImGui::Text("Blend: orbit %.0f%%  grazing %.0f%%", regime.altitude_blend * 100.0F,
                    regime.horizon_grazing * 100.0F);
        ImGui::Text("Depth mask: %s  fade %.0f m",
                    ui.cloud_scene_depth_occlusion_enabled ? "on" : "off",
                    ui.cloud_scene_depth_fade_m);

        if (const cubey::host::ScopedImGuiGroup subgroup{
                "Sampling",
                {.default_open = true,
                 .level = 1,
                 .help = "Resolution, sampling, and distance regime controls."}};
            subgroup) {
            cubey::host::imgui_enum_combo(
                "Quality", layer.quality,
                cubey::projects::atmosphere::kAtmosphereCloudQualities,
                cubey::projects::atmosphere::atmosphere_cloud_quality_name,
                "Cloud render resolution and ray-step budget.");
            cubey::host::imgui_enum_combo(
                "Sampling", layer.sampling_mode,
                cubey::projects::atmosphere::kAtmosphereCloudSamplingModes,
                cubey::projects::atmosphere::atmosphere_cloud_sampling_mode_name,
                "Ray-start pattern. Bayer is stable; blue noise is diagnostic until temporal reconstruction improves.");
            cubey::host::imgui_enum_combo(
                "Density model", layer.density_model,
                cubey::projects::atmosphere::kAtmosphereCloudDensityModels,
                cubey::projects::atmosphere::atmosphere_cloud_density_model_name,
                "Cloud density and placement path. Ref-density isolates the old reference density branch; cloud-ref-compatible bypasses macro cloud layers for diagnostics.");
            cubey::host::imgui_enum_combo(
                "Distance mode", layer.distance_mode,
                cubey::projects::atmosphere::kAtmosphereCloudDistanceModes,
                cubey::projects::atmosphere::atmosphere_cloud_distance_mode_name,
                "Local, orbit shell, or blended cloud distance behavior.");
            cubey::host::imgui_enum_combo(
                "Orbit repr.", layer.orbit_representation,
                cubey::projects::atmosphere::kAtmosphereCloudOrbitRepresentations,
                cubey::projects::atmosphere::atmosphere_cloud_orbit_representation_name,
                "Representation used above the local volume regime.");
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
                                            500.0F, "%.0f km", "Broad weather feature scale.");
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
            cubey::host::imgui_slider_float("Crispiness", &layer.crispiness, 1.0F, 80.0F,
                                            "%.1f", "Density threshold sharpness.");
            cubey::host::imgui_slider_float("Curliness", &layer.curliness, 0.0F, 1.0F, "%.2f",
                                            "Curl-like distortion applied to local density.");
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
}

void draw_celestial_controls(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{"Celestial"}; group) {
        const cubey::host::ScopedImGuiId section_id("Celestial");
        bool solar_changed = false;
        solar_changed |=
            ImGui::SliderFloat("Day", &ui.solar_time.day_of_year, 1.0F, 365.2422F, "%.1f");
        solar_changed |= ImGui::SliderFloat("Hour", &ui.solar_time.time_hours, 0.0F, 24.0F, "%.2f");
        solar_changed |= ImGui::SliderFloat("Speed (h/s)", &ui.solar_time.hours_per_second, -12.0F,
                                            12.0F, "%.2f");
        if (solar_changed && ui.refresh_celestial_state) {
            ui.refresh_celestial_state();
        }

        const PlanetCelestialDiagnostics celestial_diagnostics =
            planet_celestial_diagnostics(ui.solar_time, ui.solar_config);
        constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
        ImGui::Text("Solar / sidereal day: %.2f h / %.4f h",
                    celestial_diagnostics.mean_solar_day_hours,
                    celestial_diagnostics.sidereal_rotation_hours);
        ImGui::Text("Year / lunar sidereal / synodic: %.4f d / %.6f d / %.5f d",
                    celestial_diagnostics.tropical_year_days,
                    celestial_diagnostics.lunar_sidereal_month_days,
                    celestial_diagnostics.lunar_synodic_month_days);
        ImGui::Text("Axial tilt / moon inclination: %.3f deg / %.3f deg",
                    celestial_diagnostics.axial_tilt_rad * kRadiansToDegrees,
                    celestial_diagnostics.lunar_orbit_inclination_rad * kRadiansToDegrees);
        ImGui::Text("Moon phase: %.3f", celestial_diagnostics.moon_phase_fraction);
        ImGui::Text("Moon illum / light: %.3f / %.3f",
                    ui.celestial_lighting.moon_light_intensity / kPlanetFullMoonLightIntensity,
                    ui.celestial_lighting.moon_light_intensity);
    }
}

void draw_exposure_controls(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{"Exposure"}; group) {
        const cubey::host::ScopedImGuiId section_id("Exposure");
        ImGui::Checkbox("Auto Exposure", &ui.exposure_config.auto_exposure_enabled);
        if (ui.exposure_config.auto_exposure_enabled) {
            ImGui::SliderFloat("Daylight Exposure", &ui.exposure_config.daylight_exposure, -4.0F,
                               1.0F, "%.2f");
            ImGui::SliderFloat("Twilight Exposure", &ui.exposure_config.twilight_exposure, -3.0F,
                               3.0F, "%.2f");
            ImGui::SliderFloat("Night Exposure", &ui.exposure_config.night_exposure, -1.0F, 4.0F,
                               "%.2f");
        } else {
            ImGui::SliderFloat("Manual Exposure", &ui.exposure_config.manual_exposure, -4.0F, 4.0F,
                               "%.2f");
        }
        ImGui::Text("Sun elevation: %.1f deg",
                    planet_celestial_sun_elevation_degrees(ui.celestial_system,
                                                           ui.frame.camera_world_position_m));
        const float light_fraction =
            ui.view_light_fraction ? ui.view_light_fraction(ui.extent) : 0.0F;
        const float exposure = ui.display_exposure ? ui.display_exposure(ui.extent) : 0.0F;
        ImGui::Text("Orbit light / exposure: %.2f / %.2f", light_fraction, exposure);
    }
}

void draw_diagnostics(PlanetUiContext& ui) {
    if (const cubey::host::ScopedImGuiGroup group{"Diagnostics", {.default_open = false}}; group) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        const float radius = std::max(ui.active_config.radius_m, 1.0F);
        const float finest_global_cell =
            planet_surface_nominal_cell_edge_m(ui.active_config, ui.active_config.max_lod_level);
        const PlanetLocalDetailDiagnostics local_detail_diagnostics = ui.local_detail_diagnostics;
        ImGui::Text("Scale preset: %s", planet_scale_preset_name(ui.active_config.scale_preset));
        ImGui::Text("Radius: %.0f m", ui.active_config.radius_m);
        ImGui::Text("Atmosphere: %.0f m", ui.active_config.atmosphere_height_m);
        ImGui::Text("Atmosphere / radius: %.2f%%",
                    (ui.active_config.atmosphere_height_m / radius) * 100.0F);
        ImGui::Text("Terrain / radius: %.3f%%",
                    (ui.active_config.terrain_height_scale_m / radius) * 100.0F);
        ImGui::Text("Datum altitude: %.0f m", ui.frame.camera_datum_altitude_m);
        ImGui::Text("Terrain height: %.0f m", ui.frame.camera_surface_height_m);
        ImGui::Text("Surface clearance: %.0f m", ui.frame.camera_surface_clearance_m);
        ImGui::Text("Surface camera: %.0f%%",
                    planet_surface_camera_blend_from_clearance(
                        ui.active_config, ui.frame.camera_surface_clearance_m) *
                        100.0F);
        ImGui::Text("Horizon: %.0f m", ui.frame.horizon_distance_m);
        ImGui::Text("Near / far: %.1f m / %.0f m", ui.frame.near_plane_m, ui.frame.far_plane_m);
        ImGui::Text("Origin: %.0f %.0f %.0f", ui.frame.surface_origin_m.x,
                    ui.frame.surface_origin_m.y, ui.frame.surface_origin_m.z);
        ImGui::Text("Solar time: %.2f h, day %.1f", ui.solar_time.time_hours,
                    ui.solar_time.day_of_year);
        ImGui::Text("Sun dir: %.2f %.2f %.2f", ui.celestial_system.sun.direction.x,
                    ui.celestial_system.sun.direction.y, ui.celestial_system.sun.direction.z);
        ImGui::Text("Moon dir: %.2f %.2f %.2f", ui.celestial_system.moon.direction.x,
                    ui.celestial_system.moon.direction.y, ui.celestial_system.moon.direction.z);
        ImGui::Text("Orbit angles: planet %.2f, rotation %.2f, moon %.2f",
                    ui.celestial_system.planet_orbit_angle_rad,
                    ui.celestial_system.planet_rotation_angle_rad,
                    ui.celestial_system.moon_orbit_angle_rad);

        const PlanetSurfaceDiagnostics& surface = ui.surface_diagnostics;
        ImGui::SeparatorText("Surface");
        ImGui::Text("Patches: %u rendered / %u planned", surface.visible_patch_count,
                    surface.planned_patch_count);
        ImGui::Text("Patch budget: %u / %llu", surface.patch_count,
                    static_cast<unsigned long long>(kPlanetMaxLivePatchInstances));
        ImGui::Text("Base / refined: %u / %u", surface.base_patch_count,
                    surface.refined_patch_count);
        ImGui::Text("Subdivided parents: %u", surface.subdivided_patch_count);
        ImGui::Text("Fallback parents: %u", surface.refinement_fallback_patch_count);
        ImGui::Text("Budget fallback parents: %u", surface.budget_fallback_patch_count);
        ImGui::Text("Hysteresis delayed: %u splits / %u merges",
                    surface.hysteresis_delayed_split_count, surface.hysteresis_delayed_merge_count);
        ImGui::Text("Refinement culled: %u horizon / %u view", surface.culled_horizon_count,
                    surface.culled_view_count);
        ImGui::Text("LOD range: %u - %u", surface.min_lod_level, surface.max_lod_level);
        for (std::size_t lod_index = 0; lod_index < surface.patches_by_lod.size(); ++lod_index) {
            const std::uint32_t patch_count = surface.patches_by_lod[lod_index];
            const float min_cell = surface.min_cell_edge_m_by_lod[lod_index];
            const float max_cell = surface.max_cell_edge_m_by_lod[lod_index];
            if (patch_count == 0U && lod_index > surface.max_lod_level) {
                continue;
            }
            ImGui::Text("LOD %zu: %u patches, cell %.0f-%.0f m", lod_index, patch_count, min_cell,
                        max_cell);
        }
        ImGui::Text("Screen error: %.1f px - %.1f px", surface.min_screen_error_px,
                    surface.max_screen_error_px);
        ImGui::Text("LOD transition: %u candidates, %.0f%% max pressure",
                    surface.transition_candidate_count, surface.max_transition_pressure * 100.0F);
        ImGui::Text("LOD neighbors: %u edges, %u mismatched, max delta %u",
                    surface.lod_neighbor_edge_count, surface.lod_neighbor_mismatch_edge_count,
                    surface.max_lod_neighbor_delta);
        ImGui::Text("LOD repair splits: %u", surface.lod_neighbor_repaired_split_count);
        ImGui::Text("Surface vertices: %u", surface.vertex_count);
        ImGui::Text("Surface triangles: %u", surface.triangle_count);
        ImGui::Text("Total terrain triangles: %u",
                    surface.triangle_count +
                        (ui.local_detail_surface_weight > 0.001F
                             ? local_detail_diagnostics.triangle_count
                             : 0U));
        ImGui::Text("Configured finest cell: %.1f m", finest_global_cell);
        ImGui::Text("Cell edge: %.0f m - %.0f m", surface.min_edge_length_m,
                    surface.max_edge_length_m);
        ImGui::Text("Seam edges: %u", surface.seam_edge_count);
        ImGui::Text("Skirt triangles: %u", surface.skirt_triangle_count);
        ImGui::Text("Skirt depth: %.0f m - %.0f m", surface.min_skirt_depth_m,
                    surface.max_skirt_depth_m);

        ImGui::SeparatorText("Local Detail");
        ImGui::Text("Enabled: %s", local_detail_diagnostics.enabled ? "yes" : "no");
        ImGui::Text("Active: %s", local_detail_diagnostics.active ? "yes" : "no");
        ImGui::Text("Surface weight: %.0f%%", ui.local_detail_surface_weight * 100.0F);
        ImGui::Text("Levels: %u configured, active %u-%u (%u)", local_detail_diagnostics.lod_levels,
                    local_detail_diagnostics.active_first_level,
                    local_detail_diagnostics.active_last_level,
                    local_detail_diagnostics.active_level_count);
        ImGui::Text("Patches: %u", local_detail_diagnostics.patch_count);
        ImGui::Text("Configured outer extent: %.0f m",
                    ui.active_config.local_detail_outer_half_extent_m);
        ImGui::Text("Runtime near cell / outer extent: %.1f m / %.0f m",
                    local_detail_diagnostics.near_cell_size,
                    local_detail_diagnostics.outer_half_extent);
        ImGui::Text("Active outer extent: %.0f m",
                    local_detail_diagnostics.active_outer_half_extent);
        ImGui::Text("View scale: %.1f m/px, finest active %.1f m (%.2f px)",
                    local_detail_diagnostics.meters_per_pixel,
                    local_detail_diagnostics.finest_active_cell_size,
                    local_detail_diagnostics.projected_finest_cell_px);
        ImGui::Text("Vertices / triangles: %u / %u", local_detail_diagnostics.vertex_count,
                    local_detail_diagnostics.triangle_count);
        ImGui::Text("Detail height / scale: %.0f m / %.0f m",
                    local_detail_diagnostics.max_detail_delta_m,
                    local_detail_diagnostics.detail_scale_m);
    }
}

} // namespace

void draw_planet_ui(PlanetUiContext ui) {
    if (!cubey::host::begin_control_panel("Planet")) {
        ImGui::End();
        return;
    }

    if (ui.maybe_apply_config) {
        ui.maybe_apply_config();
    }
    draw_panel_actions(ui);

    const PlanetConfig config_before_edit = ui.edit_config;
    draw_planet_controls(ui);
    draw_surface_controls(ui);
    draw_local_detail_controls(ui);
    draw_atmosphere_controls(ui);
    draw_cloud_controls(ui);
    if (ui.edit_config != config_before_edit) {
        ui.config_apply_pending = ui.edit_config != ui.active_config;
    }

    draw_celestial_controls(ui);
    draw_exposure_controls(ui);
    cubey::host::draw_performance_ui(ui.performance);
    draw_diagnostics(ui);
    ImGui::End();
}

} // namespace cubey::projects::planet
