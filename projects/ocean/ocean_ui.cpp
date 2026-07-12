#include "ocean_ui.h"

#include "ocean_mesh.h"

#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/cloud_environment_ui.h>
#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace cubey::projects::ocean {
namespace {

struct OceanLodStats {
    std::uint32_t patches = 0;
    std::uint32_t triangles = 0;
    std::uint32_t vertices = 0;
};

[[nodiscard]] const char* cascade_role_name(std::uint32_t index) {
    switch (index) {
    case 0:
        return "core A";
    case 1:
        return "core B";
    case 2:
        return "candidate";
    case 3:
        return "candidate";
    case 4:
        return "candidate";
    }
    return "slot";
}

void format_cascade_label(char* buffer, std::size_t size, std::uint32_t index) {
    std::snprintf(buffer, size, "C%u %s", index, cascade_role_name(index));
}

void draw_map_size_combo(OceanConfig& config) {
    cubey::host::imgui_uint32_combo("Map size", &config.map_size, kOceanSupportedMapSizes,
                                    "FFT texture resolution for each ocean cascade.");
}

bool draw_cascade_map_size_combo(const char* label, std::uint32_t* value,
                                 std::uint32_t inherited_size) {
    std::array<std::uint32_t, kOceanSupportedMapSizes.size() + 1U> values{};
    values[0] = 0U;
    std::copy(kOceanSupportedMapSizes.begin(), kOceanSupportedMapSizes.end(), values.begin() + 1);

    const auto label_fn = [inherited_size](std::uint32_t candidate, char* buffer,
                                           std::size_t size) {
        if (candidate == 0U) {
            std::snprintf(buffer, size, "inherit %u", inherited_size);
            return;
        }
        std::snprintf(buffer, size, "%u", candidate);
    };
    return cubey::host::imgui_uint32_labeled_combo(
        label, value, values, label_fn,
        "Optional per-cascade FFT texture resolution. Inherit follows the global map size.");
}

[[nodiscard]] std::uint32_t ui_log2_exact(std::uint32_t value) {
    std::uint32_t result = 0;
    while (value > 1U) {
        value >>= 1U;
        ++result;
    }
    return result;
}

[[nodiscard]] std::uint32_t active_cascade_count(const OceanConfig& config) {
    std::uint32_t count = 0;
    for (const bool enabled : config.cascade_enabled) {
        count += enabled ? 1U : 0U;
    }
    return count;
}

[[nodiscard]] std::uint32_t steady_compute_dispatch_count(const OceanConfig& config) {
    std::uint32_t dispatches = 0;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (!ocean_cascade_enabled(config, cascade)) {
            continue;
        }
        const std::uint32_t fft_dispatches =
            kOceanSpectrumFieldCount * ui_log2_exact(ocean_cascade_map_size(config, cascade)) * 2U;
        dispatches += 1U + fft_dispatches + 1U;
    }
    return dispatches;
}

