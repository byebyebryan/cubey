#include "procedural_terrain_ui.h"

#include <cubey/host/imgui_helpers.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace cubey::projects::procedural_terrain {
namespace {

constexpr std::array<std::uint32_t, 7> kGridPresets{129U, 193U, 257U, 385U, 513U, 1025U, 2049U};

void draw_grid_preset_combo(TerrainConfig& config) {
    char preview[32]{};
    if (config.grid_width == config.grid_height) {
        std::snprintf(preview, sizeof(preview), "%u", config.grid_width);
    } else {
        std::snprintf(preview, sizeof(preview), "%u x %u", config.grid_width, config.grid_height);
    }
    if (!ImGui::BeginCombo("Grid preset", preview)) {
        return;
    }
    for (const std::uint32_t preset : kGridPresets) {
        char label[16]{};
        std::snprintf(label, sizeof(label), "%u", preset);
        const bool selected = config.grid_width == preset && config.grid_height == preset;
        if (ImGui::Selectable(label, selected)) {
            const float extent_m =
                static_cast<float>(std::max(config.grid_width, config.grid_height) - 1U) *
                config.cell_size_m;
            config.grid_width = preset;
            config.grid_height = preset;
            config.cell_size_m = extent_m / static_cast<float>(preset - 1U);
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
    cubey::host::imgui_attach_help(
        "Square terrain-grid presets. Cell size is adjusted to preserve the current extent.");
}

void draw_seed_control(TerrainConfig& config) {
    int seed = config.seed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                   ? std::numeric_limits<int>::max()
                   : static_cast<int>(config.seed);
    if (cubey::host::imgui_input_int("Seed", &seed, 1, 1000, "Deterministic terrain seed.")) {
        config.seed = static_cast<std::uint64_t>(std::max(seed, 0));
    }
}

void draw_material_table(const TerrainDiagnostics& diagnostics) {
    if (!ImGui::BeginTable("terrain material coverage", 2,
                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("Material");
    ImGui::TableSetupColumn("Coverage");
    ImGui::TableHeadersRow();

    const auto row = [](const char* label, float coverage) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.1f%%", coverage * 100.0F);
    };
    row("Sand", diagnostics.sand_coverage);
    row("Rock", diagnostics.rock_coverage);
    row("Vegetation", diagnostics.vegetation_coverage);
    row("Sediment", diagnostics.sediment_coverage);
    ImGui::EndTable();
}

} // namespace

void draw_terrain_ui(TerrainUiContext ui) {
    if (!cubey::host::begin_control_panel("Terrain", {.width = 390.0F})) {
        ImGui::End();
        return;
    }

    if (cubey::host::imgui_button("Reset Camera",
                                  "Return the terrain camera to its default view.")) {
        ui.reset_camera_requested = true;
    }
    ImGui::SameLine();
    if (cubey::host::imgui_button("Defaults", "Reset editable terrain settings to defaults.")) {
        const TerrainDebugView debug_view = ui.active_config.debug_view;
        ui.edit_config = TerrainConfig{};
        ui.edit_config.debug_view = debug_view;
    }

    TerrainDebugView debug_view = ui.active_config.debug_view;
    if (cubey::host::imgui_enum_combo(
            "Debug view", debug_view, kTerrainDebugViews, terrain_debug_view_name,
            "Inspect terrain material, height, shoreline, and ocean-field outputs.")) {
        ui.active_config.debug_view = debug_view;
        ui.edit_config.debug_view = debug_view;
    }
    cubey::host::imgui_checkbox("Water surface", &ui.water_visible,
                                "Toggle the rendered water surface mesh.");

    if (const cubey::host::ScopedImGuiGroup group{
            "Terrain Shape",
            {.help = "Heightfield, shoreline, and material-field generation controls."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Terrain Shape");
        draw_grid_preset_combo(ui.edit_config);
        draw_seed_control(ui.edit_config);
        cubey::host::imgui_slider_float("Cell size", &ui.edit_config.cell_size_m, 0.5F, 8.0F,
                                        "%.2f m", "World-space size of one heightfield cell.");
        cubey::host::imgui_slider_float(
            "Sea level", &ui.edit_config.sea_level_m, -24.0F, 48.0F, "%.1f m",
            "Waterline elevation used for bathymetry and shoreline fields.");
        cubey::host::imgui_slider_float("Land extent", &ui.edit_config.land_extent, 0.35F, 0.90F,
                                        "%.2f",
                                        "Approximate normalized footprint of above-water land.");
        cubey::host::imgui_slider_float("Coast noise", &ui.edit_config.coast_noise_strength, 0.0F,
                                        0.50F, "%.2f", "Amount of coastline perturbation.");
        cubey::host::imgui_slider_float("Relief", &ui.edit_config.relief_scale, 0.20F, 2.0F, "%.2f",
                                        "Overall terrain elevation contrast.");
        cubey::host::imgui_slider_float("Ridges", &ui.edit_config.ridge_scale, 0.0F, 2.0F, "%.2f",
                                        "Ridged-noise contribution.");
        cubey::host::imgui_slider_float("Valleys", &ui.edit_config.valley_scale, 0.0F, 2.0F, "%.2f",
                                        "Valley/erosion contribution.");
    }

    const bool pending_rebuild = !terrain_rebuild_config_equal(ui.active_config, ui.edit_config);
    ImGui::BeginDisabled(!pending_rebuild);
    if (cubey::host::imgui_button("Revert Terrain",
                                  "Restore editable settings from the active terrain.")) {
        ui.discard_edits_requested = true;
    }
    ImGui::EndDisabled();
    if (pending_rebuild && !ui.discard_edits_requested && !ImGui::IsAnyItemActive()) {
        ui.rebuild_requested = true;
    }
    if (pending_rebuild) {
        ImGui::SameLine();
        ImGui::TextUnformatted("rebuilds when editing stops");
    }
    if (!ui.rebuild_error.empty()) {
        ImGui::TextWrapped("Rebuild failed: %s", ui.rebuild_error.c_str());
    }

    cubey::host::draw_performance_ui(ui.performance);

    if (const cubey::host::ScopedImGuiGroup group{
            "Diagnostics",
            {.default_open = false,
             .help = "Read-only terrain rebuild, LOD, and field statistics."}};
        group) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        ImGui::Text("Grid: %u x %u", ui.active_config.grid_width, ui.active_config.grid_height);
        ImGui::Text("Cell: %.1f m | Sea: %.1f m", ui.active_config.cell_size_m,
                    ui.active_config.sea_level_m);
        ImGui::Text("Height: %.1f .. %.1f m", ui.diagnostics.min_height_m,
                    ui.diagnostics.max_height_m);
        ImGui::Text("Water depth max: %.1f m", ui.diagnostics.max_water_depth_m);
        ImGui::Text("Shore SDF max abs: %.1f m", ui.diagnostics.max_abs_shore_sdf_m);
        ImGui::Text("Land/water: %.1f%% / %.1f%%",
                    static_cast<float>(ui.diagnostics.land_samples) * 100.0F /
                        static_cast<float>(std::max<std::size_t>(ui.diagnostics.sample_count, 1U)),
                    static_cast<float>(ui.diagnostics.water_samples) * 100.0F /
                        static_cast<float>(std::max<std::size_t>(ui.diagnostics.sample_count, 1U)));
        ImGui::Text("Shoreline samples: %zu", ui.diagnostics.shoreline_samples);
        ImGui::Text("Slope avg/max: %.3f / %.3f", ui.diagnostics.average_slope,
                    ui.diagnostics.max_slope);
        ImGui::Text("Ridge/valley coverage: %.3f / %.3f", ui.diagnostics.ridge_coverage,
                    ui.diagnostics.valley_coverage);
        ImGui::Text("Macro/base/detail max abs: %.1f / %.1f / %.1f m",
                    ui.diagnostics.max_abs_macro_height_m, ui.diagnostics.max_abs_base_noise_m,
                    ui.diagnostics.max_abs_detail_noise_m);
        ImGui::Text("Feature/relax max abs: %.1f / %.2f m", ui.diagnostics.max_abs_feature_height_m,
                    ui.diagnostics.max_abs_relax_delta_m);
        ImGui::Text("Flow/stream max: %.1f / %.3f", ui.diagnostics.max_flow_accumulation,
                    ui.diagnostics.max_stream_power);
        ImGui::Text("Full terrain: %u verts / %u tris", ui.diagnostics.terrain_vertices,
                    ui.diagnostics.terrain_triangles);
        ImGui::Text("Final land: %u verts / %u tris", ui.diagnostics.final_land_vertices,
                    ui.diagnostics.final_land_triangles);
        ImGui::Text("Water mesh: %u verts / %u tris", ui.diagnostics.water_vertices,
                    ui.diagnostics.water_triangles);
        ImGui::Text("LOD plan: %u levels / %u patches", ui.diagnostics.clipmap_lod_levels,
                    ui.diagnostics.clipmap_patch_count);
        ImGui::Text("LOD extent/cell: %.1f m / %.2f m", ui.diagnostics.clipmap_outer_half_extent_m,
                    ui.diagnostics.clipmap_near_cell_size_m);
        ImGui::Text("Rebuilds: %llu | Last: %.2f ms",
                    static_cast<unsigned long long>(ui.diagnostics.rebuild_count),
                    ui.diagnostics.last_rebuild_ms);
        draw_material_table(ui.diagnostics);
    }

    ImGui::End();
}

} // namespace cubey::projects::procedural_terrain
