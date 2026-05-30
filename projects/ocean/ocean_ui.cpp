#include "ocean_ui.h"

#include "ocean_mesh.h"

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace cubey::projects::ocean {
namespace {

struct OceanLodStats {
    std::uint32_t patches = 0;
    std::uint32_t triangles = 0;
    std::uint32_t vertices = 0;
};

void draw_map_size_combo(OceanConfig& config) {
    char preview[16]{};
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
}

void draw_cascade_controls(OceanCascadeConfig& cascade, std::uint32_t index) {
    char label[32]{};
    std::snprintf(label, sizeof(label), "Cascade %u", index);
    if (!cubey::host::imgui_section(label, index == 0U)) {
        return;
    }

    const cubey::host::ScopedImGuiId section_id(label);
    ImGui::SliderFloat("Tile length", &cascade.tile_length, 8.0F, 256.0F, "%.0f m");
    ImGui::SliderFloat("Displacement scale", &cascade.displacement_scale, 0.0F, 2.0F, "%.2f");
    ImGui::SliderFloat("Normal scale", &cascade.normal_scale, 0.0F, 2.0F, "%.2f");
    ImGui::SliderFloat("Wind speed", &cascade.wind_speed, 0.1F, 32.0F, "%.1f m/s");
    ImGui::SliderFloat("Wind direction", &cascade.wind_direction_degrees, -180.0F, 180.0F,
                       "%.0f deg");
    ImGui::SliderFloat("Fetch", &cascade.fetch_length_km, 10.0F, 1200.0F, "%.0f km");
    ImGui::SliderFloat("Swell", &cascade.swell, 0.0F, 2.0F, "%.2f");
    ImGui::SliderFloat("Spread", &cascade.spread, 0.0F, 1.0F, "%.2f");
    ImGui::SliderFloat("Detail", &cascade.detail, 0.0F, 1.0F, "%.2f");
    ImGui::SliderFloat("Whitecap", &cascade.whitecap, 0.0F, 2.0F, "%.2f");
    ImGui::SliderFloat("Foam amount", &cascade.foam_amount, 0.0F, 10.0F, "%.2f");
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
    if (!cubey::host::begin_control_panel("Ocean", {.width = 390.0F})) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Paused", &ui.paused);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ui.reset_requested = true;
    }
    cubey::host::imgui_enum_combo("Debug view", ui.render_view, kOceanRenderViews,
                                  ocean_render_view_name);
    ImGui::Checkbox("Wire overlay", &ui.diagnostics.wire_overlay);
    ImGui::BeginDisabled(!ui.diagnostics.wire_overlay);
    ImGui::SliderFloat("Wire opacity", &ui.diagnostics.wire_opacity, 0.05F, 1.0F, "%.2f");
    ImGui::EndDisabled();

    if (cubey::host::imgui_section("Wave Core", true)) {
        const cubey::host::ScopedImGuiId section_id("Wave Core");
        draw_map_size_combo(ui.config);
        ImGui::SliderFloat("Depth", &ui.config.depth, 2.0F, 80.0F, "%.1f m");
        ImGui::TextUnformatted("GodotOceanWaves port");
    }

    if (cubey::host::imgui_section("Mesh", true)) {
        const cubey::host::ScopedImGuiId section_id("Mesh");
        int mesh_cells = static_cast<int>(ui.config.mesh_cells);
        if (ImGui::SliderInt("Base cells", &mesh_cells, static_cast<int>(kOceanMinMeshCells),
                             static_cast<int>(kOceanMaxMeshCells))) {
            ui.config.mesh_cells = static_cast<std::uint32_t>(
                std::clamp(mesh_cells, static_cast<int>(kOceanMinMeshCells),
                           static_cast<int>(kOceanMaxMeshCells)));
        }
        int mesh_lod_levels = static_cast<int>(ui.config.mesh_lod_levels);
        if (ImGui::SliderInt("LOD levels", &mesh_lod_levels,
                             static_cast<int>(kOceanMinMeshLodLevels),
                             static_cast<int>(kOceanMaxMeshLodLevels))) {
            ui.config.mesh_lod_levels = static_cast<std::uint32_t>(
                std::clamp(mesh_lod_levels, static_cast<int>(kOceanMinMeshLodLevels),
                           static_cast<int>(kOceanMaxMeshLodLevels)));
        }
        ImGui::SliderFloat("Extent", &ui.config.mesh_extent, 400.0F, 9000.0F, "%.0f m");
        ImGui::SliderFloat("Horizon fog", &ui.config.horizon_fog, 0.0F, 1.0F, "%.2f");
    }

    if (cubey::host::imgui_section("Shading", true)) {
        const cubey::host::ScopedImGuiId section_id("Shading");
        ImGui::SliderFloat("Roughness", &ui.config.roughness, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Normal strength", &ui.config.normal_strength, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Exposure", &ui.config.exposure, -4.0F, 4.0F, "%.2f");
        ImGui::ColorEdit3("Water", &ui.config.water_color_r);
        ImGui::ColorEdit3("Foam", &ui.config.foam_color_r);
    }

    if (cubey::host::imgui_section("Cascades", true)) {
        const cubey::host::ScopedImGuiId section_id("Cascades");
        for (std::uint32_t index = 0; index < kOceanCascadeCount; ++index) {
            draw_cascade_controls(ui.config.cascades[index], index);
        }
    }

    if (cubey::host::imgui_section("Diagnostics", true)) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        cubey::host::draw_frame_stats(ui.latest_frame_stats, ui.latest_fps, ui.latest_frame_ms);
        ImGui::Text("Mesh: %u LOD / %u patches / %u tris", ui.config.mesh_lod_levels,
                    ocean_mesh_patch_count(ui.config),
                    ocean_mesh_total_triangle_count(ui.config));
        ImGui::Text("Near cell: %.2f m", ocean_mesh_near_cell_size(ui.config));
        ImGui::Text("Vertices: %u", ocean_mesh_total_vertex_count(ui.config));
        draw_lod_diagnostics(ui.config);
    }

    ImGui::End();
}

} // namespace cubey::projects::ocean