void draw_selected_cascade_combo(OceanDiagnosticsConfig& diagnostics) {
    char preview[40]{};
    if (diagnostics.selected_cascade < 0) {
        std::snprintf(preview, sizeof(preview), "All");
    } else {
        format_cascade_label(preview, sizeof(preview),
                             static_cast<std::uint32_t>(diagnostics.selected_cascade));
    }
    if (!ImGui::BeginCombo("Inspect cascade", preview)) {
        cubey::host::imgui_attach_help("Select which cascade is isolated in cascade debug views.");
        return;
    }
    const bool all_selected = diagnostics.selected_cascade < 0;
    if (ImGui::Selectable("All", all_selected)) {
        diagnostics.selected_cascade = -1;
    }
    if (all_selected) {
        ImGui::SetItemDefaultFocus();
    }
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        char label[32]{};
        format_cascade_label(label, sizeof(label), cascade);
        const bool selected = diagnostics.selected_cascade == static_cast<int>(cascade);
        if (ImGui::Selectable(label, selected)) {
            diagnostics.selected_cascade = static_cast<int>(cascade);
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
    cubey::host::imgui_attach_help("Select which cascade is isolated in cascade debug views.");
}

void request_camera_preset(OceanUiContext& ui, OceanCameraPreset preset) {
    ui.camera_preset = preset;
    ui.camera_preset_requested = true;
}

void apply_feature_preset(OceanUiContext& ui, const char* preset) {
    if (std::string_view{preset} == "exp-full") {
        ui.config.cascade_enabled.fill(true);
        ui.config.surface_shape_strength = 1.0F;
        ui.config.surface_foam_strength = 1.0F;
        ui.config.foam_history_strength = 1.0F;
        ui.config.atmosphere_material_strength = 1.0F;
        ui.config.atmosphere_sky_strength = 1.0F;
        ui.config.atmosphere_reflection_strength = 1.0F;
        ui.config.atmosphere_light_strength = 1.0F;
        ui.config.foam_lighting_strength = 1.0F;
        ui.config.self_shadow_strength = 0.45F;
        ui.config.terrain_foam_strength = 1.0F;
        ui.config.shape_fade_distance_scale = 1.0F;
        ui.config.normal_fade_distance_scale = 1.0F;
        ui.config.foam_fade_distance_scale = 1.0F;
        ui.config.far_field_enabled = true;
        ui.config.far_roughness_strength = 0.12F;
        ui.config.far_glint_strength = 0.28F;
        ui.config.far_detail_footprint_start_m = 0.9F;
        ui.config.far_detail_footprint_end_m = 5.0F;
        ui.config.far_reflection_variation_strength = 0.08F;
        ui.config.sun_glitter_width = 0.10F;
        ui.diagnostics.shape_anti_repeat_strength = 1.0F;
        ui.diagnostics.detail_anti_repeat_strength = 1.0F;
        ui.config.spectral_domains_enabled = true;
        return;
    }
    if (std::string_view{preset} == "ref-like") {
        ui.config.cascade_enabled = {true, true, false, false, false};
        ui.config.surface_shape_strength = 1.0F;
        ui.config.surface_foam_strength = 1.0F;
        ui.config.foam_history_strength = 1.0F;
        ui.config.atmosphere_material_strength = 1.0F;
        ui.config.atmosphere_sky_strength = 1.0F;
        ui.config.atmosphere_reflection_strength = 1.0F;
        ui.config.atmosphere_light_strength = 1.0F;
        ui.config.foam_lighting_strength = 1.0F;
        ui.config.self_shadow_strength = 0.35F;
        ui.config.terrain_foam_strength = 0.0F;
        ui.config.shape_fade_distance_scale = 1.0F;
        ui.config.normal_fade_distance_scale = 1.0F;
        ui.config.foam_fade_distance_scale = 1.0F;
        ui.config.far_field_enabled = false;
        ui.config.far_roughness_strength = 0.0F;
        ui.config.far_glint_strength = 0.0F;
        ui.config.far_detail_footprint_start_m = 0.9F;
        ui.config.far_detail_footprint_end_m = 5.0F;
        ui.config.far_reflection_variation_strength = 0.0F;
        ui.config.sun_glitter_width = 0.10F;
        ui.diagnostics.shape_anti_repeat_strength = 0.0F;
        ui.diagnostics.detail_anti_repeat_strength = 0.0F;
        ui.config.spectral_domains_enabled = false;
        return;
    }
    if (std::string_view{preset} == "cheap") {
        ui.config.cascade_enabled = {true, true, false, false, false};
        ui.config.surface_shape_strength = 1.0F;
        ui.config.surface_foam_strength = 0.85F;
        ui.config.foam_history_strength = 0.65F;
        ui.config.atmosphere_material_strength = 1.0F;
        ui.config.atmosphere_sky_strength = 0.65F;
        ui.config.atmosphere_reflection_strength = 0.35F;
        ui.config.atmosphere_light_strength = 1.0F;
        ui.config.foam_lighting_strength = 0.55F;
        ui.config.self_shadow_strength = 0.0F;
        ui.config.terrain_foam_strength = 0.0F;
        ui.config.shape_fade_distance_scale = 0.85F;
        ui.config.normal_fade_distance_scale = 0.80F;
        ui.config.foam_fade_distance_scale = 0.80F;
        ui.config.far_field_enabled = false;
        ui.config.far_roughness_strength = 0.0F;
        ui.config.far_glint_strength = 0.0F;
        ui.config.far_detail_footprint_start_m = 0.9F;
        ui.config.far_detail_footprint_end_m = 5.0F;
        ui.config.far_reflection_variation_strength = 0.0F;
        ui.config.sun_glitter_width = 0.10F;
        ui.diagnostics.shape_anti_repeat_strength = 0.0F;
        ui.diagnostics.detail_anti_repeat_strength = 0.0F;
        ui.config.spectral_domains_enabled = false;
        return;
    }
    if (std::string_view{preset} == "no-history") {
        ui.config.foam_history_strength = 0.0F;
        return;
    }
    if (std::string_view{preset} == "static-material") {
        ui.config.atmosphere_material_strength = 0.0F;
        ui.config.atmosphere_sky_strength = 0.0F;
        ui.config.atmosphere_reflection_strength = 0.0F;
        ui.config.atmosphere_light_strength = 0.0F;
        ui.config.foam_lighting_strength = 0.0F;
        ui.config.self_shadow_strength = 0.0F;
    }
}

void draw_feature_preset_button(OceanUiContext& ui, const char* label, const char* preset,
                                const char* help) {
    if (cubey::host::imgui_button(label, help)) {
        apply_feature_preset(ui, preset);
    }
}

void draw_camera_preset_button(OceanUiContext& ui, OceanCameraPreset preset, const char* label) {
    const bool selected = ui.camera_preset == preset;
    if (selected) {
        ImGui::BeginDisabled();
    }
    if (cubey::host::imgui_button(label, "Move the camera to this inspection preset.")) {
        request_camera_preset(ui, preset);
    }
    if (selected) {
        ImGui::EndDisabled();
    }
}

void draw_cascade_controls(OceanCascadeConfig& cascade, std::uint32_t index) {
    char label[40]{};
    format_cascade_label(label, sizeof(label), index);
    const cubey::host::ScopedImGuiGroup group(
        label, {.default_open = false,
                .level = 1U,
                .help = "Per-slot spectral wave controls. Enable a slot from Feature Isolation "
                        "to include it in compute and surface sampling."});
    if (!group) {
        return;
    }

    const cubey::host::ScopedImGuiId section_id(label);
    cubey::host::imgui_slider_float("Tile length", &cascade.tile_length, 8.0F, 2400.0F, "%.0f m",
                                    "World-space repeat length for this cascade.");
    cubey::host::imgui_slider_float("Displacement scale", &cascade.displacement_scale, 0.0F, 2.0F,
                                    "%.2f",
                                    "Vertical and horizontal displacement contribution.");
    cubey::host::imgui_slider_float("Normal scale", &cascade.normal_scale, 0.0F, 2.0F, "%.2f",
                                    "Surface-normal contribution from this cascade.");
    cubey::host::imgui_slider_float("Wind speed", &cascade.wind_speed, 0.1F, 32.0F, "%.1f m/s",
                                    "Wind speed used by the spectral wave model.");
    cubey::host::imgui_slider_float("Wind direction", &cascade.wind_direction_degrees, -180.0F,
                                    180.0F, "%.0f deg",
                                    "Dominant wind direction for this wave scale.");
    cubey::host::imgui_slider_float("Fetch", &cascade.fetch_length_km, 10.0F, 1200.0F, "%.0f km",
                                    "Effective distance over which wind transfers energy.");
    cubey::host::imgui_slider_float("Swell", &cascade.swell, 0.0F, 2.0F, "%.2f",
                                    "Boost for organized long-travel wave energy.");
    cubey::host::imgui_slider_float("Spread", &cascade.spread, 0.0F, 1.0F, "%.2f",
                                    "Directional spread around the dominant wind.");
    cubey::host::imgui_slider_float("Detail", &cascade.detail, 0.0F, 1.0F, "%.2f",
                                    "High-frequency detail weight inside this cascade.");
    cubey::host::imgui_slider_float("Whitecap", &cascade.whitecap, 0.0F, 2.0F, "%.2f",
                                    "Crest threshold bias used by foam generation.");
    cubey::host::imgui_slider_float("Foam amount", &cascade.foam_amount, 0.0F, 10.0F, "%.2f",
                                    "Foam contribution produced by this cascade.");
    cubey::host::imgui_slider_float(
        "Domain min waves", &cascade.domain_min_waves, 0.0F, 8.0F, "%.2f",
        "Minimum number of wavelengths admitted by spectral-domain filtering. Zero disables this "
        "slot's spectral cutoff.");
}

void draw_lod_diagnostics(const OceanSurfaceFrame& surface_frame) {
    const OceanConfig& config = surface_frame.mesh_config;
    const OceanHorizonDiagnostics& horizon = surface_frame.horizon;

    ImGui::Text("Effective mesh: %u cells / %u LOD / %.1f km half",
                config.mesh_cells, config.mesh_lod_levels,
                horizon.mesh_half_extent_m / kOceanMetersPerKilometer);
    ImGui::Text("Cell size: near %.2f m / target %.2f m / far %.2f m",
                ocean_mesh_near_cell_size(config), horizon.target_near_cell_size_m,
                horizon.far_cell_size_m);
    ImGui::Text("Horizon coverage: %.1f km required / %.0f%% / camera %.1f m",
                horizon.required_half_extent_m / kOceanMetersPerKilometer,
                horizon.coverage_ratio * 100.0F, horizon.camera_altitude_m);

    std::array<OceanLodStats, kOceanMaxMeshLodLevels> stats{};
    const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(config);
    for (const OceanMeshPatch& patch : patches) {
        OceanLodStats& level = stats[patch.level];
        ++level.patches;
        level.triangles += ocean_mesh_patch_triangle_count(patch);
        level.vertices += ocean_mesh_patch_vertex_count(patch);
    }

    if (!ImGui::BeginTable("LOD breakdown", 5,
                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("Level");
    ImGui::TableSetupColumn("Cell");
    ImGui::TableSetupColumn("Patches");
    ImGui::TableSetupColumn("Tris");
    ImGui::TableSetupColumn("Half");
    ImGui::TableHeadersRow();
    for (std::uint32_t level = 0; level < config.mesh_lod_levels; ++level) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%u", level);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f", ocean_mesh_level_cell_size(config, level));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%u", stats[level].patches);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%u", stats[level].triangles);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.0f", ocean_mesh_level_half_extent(config, level));
    }
    ImGui::EndTable();

    if (!ImGui::BeginTable("Cascade LOD bands", 7,
                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("Cascade");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("Tile");
    ImGui::TableSetupColumn("Shape m");
    ImGui::TableSetupColumn("Surface m");
    ImGui::TableSetupColumn("Mesh cell");
    ImGui::TableSetupColumn("Horizon wt");
    ImGui::TableHeadersRow();
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        const OceanCascadeConfig& cascade_config = ocean_cascade(config, cascade);
        const OceanCascadeLodBand lod = ocean_cascade_lod_band(config, cascade);
        const float shape_weight =
            ocean_cascade_displacement_lod_weight(config, cascade, horizon.horizon_distance_m,
                                                  horizon.far_cell_size_m);
        const float surface_weight =
            ocean_cascade_surface_lod_weight(config, cascade, horizon.horizon_distance_m);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("C%u", cascade);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", ocean_cascade_enabled(config, cascade) ? "on" : "off");
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.0f", cascade_config.tile_length);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.0f-%.0f", lod.displacement_fade_start, lod.displacement_fade_end);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.0f-%.0f", lod.surface_fade_start, lod.surface_fade_end);
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%.1f-%.1f", lod.mesh_cell_full, lod.mesh_cell_zero);
        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%.2f / %.2f", shape_weight, surface_weight);
    }
    ImGui::EndTable();
}

} // namespace

