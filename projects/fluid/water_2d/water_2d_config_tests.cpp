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
        constexpr std::size_t kExpectedCellCount = std::size_t{256} * std::size_t{144};
        constexpr std::size_t kExpectedUFaceCount = std::size_t{257} * std::size_t{144};
        constexpr std::size_t kExpectedVFaceCount = std::size_t{256} * std::size_t{145};
        constexpr std::size_t kExpectedActiveParticleCount =
            std::size_t{128} * std::size_t{100} * 4U;
        constexpr std::size_t kExpectedParticleCapacity = std::size_t{235} * std::size_t{132} * 4U;
        constexpr std::size_t kExpectedBinIndexCount = kExpectedCellCount * 16U;

        require(config.grid_width == 256, "water grid should default to 256 columns");
        require(config.grid_height == 144, "water grid should default to 144 rows");
        require(config.pressure_iterations == 256,
                "water pressure solve should default to a stronger Jacobi pass count");
        require(config.particles_per_cell == 4, "water should seed four particles per cell");
        require(config.max_particles_per_cell == 16,
                "water particle bins should reserve a bounded overflow margin");
        require(config.active_particle_count == kExpectedActiveParticleCount,
                "water active particle count should come from the default fill area");
        require(config.particle_capacity == kExpectedParticleCapacity,
                "water particle capacity should cover the maximum editable fill area");
        require(config.flip_ratio == 0.78F, "water should default to a stable PIC/FLIP blend");
        require(config.gravity < 0.0F, "water gravity should pull downward by default");
        require(config.initial_fill_height == 0.70F,
                "water fill height should default to a readable dam-break slab");
        require(config.initial_fill_width == 0.50F,
                "water fill width should default to a readable dam-break slab");

        require(cubey::projects::fluid::water_2d::cell_count(config) == kExpectedCellCount,
                "water cell count should multiply dimensions");
        require(cubey::projects::fluid::water_2d::u_face_count(config) == kExpectedUFaceCount,
                "water U faces should include one more vertical face column");
        require(cubey::projects::fluid::water_2d::v_face_count(config) == kExpectedVFaceCount,
                "water V faces should include one more horizontal face row");
        require(cubey::projects::fluid::water_2d::particle_bin_index_count(config) ==
                    kExpectedBinIndexCount,
                "water particle bins should allocate fixed cell slots");
        require(cubey::projects::fluid::water_2d::scalar_field_byte_size(config) ==
                    sizeof(float) * kExpectedCellCount,
                "water scalar byte size should cover one float per cell");
        require(cubey::projects::fluid::water_2d::cell_uint_field_byte_size(config) ==
                    sizeof(std::uint32_t) * kExpectedCellCount,
                "water uint byte size should cover one uint per cell");
        require(cubey::projects::fluid::water_2d::u_face_byte_size(config) ==
                    sizeof(float) * kExpectedUFaceCount,
                "water U-face byte size should cover one float per U face");
        require(cubey::projects::fluid::water_2d::v_face_byte_size(config) ==
                    sizeof(float) * kExpectedVFaceCount,
                "water V-face byte size should cover one float per V face");
        require(cubey::projects::fluid::water_2d::particle_buffer_byte_size(config) ==
                    sizeof(float) * kExpectedParticleCapacity * 4U,
                "water particle byte size should cover vec4 particle capacity");

        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Surface,
                "empty debug view should map to surface");
        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("particles") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Particles,
                "debug view parser should accept particles");
        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("cells") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Cells,
                "debug view parser should accept cells");
        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("solid") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Solid,
                "debug view parser should accept solid");
        require(cubey::projects::fluid::water_2d::next_debug_view(
                    cubey::projects::fluid::water_2d::Water2DDebugView::Solid) ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Surface,
                "debug view should cycle back to surface");

        bool threw_for_phi = false;
        try {
            static_cast<void>(
                cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("phi"));
        } catch (const std::runtime_error&) {
            threw_for_phi = true;
        }
        require(threw_for_phi, "water config should reject the removed level-set phi view");

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
        require(default_from_run_config.active_particle_count == config.active_particle_count,
                "default run config should preserve water active particle count");
        require(default_from_run_config.particle_capacity == config.particle_capacity,
                "default run config should preserve water particle capacity");

        cubey::RunConfig run_config;
        run_config.grid_width = 320;
        run_config.grid_height = 180;
        const cubey::projects::fluid::water_2d::Water2DConfig configured =
            cubey::projects::fluid::water_2d::water_2d_config_from_run_config(run_config);
        require(configured.grid_width == 320, "water config should honor run config grid width");
        require(configured.grid_height == 180, "water config should honor run config grid height");
        require(configured.active_particle_count == (160U * 126U * 4U),
                "water config should size active particles from configured grid dimensions");
        require(configured.particle_capacity == (294U * 165U * 4U),
                "water config should size particle capacity from configured grid dimensions");

        cubey::projects::fluid::water_2d::Water2DConfig edited_fill = config;
        edited_fill.initial_fill_width = 0.25F;
        edited_fill.initial_fill_height = 0.25F;
        cubey::projects::fluid::water_2d::refresh_particle_counts(edited_fill);
        require(edited_fill.active_particle_count == (64U * 36U * 4U),
                "water runtime fill edits should update the active particle count");
        require(edited_fill.particle_capacity == config.particle_capacity,
                "water runtime fill edits should retain the allocated particle capacity");
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
        const std::string build_bins_shader =
            read_text_file(source_root / "shaders/water_2d_build_bins.comp");
        const std::string p2g_shader =
            read_text_file(source_root / "shaders/water_2d_particle_to_grid.comp");
        const std::string pressure_shader =
            read_text_file(source_root / "shaders/water_2d_pressure.comp");
        const std::string projection_shader =
            read_text_file(source_root / "shaders/water_2d_projection.comp");
        const std::string g2p_shader =
            read_text_file(source_root / "shaders/water_2d_grid_to_particle.comp");
        const std::string advect_shader =
            read_text_file(source_root / "shaders/water_2d_advect_particles.comp");
        const std::string render_shader =
            read_text_file(source_root / "shaders/water_2d_render.frag");
        require_contains(reset_shader, "ParticlePositions",
                         "water reset shader should initialize particle positions");
        require_contains(reset_shader, "water_2d_contract.glsl",
                         "water reset shader should use the shared water shader contract");
        require_contains(reset_shader, "release_edge",
                         "water reset shader should seed a readable dam-break release");
        require_contains(build_bins_shader, "WATER2D_BINDING_CELL_COUNTS",
                         "water bin build should use shared descriptor binding names");
        require_contains(build_bins_shader, "atomicAdd",
                         "water bin build should atomically count particles per cell");
        require_contains(build_bins_shader, "max_particles_per_cell",
                         "water bin build should clamp fixed-capacity cell slots");
        require_contains(p2g_shader, "gather_face_velocity",
                         "water particle-to-grid shader should gather face velocities");
        require_contains(p2g_shader, "u_previous.values",
                         "water particle-to-grid shader should preserve pre-solve face velocity");
        require_contains(pressure_shader, "cell_counts.values[index] > 0u",
                         "water pressure shader should solve only occupied liquid cells");
        require_contains(projection_shader, "read_pressure",
                         "water projection shader should read the selected pressure buffer");
        require_contains(g2p_shader, "flip_velocity",
                         "water grid-to-particle shader should support FLIP velocity updates");
        require_contains(g2p_shader, "params.particle_options.w",
                         "water grid-to-particle shader should use the configured PIC/FLIP blend");
        require_contains(advect_shader, "collide_obstacle",
                         "water particle advection should collide against the optional obstacle");
        require_contains(render_shader, "particle_density",
                         "water render shader should draw from particle bins");
        require_contains(render_shader, "vec2 uv = vec2(screen_uv.x, 1.0 - screen_uv.y)",
                         "water render shader should flip screen Y to solver Y");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "water_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
