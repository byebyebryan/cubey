#include "ocean_config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void require_contains(const std::string& text, const std::string& needle, const char* message) {
    require(text.find(needle) != std::string::npos, message);
}

} // namespace

int main() {
    try {
        namespace ocean = cubey::projects::ocean;

        const ocean::OceanConfig defaults{};
        require(defaults.mesh_cells >= ocean::kOceanMinMeshCells &&
                    defaults.mesh_cells <= ocean::kOceanMaxMeshCells,
                "default mesh resolution should be in supported range");
        require(ocean::ocean_mesh_vertex_count(defaults) ==
                    defaults.mesh_cells * defaults.mesh_cells * 6U,
                "ocean vertex count should match generated grid triangles");
        require(defaults.mesh_extent > 1000.0F,
                "default ocean mesh should target horizon-scale rendering");
        require(defaults.disturbance_radius > 0.0F && defaults.disturbance_strength == 0.0F,
                "default ocean config should expose interaction hooks without radial rings");

        require(ocean::ocean_render_view_from_name("") == ocean::OceanRenderView::Final,
                "empty debug view should use final ocean rendering");
        require(ocean::ocean_render_view_from_name("normal") == ocean::OceanRenderView::Normal,
                "normal debug view should parse");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Depth) ==
                    ocean::OceanRenderView::Final,
                "ocean debug view cycle should wrap");

        bool rejected = false;
        try {
            static_cast<void>(ocean::ocean_render_view_from_name("velocity"));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "unknown ocean debug view should be rejected");

        cubey::RunConfig run_config;
        run_config.debug_view = "foam";
        run_config.pbr.exposure = 0.75F;
        const ocean::OceanConfig from_run_config = ocean::ocean_config_from_run_config(run_config);
        require(from_run_config.render_view == ocean::OceanRenderView::Foam,
                "run config should initialize ocean debug view");
        require(from_run_config.exposure == 0.75F, "run config should initialize ocean exposure");

        ocean::OceanConfig invalid_low = defaults;
        invalid_low.mesh_cells = ocean::kOceanMinMeshCells - 1U;
        rejected = false;
        try {
            static_cast<void>(ocean::ocean_mesh_vertex_count(invalid_low));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean mesh sizing should reject unsupported low resolution");

        const std::filesystem::path source_root(CUBEY_OCEAN_SOURCE_DIR);
        const std::string vertex_shader = read_text_file(source_root / "shaders/ocean.vert");
        const std::string fragment_shader = read_text_file(source_root / "shaders/ocean.frag");
        require_contains(vertex_shader, "pow(abs(signed_uv), vec2(1.72))",
                         "ocean vertex shader should use a camera-relative projected grid");
        require_contains(vertex_shader, "short_wave_lod",
                         "ocean vertex shader should attenuate high-frequency waves by distance");
        require_contains(fragment_shader, "cubey_pbr_apply_display_transform",
                         "ocean fragment shader should use the shared display transform");
        require_contains(fragment_shader, "OCEAN_VIEW_REFLECTION",
                         "ocean fragment shader should expose reflection debug view");

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ocean_config_tests: " << error.what() << '\n';
        return 1;
    }
}