void draw_ocean_ui(OceanUiContext ui) {
    ui.config.sea_state = ocean_infer_sea_state(ui.config);
    if (!cubey::host::begin_control_panel("Ocean", {.width = 430.0F})) {
        ImGui::End();
        return;
    }

    cubey::host::imgui_checkbox("Paused", &ui.paused, "Pause ocean time integration.");
    ImGui::SameLine();
    if (cubey::host::imgui_button("Reset", "Reset ocean time and regenerate spectrum state.")) {
        ui.reset_requested = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ui.paused);
    if (cubey::host::imgui_button("Step", "Advance one ocean frame while paused.")) {
        ui.step_requested = true;
    }
    ImGui::EndDisabled();
    cubey::host::imgui_enum_combo("Debug view", ui.render_view, kOceanRenderViews,
                                  ocean_render_view_name,
                                  "Inspect final shading, wave fields, foam, lighting, and LOD.");
    draw_selected_cascade_combo(ui.diagnostics);
    cubey::host::imgui_checkbox("Wire overlay", &ui.diagnostics.wire_overlay,
                                "Draw mesh topology over the ocean surface.");
    ImGui::BeginDisabled(!ui.diagnostics.wire_overlay);
    cubey::host::imgui_slider_float("Wire opacity", &ui.diagnostics.wire_opacity, 0.05F, 1.0F,
                                    "%.2f", "Opacity used by the wire overlay.");
    ImGui::EndDisabled();
    cubey::host::imgui_checkbox(
        "Size pillar", &ui.diagnostics.size_reference_enabled,
        "Draw the 50 m sea-level-centered scale pillar and its basic direct-light ocean shadow.");

    if (const cubey::host::ScopedImGuiGroup group{
            "Wave Core", {.help = "Global controls for the FFT wave field and terrain coupling."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Wave Core");
        OceanSeaState sea_state = ui.config.sea_state;
        if (cubey::host::imgui_enum_combo(
                "Sea state", sea_state, kOceanSeaStatePresets, ocean_sea_state_name,
                "Apply a coherent wave and foam preset. Manual edits are reported as custom.")) {
            apply_ocean_sea_state(ui.config, sea_state);
        }
        draw_map_size_combo(ui.config);
        cubey::host::imgui_enum_combo(
            "Field precision", ui.config.field_precision, kOceanFieldPrecisions,
            ocean_field_precision_name,
            "Storage precision for ocean FFT textures. Half is the default practical path; full "
            "is useful for comparison captures.");
        cubey::host::imgui_checkbox(
            "Spectral domains", &ui.config.spectral_domains_enabled,
            "Limit cascades to wavelength bands so scales blend without duplicating energy.");
        cubey::host::imgui_slider_float(
            "Shape anti-repeat", &ui.diagnostics.shape_anti_repeat_strength, 0.0F, 1.0F, "%.2f",
            "Strength of the secondary displacement sample used to break tiling.");
        cubey::host::imgui_slider_float(
            "Detail anti-repeat", &ui.diagnostics.detail_anti_repeat_strength, 0.0F, 1.0F,
            "%.2f", "Strength of far-field normal/foam domain perturbation.");
        cubey::host::imgui_slider_float("Depth", &ui.config.depth, 2.0F, 80.0F, "%.1f m",
                                        "Water depth used by the dispersion model.");
        cubey::host::imgui_checkbox(
            "Terrain field influence", &ui.config.terrain_fields_enabled,
            "Use terrain-ocean fields to affect the ocean instead of diagnostics only.");
        ImGui::TextUnformatted("GodotOceanWaves port");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Feature Isolation",
            {.default_open = false,
             .help = "Strength gates and slot toggles around the core C0/C1 wave pair."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Feature Isolation");
        draw_feature_preset_button(ui, "All slots", "exp-full",
                                   "Enable every cascade slot for comparison.");
        ImGui::SameLine();
        draw_feature_preset_button(ui, "Core", "ref-like", "Use the default C0/C1 core slots.");
        ImGui::SameLine();
        draw_feature_preset_button(ui, "Cheap", "cheap",
                                   "Keep the material readable while reducing expensive extras.");
        draw_feature_preset_button(ui, "No history", "no-history",
                                   "Disable persistent foam history contribution.");
        ImGui::SameLine();
        draw_feature_preset_button(ui, "Static material", "static-material",
                                   "Blend water material sampling away from atmosphere probes.");

        ImGui::TextUnformatted("Active cascade work");
        cubey::host::imgui_attach_help(
            "Disabled cascades skip spectrum, modulation, FFT, and unpack dispatches and are "
            "also hidden from the surface shader.");
        for (std::uint32_t index = 0; index < kOceanCascadeCount; ++index) {
            if (index > 0U) {
                ImGui::SameLine();
            }
            char label[8]{};
            std::snprintf(label, sizeof(label), "C%u", index);
            cubey::host::imgui_checkbox(label, &ui.config.cascade_enabled[index],
                                        "Enable this cascade's compute work and surface samples.");
        }
        ImGui::Text("Active cascades: %u/%u", active_cascade_count(ui.config),
                    kOceanCascadeCount);
        ImGui::Text("Steady compute dispatches: %u/frame",
                    steady_compute_dispatch_count(ui.config));
        if (ImGui::BeginTable("Cascade work policy", 4,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Cascade");
            ImGui::TableSetupColumn("Map");
            ImGui::TableSetupColumn("Interval");
            ImGui::TableSetupColumn("Effective");
            ImGui::TableHeadersRow();
            for (std::uint32_t index = 0; index < kOceanCascadeCount; ++index) {
                char id[8]{};
                std::snprintf(id, sizeof(id), "C%u", index);
                const cubey::host::ScopedImGuiId row_id(id);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("C%u", index);
                ImGui::TableSetColumnIndex(1);
                draw_cascade_map_size_combo("##map", &ui.config.cascade_map_sizes[index],
                                            ui.config.map_size);
                ImGui::TableSetColumnIndex(2);
                int interval = static_cast<int>(ui.config.cascade_update_intervals[index]);
                if (cubey::host::imgui_slider_int("##interval", &interval, 1, 8,
                                                  "Frame interval for recomputing this cascade.")) {
                    ui.config.cascade_update_intervals[index] =
                        static_cast<std::uint32_t>(std::clamp(interval, 1, 8));
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u / %u frame%s", ocean_cascade_map_size(ui.config, index),
                            ocean_cascade_update_interval(ui.config, index),
                            ocean_cascade_update_interval(ui.config, index) == 1U ? "" : "s");
            }
            ImGui::EndTable();
        }
        cubey::host::imgui_slider_float(
            "Shape strength", &ui.config.surface_shape_strength, 0.0F, 1.5F, "%.2f",
            "Global displacement and normal strength for enabled cascade slots.");
        cubey::host::imgui_slider_float(
            "Foam strength", &ui.config.surface_foam_strength, 0.0F, 2.0F, "%.2f",
            "Global presentation foam strength for enabled cascade slots.");
        cubey::host::imgui_slider_float("Foam history", &ui.config.foam_history_strength, 0.0F,
                                        1.5F, "%.2f",
                                        "Persistent foam history contribution to final coverage.");
        cubey::host::imgui_slider_float(
            "Atmosphere master", &ui.config.atmosphere_material_strength, 0.0F, 1.0F, "%.2f",
            "Master blend for runtime atmosphere influence on the water material.");
        cubey::host::imgui_slider_float(
            "Sky radiance", &ui.config.atmosphere_sky_strength, 0.0F, 1.0F, "%.2f",
            "Blend ambient sky and horizon fog from static sky to runtime atmosphere radiance.");
        cubey::host::imgui_slider_float(
            "Reflection probe", &ui.config.atmosphere_reflection_strength, 0.0F, 1.0F, "%.2f",
            "Blend water reflections from sky radiance to the runtime atmosphere reflection probe.");
        cubey::host::imgui_slider_float(
            "Light response", &ui.config.atmosphere_light_strength, 0.0F, 1.0F, "%.2f",
            "Blend direct and ambient material energy from static daylight to runtime sun/moon.");
        cubey::host::imgui_slider_float(
            "Foam lighting", &ui.config.foam_lighting_strength, 0.0F, 1.0F, "%.2f",
            "Blend foam shading from static brightness to runtime day/night lighting.");
        cubey::host::imgui_slider_float("Terrain foam", &ui.config.terrain_foam_strength, 0.0F,
                                        2.0F, "%.2f",
                                        "Strength of optional shoreline foam from terrain fields.");
        cubey::host::imgui_slider_float(
            "Shape fade", &ui.config.shape_fade_distance_scale, 0.25F, 2.5F, "%.2f",
            "Distance multiplier for per-cascade displacement fade-out.");
        cubey::host::imgui_slider_float(
            "Normal fade", &ui.config.normal_fade_distance_scale, 0.25F, 2.5F, "%.2f",
            "Distance multiplier for far-field normal fade-out.");
        cubey::host::imgui_slider_float(
            "Foam fade", &ui.config.foam_fade_distance_scale, 0.25F, 2.5F, "%.2f",
            "Distance multiplier for persistent/current foam fade-out.");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Camera",
            {.default_open = false,
             .help = "Quick camera viewpoints for inspecting wave shape and scale."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Camera");
        draw_camera_preset_button(ui, OceanCameraPreset::Default, "Default");
        ImGui::SameLine();
        draw_camera_preset_button(ui, OceanCameraPreset::Low, "Low");
        ImGui::SameLine();
        draw_camera_preset_button(ui, OceanCameraPreset::Mid, "Mid");
        ImGui::SameLine();
        draw_camera_preset_button(ui, OceanCameraPreset::High, "High");
        ImGui::SameLine();
        draw_camera_preset_button(ui, OceanCameraPreset::Close, "Close");
        ImGui::SameLine();
        draw_camera_preset_button(ui, OceanCameraPreset::Overhead, "Overhead");
        ImGui::SameLine();
        draw_camera_preset_button(ui, OceanCameraPreset::Wide, "Wide");
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Mesh",
            {.default_open = false,
             .help = "Camera-relative ocean mesh and clipmap LOD controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Mesh");
        int mesh_cells = static_cast<int>(ui.config.mesh_cells);
        if (cubey::host::imgui_slider_int(
                "Base cells", &mesh_cells, static_cast<int>(kOceanMinMeshCells),
                static_cast<int>(kOceanMaxMeshCells), "Grid resolution per clipmap patch.")) {
            ui.config.mesh_cells = static_cast<std::uint32_t>(
                std::clamp(mesh_cells, static_cast<int>(kOceanMinMeshCells),
                           static_cast<int>(kOceanMaxMeshCells)));
        }
        int mesh_lod_levels = static_cast<int>(ui.config.mesh_lod_levels);
        if (cubey::host::imgui_slider_int(
                "LOD levels", &mesh_lod_levels, static_cast<int>(kOceanMinMeshLodLevels),
                static_cast<int>(kOceanMaxMeshLodLevels), "Number of concentric mesh LOD rings.")) {
            ui.config.mesh_lod_levels = static_cast<std::uint32_t>(
                std::clamp(mesh_lod_levels, static_cast<int>(kOceanMinMeshLodLevels),
                           static_cast<int>(kOceanMaxMeshLodLevels)));
        }
        cubey::host::imgui_slider_float("Extent", &ui.config.mesh_extent, 400.0F, 128000.0F,
                                        "%.0f m",
                                        "Minimum half extent covered by the ocean mesh.");
        cubey::host::imgui_checkbox(
            "Auto horizon", &ui.config.horizon_auto_extent,
            "Derive effective mesh extent from camera altitude and planet radius.");
        cubey::host::imgui_slider_float("Horizon margin", &ui.config.horizon_extent_margin, 1.0F,
                                        2.0F, "%.2f",
                                        "Safety multiplier applied to the computed horizon.");
        cubey::host::imgui_slider_float("Target near cell",
                                        &ui.config.horizon_target_near_cell_m, 0.5F, 8.0F,
                                        "%.2f m",
                                        "Preferred near-field cell size when auto horizon is on.");
        cubey::host::imgui_slider_float(
            "Altitude cell ratio", &ui.config.horizon_altitude_cell_ratio, 0.0F, 0.08F, "%.3f",
            "High-camera multiplier for the effective near-cell target. Larger values make high "
            "views use fewer mesh cells.");
        cubey::host::imgui_slider_float("Horizon fog", &ui.config.horizon_fog, 0.0F, 1.0F, "%.2f",
                                        "Distance fade used to soften the horizon.");
        cubey::host::imgui_enum_combo("Surface mode", ui.config.surface_mode, kOceanSurfaceModes,
                                      ocean_surface_mode_name,
                                      "Flat keeps the old tangent plane; curved-far bends the far field.");
        cubey::host::imgui_slider_float(
            "Planet scale", &ui.config.planet_radius_scale, 0.02F, 2.0F, "%.2f",
            "Scale applied to the ocean surface radius. Lower values exaggerate curvature.");
        cubey::host::imgui_slider_float(
            "Curve start", &ui.config.curvature_start_ratio, 0.0F, 0.95F, "%.2f horizon",
            "Fraction of horizon distance where spherical far-surface drop begins.");
        cubey::host::imgui_slider_float(
            "Curve end", &ui.config.curvature_end_ratio, 0.05F, 1.0F, "%.2f horizon",
            "Fraction of horizon distance where spherical far-surface drop reaches full strength.");
        cubey::host::imgui_slider_float("Curve strength", &ui.config.curvature_strength, 0.0F,
                                        1.0F, "%.2f",
                                        "Strength of curved far-ocean surface mapping.");
        if (ui.config.curvature_start_ratio >= ui.config.curvature_end_ratio) {
            ui.config.curvature_end_ratio =
                std::min(1.0F, ui.config.curvature_start_ratio + 0.01F);
            ui.config.curvature_start_ratio =
                std::min(ui.config.curvature_start_ratio, ui.config.curvature_end_ratio - 0.01F);
        }
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Shading",
            {.default_open = false,
             .help = "Surface material, foam, color, and exposure controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Shading");
        cubey::host::imgui_slider_float("Roughness", &ui.config.roughness, 0.0F, 1.0F, "%.2f",
                                        "Microfacet roughness used by ocean shading.");
        cubey::host::imgui_slider_float("Normal strength", &ui.config.normal_strength, 0.0F, 2.0F,
                                        "%.2f", "Final normal-map intensity.");
        cubey::host::imgui_slider_float(
            "Spectral LOD", &ui.config.spectral_lod_handoff, 0.0F, 1.0F, "%.2f",
            "Blend unresolved wave slope and foam statistics into stable distant material response.");
        cubey::host::imgui_checkbox(
            "Far field", &ui.config.far_field_enabled,
            "Hand unresolved distant wave energy into roughness, reflection, and glints.");
        ImGui::BeginDisabled(!ui.config.far_field_enabled);
        cubey::host::imgui_slider_float("Far start", &ui.config.far_field_start_m, 50.0F,
                                        1800.0F, "%.0f m",
                                        "Distance where statistical far-field material response begins.");
        cubey::host::imgui_slider_float("Far end", &ui.config.far_field_end_m, 200.0F, 5000.0F,
                                        "%.0f m",
                                        "Distance where statistical far-field material response reaches full strength.");
        if (ui.config.far_field_end_m <= ui.config.far_field_start_m) {
            ui.config.far_field_end_m = ui.config.far_field_start_m + 1.0F;
        }
        cubey::host::imgui_slider_float(
            "Far roughness", &ui.config.far_roughness_strength, 0.0F, 0.5F, "%.2f",
            "Roughness increase driven by unresolved distant wave energy.");
        cubey::host::imgui_slider_float(
            "Sun glitter", &ui.config.far_glint_strength, 0.0F, 0.8F, "%.2f",
            "Far-field specular strength along the reflected sun corridor.");
        cubey::host::imgui_slider_float(
            "Glitter width", &ui.config.sun_glitter_width, 0.02F, 0.40F, "%.2f",
            "Angular width of the far-field reflected-sun corridor.");
        cubey::host::imgui_slider_float(
            "Far detail start", &ui.config.far_detail_footprint_start_m, 0.1F, 6.0F, "%.2f m",
            "Pixel footprint where distant normal detail starts fading into material response.");
        cubey::host::imgui_slider_float(
            "Far detail end", &ui.config.far_detail_footprint_end_m, 0.2F, 16.0F, "%.2f m",
            "Pixel footprint where distant normal detail reaches its minimum residual strength.");
        if (ui.config.far_detail_footprint_end_m <= ui.config.far_detail_footprint_start_m) {
            ui.config.far_detail_footprint_end_m = ui.config.far_detail_footprint_start_m + 0.01F;
        }
        cubey::host::imgui_slider_float(
            "Far reflection patches", &ui.config.far_reflection_variation_strength, 0.0F,
            0.3F, "%.2f",
            "Broad low-frequency variation applied to distant reflected sky and atmosphere.");
        ImGui::EndDisabled();
        cubey::host::imgui_slider_float(
            "Cloud reflection", &ui.config.cloud_reflection_strength, 0.0F, 1.0F, "%.2f",
            "Strength of the selected cloud environment in ocean reflections.");
        cubey::host::imgui_enum_combo(
            "Reflection source", ui.config.cloud_reflection_source,
            kOceanCloudReflectionSources, ocean_cloud_reflection_source_name,
            "Current-view is the existing low-cost projection; cached captures all directions; hybrid overlays sharp current-view detail.");
        ImGui::BeginDisabled(ui.config.cloud_reflection_source ==
                             OceanCloudReflectionSource::CurrentView);
        constexpr std::array<std::uint32_t, 3> cloud_environment_extents{32U, 64U, 128U};
        cubey::host::imgui_uint32_combo(
            "Probe extent", &ui.config.cloud_environment_extent, cloud_environment_extents,
            "Resolution per face of the cached six-direction cloud environment.");
        cubey::host::imgui_slider_float(
            "Probe rate", &ui.config.cloud_environment_update_hz, 0.5F, 30.0F, "%.1f Hz",
            "Coherent six-face refresh rate; captures crossfade over one refresh interval.");
        ImGui::EndDisabled();
        cubey::host::imgui_slider_float(
            "Cloud shadow", &ui.config.cloud_shadow_strength, 0.0F, 1.0F, "%.2f",
            "Strength of shared cloud transmittance on direct ocean lighting.");
        cubey::host::imgui_slider_float(
            "Wave shadow", &ui.config.self_shadow_strength, 0.0F, 1.0F, "%.2f",
            "Strength of experimental heightfield ray-marched wave self-shadowing.");
        cubey::host::imgui_slider_float(
            "Shadow reach", &ui.config.self_shadow_distance, 4.0F, 160.0F, "%.0f m",
            "Maximum distance marched through the wave heightfield toward the light.");
        cubey::host::imgui_slider_float(
            "Shadow bias", &ui.config.self_shadow_bias, 0.0F, 1.0F, "%.2f m",
            "Height bias used to reduce self-shadow acne on the current wave surface.");
        int self_shadow_steps = static_cast<int>(ui.config.self_shadow_steps);
        if (cubey::host::imgui_slider_int(
                "Shadow steps", &self_shadow_steps, 1, 24,
                "Heightfield samples used by experimental wave self-shadowing.")) {
            ui.config.self_shadow_steps =
                static_cast<std::uint32_t>(std::clamp(self_shadow_steps, 1, 24));
        }
        cubey::host::imgui_slider_float("Foam density", &ui.config.foam_density, 0.0F, 4.0F, "%.2f",
                                        "Global density multiplier for rendered foam.");
        cubey::host::imgui_slider_float("Foam sharpness", &ui.config.foam_sharpness, 0.0F, 1.0F,
                                        "%.2f", "Contrast of the foam mask.");
        cubey::host::imgui_slider_float("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f",
                                        "Manual exposure bias in stops.");
        cubey::host::imgui_color_edit3("Water", &ui.config.water_color_r,
                                       "Base water tint before lighting.");
        cubey::host::imgui_color_edit3("Foam", &ui.config.foam_color_r,
                                       "Foam tint before lighting.");
    }

    ui.atmosphere_changed =
        cubey::host::draw_atmosphere_environment_controls(ui.atmosphere, {.default_open = false}) ||
        ui.atmosphere_changed;

    static_cast<void>(cubey::host::draw_cloud_environment_controls(
        ui.clouds,
        {.help = "Shared surface-view cloud layer composited over the ocean sky.",
         .show_aerial_orbit_controls = false}));

    if (const cubey::host::ScopedImGuiGroup group{
            "Cascades",
            {.default_open = false,
             .help = "Wave cascade slots. Defaults use C0/C1; other slots are opt-in."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Cascades");
        for (std::uint32_t index = 0; index < kOceanCascadeCount; ++index) {
            draw_cascade_controls(ui.config.cascades[index], index);
        }
    }

    const std::array<cubey::host::PerformanceCounter, 6> performance_counters{
        cubey::host::PerformanceCounter{
            "Clip patches", ui.draw_stats.generated_patches, nullptr},
        cubey::host::PerformanceCounter{"Draw patches", ui.draw_stats.submitted_patches, nullptr},
        cubey::host::PerformanceCounter{"LOD levels",
                                        ui.surface_frame.mesh_config.mesh_lod_levels, nullptr},
        cubey::host::PerformanceCounter{"Clip tris", ui.draw_stats.generated_triangles, nullptr},
        cubey::host::PerformanceCounter{"Draw tris", ui.draw_stats.submitted_triangles, nullptr},
        cubey::host::PerformanceCounter{
            "Horizon cover",
            static_cast<std::uint64_t>(
                std::max(0.0F, ui.surface_frame.horizon.coverage_ratio) * 100.0F),
            "%"},
    };
    cubey::host::PerformanceUiContext performance = ui.performance;
    performance.counters = performance_counters;
    cubey::host::draw_performance_ui(performance);

    if (const cubey::host::ScopedImGuiGroup group{
            "Diagnostics",
            {.default_open = false, .help = "Read-only runtime, cascade, and mesh statistics."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        ImGui::Text("Map: %u", ui.config.map_size);
        ImGui::Text("Spectral domains: %s",
                    ui.config.spectral_domains_enabled ? "enabled" : "disabled");
        ImGui::Text("Active overlap: %u pair%s / %.2f octaves max",
                    ui.spectrum_diagnostics.overlapping_pair_count,
                    ui.spectrum_diagnostics.overlapping_pair_count == 1U ? "" : "s",
                    ui.spectrum_diagnostics.max_overlap_octaves);
        ImGui::Text("Terrain fields: %s",
                    ui.config.terrain_fields_enabled ? "influence enabled" : "diagnostic only");
        ImGui::Text("Cloud env: %s / gen %llu / blend %.2f / age %.2f s",
                    ui.cloud_environment_valid ? "ready" : "clear fallback",
                    static_cast<unsigned long long>(ui.cloud_environment_generation),
                    ui.cloud_environment_blend, ui.cloud_environment_age_seconds);
        ImGui::Text("Surface: %s", ocean_surface_mode_name(ui.surface_frame.surface_mode));
        ImGui::Text("Curvature: %.1f-%.1f km / strength %.2f",
                    ui.surface_frame.curvature_start_m / kOceanMetersPerKilometer,
                    ui.surface_frame.curvature_end_m / kOceanMetersPerKilometer,
                    ui.surface_frame.curvature_strength);
        ImGui::Text("Horizon drop: %.1f m",
                    ocean_surface_curvature_drop_m(ui.surface_frame.horizon.horizon_distance_m,
                                                   ui.surface_frame.local_frame.planet_radius_m,
                                                   ui.surface_frame.curvature_start_m,
                                                   ui.surface_frame.curvature_end_m,
                                                   ui.surface_frame.curvature_strength));
        ImGui::Text("Datum: %.1f m / planet %.0f km",
                    ui.surface_frame.local_frame.water_datum_m,
                    ui.surface_frame.local_frame.planet_radius_m / kOceanMetersPerKilometer);
        ImGui::Text("Frame origin: %.1f, %.1f, %.1f km",
                    ui.surface_frame.local_frame.world_origin_m.x / kOceanMetersPerKilometer,
                    ui.surface_frame.local_frame.world_origin_m.y / kOceanMetersPerKilometer,
                    ui.surface_frame.local_frame.world_origin_m.z / kOceanMetersPerKilometer);
        ImGui::Text("Frame up: %.2f, %.2f, %.2f",
                    ui.surface_frame.local_frame.up.x, ui.surface_frame.local_frame.up.y,
                    ui.surface_frame.local_frame.up.z);
        for (std::uint32_t index = 0; index < kOceanCascadeCount; ++index) {
            if (!ocean_cascade_enabled(ui.config, index)) {
                continue;
            }
            char label[40]{};
            format_cascade_label(label, sizeof(label), index);
            const OceanCascadeSpectrumDiagnostics& spectrum =
                ui.spectrum_diagnostics.cascades[index];
            ImGui::Text("%s: peak %.1f m / safe %.2f-%.1f m / %s", label,
                        spectrum.peak_wavelength_m, spectrum.safe_min_wavelength_m,
                        spectrum.max_wavelength_m,
                        spectrum.peak_supported ? "peak fits" : "peak omitted");
            ImGui::Text("    domain %s / disp %.2f", spectrum.domain_active ? "active" : "inactive",
                        ui.config.cascades[index].displacement_scale);
        }
        ImGui::Text("Mesh: %u LOD / %u patches / %u tris",
                    ui.surface_frame.mesh_config.mesh_lod_levels,
                    ui.draw_stats.generated_patches, ui.draw_stats.generated_triangles);
        ImGui::Text("Submitted: %u patches / %u tris / culled %u patches",
                    ui.draw_stats.submitted_patches, ui.draw_stats.submitted_triangles,
                    ui.draw_stats.culled_patches);
        ImGui::Text("Base cells: %u configured / %u effective", ui.config.mesh_cells,
                    ui.surface_frame.mesh_config.mesh_cells);
        ImGui::Text("Near cell: %.2f m", ocean_mesh_near_cell_size(ui.surface_frame.mesh_config));
        ImGui::Text("Target near cell: %.2f m",
                    ui.surface_frame.horizon.target_near_cell_size_m);
        ImGui::Text("Far cell: %.2f m", ui.surface_frame.horizon.far_cell_size_m);
        ImGui::Text("Camera altitude: %.1f m", ui.surface_frame.horizon.camera_altitude_m);
        ImGui::Text("Horizon: %.1f km / required extent %.1f km",
                    ui.surface_frame.horizon.horizon_distance_m / kOceanMetersPerKilometer,
                    ui.surface_frame.horizon.required_half_extent_m / kOceanMetersPerKilometer);
        ImGui::Text("Mesh extent: %.1f km / coverage %.0f%%",
                    ui.surface_frame.horizon.mesh_half_extent_m / kOceanMetersPerKilometer,
                    ui.surface_frame.horizon.coverage_ratio * 100.0F);
        ImGui::Text("Vertices: %u",
                    ocean_mesh_total_vertex_count(ui.surface_frame.mesh_config));
        draw_lod_diagnostics(ui.surface_frame);
    }

    ui.config.sea_state = ocean_infer_sea_state(ui.config);
    ImGui::End();
}

} // namespace cubey::projects::ocean
