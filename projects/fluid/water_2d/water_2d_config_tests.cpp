#include "water_2d_config.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open " + path.string());
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

} // namespace

int main() {
    try {
        const cubey::projects::fluid::water_2d::Water2DConfig config;
        constexpr std::size_t kExpectedCellCount = std::size_t{512} * std::size_t{288};
        constexpr std::size_t kExpectedUFaceCount = std::size_t{513} * std::size_t{288};
        constexpr std::size_t kExpectedVFaceCount = std::size_t{512} * std::size_t{289};

        require(config.grid_width == 512, "water grid should default to 512 columns");
        require(config.grid_height == 288, "water grid should default to 288 rows");
        require(config.pressure_iterations == 32,
                "water pressure solve should default to 32 Jacobi iterations");
        require(config.reinitialization_iterations == 4,
                "water level set cleanup should default to four iterations");
        require(config.gravity < 0.0F, "water gravity should pull downward by default");
        require(config.initial_fill_height > 0.0F && config.initial_fill_height < 1.0F,
                "water fill height should be normalized");
        require(config.initial_fill_width > 0.0F && config.initial_fill_width < 1.0F,
                "water fill width should be normalized");

        require(cubey::projects::fluid::water_2d::cell_count(config) == kExpectedCellCount,
                "water cell count should multiply dimensions");
        require(cubey::projects::fluid::water_2d::u_face_count(config) == kExpectedUFaceCount,
                "water U faces should include one more vertical face column");
        require(cubey::projects::fluid::water_2d::v_face_count(config) == kExpectedVFaceCount,
                "water V faces should include one more horizontal face row");
        require(cubey::projects::fluid::water_2d::scalar_field_byte_size(config) ==
                    sizeof(float) * kExpectedCellCount,
                "water scalar byte size should cover one float per cell");
        require(cubey::projects::fluid::water_2d::u_face_byte_size(config) ==
                    sizeof(float) * kExpectedUFaceCount,
                "water U-face byte size should cover one float per U face");
        require(cubey::projects::fluid::water_2d::v_face_byte_size(config) ==
                    sizeof(float) * kExpectedVFaceCount,
                "water V-face byte size should cover one float per V face");

        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Surface,
                "empty debug view should map to surface");
        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("phi") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Phi,
                "debug view parser should accept phi");
        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("solid") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Solid,
                "debug view parser should accept solid");
        require(cubey::projects::fluid::water_2d::next_debug_view(
                    cubey::projects::fluid::water_2d::Water2DDebugView::Solid) ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Surface,
                "debug view should cycle back to surface");

        bool threw_for_debug_view = false;
        try {
            static_cast<void>(
                cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("density"));
        } catch (const std::runtime_error&) {
            threw_for_debug_view = true;
        }
        require(threw_for_debug_view, "water config should reject unknown debug views");

        const cubey::RunConfig default_run_config;
        const cubey::projects::fluid::water_2d::Water2DConfig default_from_run_config =
            cubey::projects::fluid::water_2d::water_2d_config_from_run_config(default_run_config);
        require(default_from_run_config.grid_width == config.grid_width,
                "default run config should preserve water grid width");
        require(default_from_run_config.grid_height == config.grid_height,
                "default run config should preserve water grid height");

        cubey::RunConfig run_config;
        run_config.grid_width = 320;
        run_config.grid_height = 180;
        const cubey::projects::fluid::water_2d::Water2DConfig configured =
            cubey::projects::fluid::water_2d::water_2d_config_from_run_config(run_config);
        require(configured.grid_width == 320, "water config should honor run config grid width");
        require(configured.grid_height == 180, "water config should honor run config grid height");
        require(cubey::projects::fluid::water_2d::water_2d_headless_frame_count(run_config) == 120,
                "water headless frame count should default to 120");
        run_config.frames = 8;
        require(cubey::projects::fluid::water_2d::water_2d_headless_frame_count(run_config) == 8,
                "water headless frame count should honor run config frames");

        const cubey::FrameTiming timing =
            cubey::projects::fluid::water_2d::fixed_water_2d_headless_timing(config, 5);
        require(timing.frame_index == 5, "water fixed timing should carry frame index");
        require(timing.delta_seconds == config.fixed_delta_seconds,
                "water fixed timing should use configured timestep");

        const std::filesystem::path source_root{CUBEY_WATER_2D_SOURCE_DIR};
        const std::string reset_shader =
            read_text_file(source_root / "shaders/water_2d_reset.comp");
        const std::string pressure_shader =
            read_text_file(source_root / "shaders/water_2d_pressure.comp");
        const std::string projection_shader =
            read_text_file(source_root / "shaders/water_2d_projection.comp");
        require_contains(reset_shader, "layout(local_size_x = 8, local_size_y = 8)",
                         "water reset shader should match the C++ group size");
        require_contains(pressure_shader, "phi_a.values[index] >= 0.0",
                         "water pressure shader should treat non-liquid cells as air");
        require_contains(projection_shader, "u_a.values[index]",
                         "water projection shader should write projected U-face velocity");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "water_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
