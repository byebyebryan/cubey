#include "ocean_ui.h"

#include "ocean_mesh.h"

#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

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
        return "Macro swell";
    case 1:
        return "Macro chop";
    case 2:
        return "Primary crest";
    case 3:
        return "Secondary wave";
    case 4:
        return "Detail normals";
    }
    return "Cascade";
}

void format_cascade_label(char* buffer, std::size_t size, std::uint32_t index) {
    std::snprintf(buffer, size, "C%u %s", index, cascade_role_name(index));
}

void draw_map_size_combo(OceanConfig& config) {
    char preview[40]{};
    std::snprintf(preview, sizeof(preview), "%u", config.map_size);
    if (ImGui::BeginCombo("Map size", preview)) {
        for (const std::uint32_t map_size : kOceanSupportedMapSizes) {
            const bool selected = config.map_size == map_size;
            char label[16]{};
            std::snprintf(label, sizeof(label), "%u", map_size);
            if (ImGui::Selectable(label, selected)) {
                config.map_size = map_size;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    cubey::host::imgui_attach_help("FFT texture resolution for each ocean cascade.");
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

void draw_camera_preset_button(OceanUiContext& ui, OceanCameraPreset preset, const char* label) {
    const bool selected = ui.camera_preset == preset;
    if (selected) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(label)) {
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
                .help = "Per-scale spectral wave controls. Larger tile lengths drive macro "
                        "shape; smaller tile lengths contribute crest detail and foam."});
    if (!group) {
        return;
    }

    const cubey::host::ScopedImGuiId section_id(label);
    const bool macro_layer = index < kOceanMacroCascadeCount;
    cubey::host::imgui_slider_float("Tile length", &cascade.tile_length, 8.0F,
                                    macro_layer ? 2400.0F : 256.0F, "%.0f m",
                                    "World-space repeat length for this cascade.");
    cubey::host::imgui_slider_float("Displacement scale", &cascade.displacement_scale, 0.0F,
                                    macro_layer ? 1.50F : 2.0F, "%.2f",
                                    "Vertical and horizontal displacement contribution.");
    cubey::host::imgui_slider_float("Normal scale", &cascade.normal_scale, 0.0F,
                                    macro_layer ? 0.50F : 2.0F, "%.2f",
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
}

void draw_lod_diagnostics(const OceanConfig& config) {
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
}

} // namespace

void draw_ocean_ui(OceanUiContext ui) {
    if (!cubey::host::begin_control_panel("Ocean", {.width = 430.0F})) {
        ImGui::End();
        return;
    }

    cubey::host::imgui_checkbox("Paused", &ui.paused, "Pause ocean time integration.");
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.reset_requested = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ui.paused);
    if (ImGui::Button("Step")) {
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

    if (const cubey::host::ScopedImGuiGroup group{
            "Wave Core", {.help = "Global controls for the FFT wave field and terrain coupling."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Wave Core");
        draw_map_size_combo(ui.config);
        cubey::host::imgui_checkbox(
            "Spectral domains", &ui.config.spectral_domains_enabled,
            "Limit cascades to wavelength bands so scales blend without duplicating energy.");
        cubey::host::imgui_slider_float("Anti-repeat", &ui.diagnostics.anti_repeat_strength, 0.0F,
                                        1.0F, "%.2f",
                                        "Strength of domain perturbation used to break tiling.");
        cubey::host::imgui_slider_float("Depth", &ui.config.depth, 2.0F, 80.0F, "%.1f m",
                                        "Water depth used by the dispersion model.");
        cubey::host::imgui_checkbox(
            "Terrain field influence", &ui.config.terrain_fields_enabled,
            "Use terrain-ocean fields to affect the ocean instead of diagnostics only.");
        ImGui::TextUnformatted("GodotOceanWaves port");
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
        cubey::host::imgui_slider_float("Extent", &ui.config.mesh_extent, 400.0F, 9000.0F, "%.0f m",
                                        "Half extent covered by the ocean mesh.");
        cubey::host::imgui_slider_float("Horizon fog", &ui.config.horizon_fog, 0.0F, 1.0F, "%.2f",
                                        "Distance fade used to soften the horizon.");
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

    if (const cubey::host::ScopedImGuiGroup group{
            "Cascades",
            {.default_open = false,
             .help = "Wave cascades ordered from macro swell to fine surface detail."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Cascades");
        for (std::uint32_t index = 0; index < kOceanCascadeCount; ++index) {
            draw_cascade_controls(ui.config.cascades[index], index);
        }
    }

    if (const cubey::host::ScopedImGuiGroup group{
            "Diagnostics",
            {.default_open = false, .help = "Read-only runtime, cascade, and mesh statistics."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        cubey::host::draw_frame_stats(ui.latest_frame_stats, ui.latest_fps, ui.latest_frame_ms);
        ImGui::Text("Map: %u", ui.config.map_size);
        ImGui::Text("Spectral domains: %s",
                    ui.config.spectral_domains_enabled ? "enabled" : "disabled");
        ImGui::Text("Terrain fields: %s",
                    ui.config.terrain_fields_enabled ? "influence enabled" : "diagnostic only");
        for (std::uint32_t index = 0; index < kOceanCascadeCount; ++index) {
            char label[40]{};
            format_cascade_label(label, sizeof(label), index);
            const OceanCascadeDomain domain = ocean_cascade_domain(ui.config, index);
            if (domain.active) {
                ImGui::Text("%s: %.0f m / disp %.2f / domain %.2f-%.2f m", label,
                            ui.config.cascades[index].tile_length,
                            ui.config.cascades[index].displacement_scale, domain.low_wavelength,
                            domain.high_wavelength);
            } else {
                ImGui::Text("%s: %.0f m / disp %.2f / domain inactive", label,
                            ui.config.cascades[index].tile_length,
                            ui.config.cascades[index].displacement_scale);
            }
        }
        ImGui::Text("Mesh: %u LOD / %u patches / %u tris", ui.config.mesh_lod_levels,
                    ocean_mesh_patch_count(ui.config), ocean_mesh_total_triangle_count(ui.config));
        ImGui::Text("Near cell: %.2f m", ocean_mesh_near_cell_size(ui.config));
        ImGui::Text("Vertices: %u", ocean_mesh_total_vertex_count(ui.config));
        draw_lod_diagnostics(ui.config);
    }

    ImGui::End();
}

} // namespace cubey::projects::ocean
