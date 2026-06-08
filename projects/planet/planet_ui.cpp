#include "planet_ui.h"

#include "planet_local_detail.h"

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

constexpr std::array<PlanetDebugView, 28> kDebugViews{
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
