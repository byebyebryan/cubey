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

constexpr std::array<std::uint32_t, 5> kGridPresets{129U, 193U, 257U, 385U, 513U};

void draw_grid_preset_combo(TerrainConfig& config) {
    char preview[32]{};
    if (config.grid_width == config.grid_height) {
        std::snprintf(preview, sizeof(preview), "%u", config.grid_width);
    } else {
        std::snprintf(preview, sizeof(preview), "%u x %u", config.grid_width,
                      config.grid_height);
    }
    if (!ImGui::BeginCombo("Grid preset", preview)) {
        return;
    }
    for (const std::uint32_t preset : kGridPresets) {
        char label[16]{};
        std::snprintf(label, sizeof(label), "%u", preset);
        const bool selected = config.grid_width == preset && config.grid_height == preset;
        if (ImGui::Selectable(label, selected)) {
            config.grid_width = preset;
            config.grid_height = preset;
        }
        if (selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
}

void draw_seed_control(TerrainConfig& config) {
    int seed = config.seed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                   ? std::numeric_limits<int>::max()
                   : static_cast<int>(config.seed);
    if (ImGui::InputInt("Seed", &seed, 1, 1000)) {
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

    if (ImGui::Button("Reset Camera")) {
        ui.reset_camera_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Defaults")) {
        const TerrainDebugView debug_view = ui.active_config.debug_view;
        ui.edit_config = TerrainConfig{};
        ui.edit_config.debug_view = debug_view;
    }

    TerrainDebugView debug_view = ui.active_config.debug_view;
    if (cubey::host::imgui_enum_combo("Debug view", debug_view, kTerrainDebugViews,
                                      terrain_debug_view_name)) {
        ui.active_config.debug_view = debug_view;
        ui.edit_config.debug_view = debug_view;
    }
    ImGui::Checkbox("Water surface", &ui.water_visible);

    if (cubey::host::imgui_section("Terrain Shape", true)) {
        const cubey::host::ScopedImGuiId section_id("Terrain Shape");
        draw_grid_preset_combo(ui.edit_config);
        draw_seed_control(ui.edit_config);
        ImGui::SliderFloat("Cell size", &ui.edit_config.cell_size_m, 2.0F, 8.0F, "%.1f m");
        ImGui::SliderFloat("Sea level", &ui.edit_config.sea_level_m, -24.0F, 48.0F, "%.1f m");
        ImGui::SliderFloat("Land extent", &ui.edit_config.land_extent, 0.35F, 0.90F, "%.2f");
        ImGui::SliderFloat("Coast noise", &ui.edit_config.coast_noise_strength, 0.0F, 0.50F,
                           "%.2f");
        ImGui::SliderFloat("Relief", &ui.edit_config.relief_scale, 0.20F, 2.0F, "%.2f");
        ImGui::SliderFloat("Ridges", &ui.edit_config.ridge_scale, 0.0F, 2.0F, "%.2f");
    }

    const bool pending_rebuild =
        !terrain_rebuild_config_equal(ui.active_config, ui.edit_config);
    ImGui::BeginDisabled(!pending_rebuild);
    if (ImGui::Button("Apply Terrain")) {
        ui.rebuild_requested = true;
    }
    ImGui::EndDisabled();
    if (pending_rebuild) {
        ImGui::SameLine();
        ImGui::TextUnformatted("pending rebuild");
    }

    if (cubey::host::imgui_section("Diagnostics", true)) {
        const cubey::host::ScopedImGuiId section_id("Diagnostics");
        cubey::host::draw_frame_stats(ui.latest_frame_stats, ui.latest_fps, ui.latest_frame_ms);
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
        ImGui::Text("Terrain mesh: %u verts / %u tris", ui.diagnostics.terrain_vertices,
                    ui.diagnostics.terrain_triangles);
        ImGui::Text("Water mesh: %u verts / %u tris", ui.diagnostics.water_vertices,
                    ui.diagnostics.water_triangles);
        ImGui::Text("Rebuilds: %llu | Last: %.2f ms",
                    static_cast<unsigned long long>(ui.diagnostics.rebuild_count),
                    ui.diagnostics.last_rebuild_ms);
        draw_material_table(ui.diagnostics);
    }

    ImGui::End();
}

} // namespace cubey::projects::procedural_terrain
