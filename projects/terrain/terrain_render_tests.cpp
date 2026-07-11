#include "terrain_clipmap.h"
#include "terrain_config.h"

#include <cubey/core/run_config.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_near(float actual, float expected, float tolerance, std::string_view message) {
    require(std::abs(actual - expected) <= tolerance, message);
}

void test_runtime_config_defaults_to_the_v1_scene() {
    const cubey::RunConfig run_config{};
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.source.preset == cubey::projects::terrain::TerrainPreset::Mountain,
            "terrain runtime should default to mountain preset");
    require(config.source.weathering == cubey::projects::terrain::TerrainWeatheringMode::Local,
            "terrain runtime should default to local weathering");
    require(config.camera == cubey::projects::terrain::TerrainCameraPreset::Oblique,
            "terrain runtime should default to oblique camera");
    require(config.debug_view == cubey::projects::terrain::TerrainDebugView::Surface,
            "terrain runtime should default to surface view");
    require_near(config.near_cell_size_m, 2.0F, 0.0F,
                 "terrain runtime should default to two-meter near cells");
    require(config.lod_levels == 8U && config.cells_per_axis == 128U,
            "terrain runtime should use the v1 clipmap dimensions");
}

void test_clipmap_has_expected_extent_and_transition_data() {
    const cubey::projects::terrain::TerrainRuntimeConfig config{};
    const auto clipmap = cubey::projects::terrain::terrain_clipmap_config(config);
    require_near(cubey::render::clipmap_grid_2d_near_cell_size(clipmap), 2.0F, 0.0001F,
                 "terrain clipmap should preserve the configured near cell size");
    require_near(clipmap.outer_half_extent, 16'384.0F, 0.01F,
                 "terrain clipmap should cover the v1 outer radius");

    const auto mesh = cubey::projects::terrain::make_terrain_clipmap_mesh(config);
    require(mesh.diagnostics.lod_levels == 8U && mesh.diagnostics.patch_count == 29U,
            "terrain clipmap should contain one center and seven four-piece rings");
    require(mesh.diagnostics.total_triangles ==
                cubey::projects::terrain::terrain_clipmap_triangle_count(mesh),
            "terrain clipmap diagnostics should match mesh triangles");
    require(!mesh.vertices.empty() && mesh.vertices.front().color[2] == 0.0F,
            "terrain clipmap should draw the finest level first");

    bool has_transition = false;
    bool has_fully_morphed_guard = false;
    bool has_child_coverage = false;
    bool has_skirts = false;
    for (const auto& vertex : mesh.vertices) {
        require(vertex.color[0] >= 2.0F && vertex.color[1] >= 0.0F && vertex.color[1] <= 1.0F &&
                    vertex.color[2] >= 0.0F && vertex.color[2] <= 1.0F,
                "terrain clipmap vertex metadata should stay in range");
        has_transition = has_transition || vertex.color[1] > 0.0F;
        has_fully_morphed_guard = has_fully_morphed_guard || vertex.color[1] == 1.0F;
        require(vertex.normal[1] >= 0.0F, "terrain skirt depth should not be negative");
        require_near(vertex.normal[2], 2.0F, 0.0F,
                     "terrain levels should share the finest snapped origin");
        has_skirts = has_skirts || vertex.normal[1] > 0.0F;
        if (vertex.color[2] == 0.0F) {
            require_near(vertex.normal[0], 0.0F, 0.0F,
                         "terrain finest level should not mask child coverage");
        } else {
            require_near(vertex.normal[0], vertex.color[0] * 32.0F, 0.001F,
                         "terrain coarse levels should publish their child half extent");
            has_child_coverage = true;
        }
    }
    require(has_transition, "terrain clipmap should publish transition morph weights");
    require(has_fully_morphed_guard,
            "terrain clipmap should fully morph its ownership guard cells");
    require(has_child_coverage, "terrain clipmap should publish finer-level coverage metadata");
    require(has_skirts, "terrain clipmap should add boundary skirts below transitioning levels");
}

} // namespace

int main() {
    try {
        test_runtime_config_defaults_to_the_v1_scene();
        test_clipmap_has_expected_extent_and_transition_data();
        std::cout << "terrain_render_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_render_tests: " << error.what() << '\n';
        return 1;
    }
}
