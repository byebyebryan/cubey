#include "water_3d_config.h"

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
        namespace water = cubey::projects::fluid::water_3d;
        water::Water3DConfig config;
        constexpr std::size_t kExpectedCellCount = std::size_t{64} * 64U * 64U;
        constexpr std::size_t kExpectedUFaceCount = std::size_t{65} * 64U * 64U;
        constexpr std::size_t kExpectedVFaceCount = std::size_t{64} * 65U * 64U;
        constexpr std::size_t kExpectedWFaceCount = std::size_t{64} * 64U * 65U;
        constexpr std::size_t kExpectedActiveParticleCount =
            std::size_t{32} * 44U * 32U * 4U;
        constexpr std::size_t kExpectedParticleCapacity =
            std::size_t{48} * 48U * 48U * 4U;

        require(config.grid_width == 64 && config.grid_height == 64 && config.grid_depth == 64,
                "water 3D should default to a 64^3 grid");
        require(config.pressure_iterations == 32,
                "water 3D should default to a bounded pressure solve");
        require(config.particles_per_cell == 4, "water 3D should seed four particles per cell");
        require(config.max_particles_per_cell == 32,
                "water 3D bins should reserve fixed overflow slots");
        require(config.active_particle_count == kExpectedActiveParticleCount,
                "water 3D active particles should come from the default fill volume");
        require(config.particle_capacity == kExpectedParticleCapacity,
                "water 3D capacity should cover the maximum editable fill volume");
        require(config.transfer_mode == water::Water3DTransferMode::Apic,
                "water 3D should default to APIC transfer");
        require(config.initial_fill_width == 0.50F && config.initial_fill_height == 0.70F &&
                    config.initial_fill_depth == 0.50F,
                "water 3D should default to the planned centered dam fill");
        require(sizeof(water::Water3DSimulationUniforms) ==
                    sizeof(float) * water::kWater3DSimulationUniformFloatCount,
                "water 3D simulation uniforms should match the shader contract");
        require(sizeof(water::Water3DDispatchPushConstants) ==
                    sizeof(float) * water::kWater3DSimulationPushConstantFloatCount,
                "water 3D dispatch constants should match the shader contract");

        require(water::cell_count(config) == kExpectedCellCount,
                "water 3D cell count should multiply dimensions");
        require(water::u_face_count(config) == kExpectedUFaceCount,
                "water 3D U faces should include one extra x face");
        require(water::v_face_count(config) == kExpectedVFaceCount,
                "water 3D V faces should include one extra y face");
        require(water::w_face_count(config) == kExpectedWFaceCount,
                "water 3D W faces should include one extra z face");
        require(water::particle_buffer_byte_size(config) ==
                    sizeof(float) * kExpectedParticleCapacity * 4U,
                "water 3D particle buffers should store vec4 capacity");
        require(water::particle_affine_buffer_byte_size(config) ==
                    sizeof(float) * kExpectedParticleCapacity * 12U,
                "water 3D APIC affine buffers should store three vec4 rows per particle");
        require(water::particle_bin_index_count(config) == kExpectedCellCount * 32U,
                "water 3D particle bins should allocate fixed cell slots");

        cubey::RunConfig run_config;
        run_config.grid_width = 32;
        run_config.grid_height = 48;
        run_config.grid_depth = 40;
        const water::Water3DConfig overridden = water::water_3d_config_from_run_config(run_config);
        require(overridden.grid_width == 32 && overridden.grid_height == 48 &&
                    overridden.grid_depth == 40,
                "water 3D should accept CLI grid dimensions");
        require(overridden.active_particle_count == water::active_particle_count_for_fill(overridden),
                "water 3D run-config construction should refresh active particle counts");
        require(overridden.particle_capacity == water::particle_capacity_for_config(overridden),
                "water 3D run-config construction should refresh particle capacity");

        bool rejected = false;
        try {
            water::Water3DConfig invalid;
            invalid.grid_width = 8;
            static_cast<void>(water::cell_count(invalid));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "water 3D should reject degenerate grid dimensions");

        const std::filesystem::path shader_dir =
            std::filesystem::path(CUBEY_WATER_3D_SOURCE_DIR) / "shaders";
        const std::string contract = read_text_file(shader_dir / "water_3d_contract.glsl");
        const std::string reset_shader = read_text_file(shader_dir / "water_3d_reset.comp");
        const std::string p2g_shader =
            read_text_file(shader_dir / "water_3d_particle_to_grid.comp");
        const std::string extrapolate_shader =
            read_text_file(shader_dir / "water_3d_extrapolate_velocity.comp");
        const std::string g2p_shader =
            read_text_file(shader_dir / "water_3d_grid_to_particle.comp");
        const std::string render_shader = read_text_file(shader_dir / "water_3d_render.frag");

        require_contains(contract, "WATER3D_BINDING_W_FIELD",
                         "water 3D contract should expose the W face field");
        require_contains(contract, "WATER3D_BINDING_U_SCRATCH",
                         "water 3D contract should expose velocity extrapolation scratch fields");
        require_contains(contract, "WATER3D_BINDING_W_WEIGHT_SCRATCH",
                         "water 3D contract should expose extrapolation validity scratch fields");
        require_contains(reset_shader, "particle_affine.values[id * 3u + 2u]",
                         "water 3D reset should clear all APIC affine rows");
        require_contains(p2g_shader, "gather_face_velocity",
                         "water 3D particle-to-grid should gather face velocities");
        require_contains(p2g_shader, "velocity += unpack_affine(particle_id) * delta",
                         "water 3D particle-to-grid should apply APIC local velocity");
        require_contains(extrapolate_shader, "read_scratch()",
                         "water 3D velocity extrapolation should ping-pong scratch buffers");
        require_contains(extrapolate_shader, "source_u_valid(uint(neighbor.x)",
                         "water 3D velocity extrapolation should average valid neighbor faces");
        require_contains(extrapolate_shader, "write_w(index, 0.0, 0.0, 0.0)",
                         "water 3D velocity extrapolation should keep blocked faces invalid");
        require_contains(g2p_shader, "flip_velocity",
                         "water 3D grid-to-particle should keep the PIC/FLIP path");
        require_contains(g2p_shader, "store_affine",
                         "water 3D grid-to-particle should write APIC affine state");
        require_contains(g2p_shader, "confidence_blend",
                         "water 3D grid-to-particle should use extrapolated velocity confidence");
        require_contains(g2p_shader, "fallback_velocity",
                         "water 3D grid-to-particle should preserve gravity when confidence is low");
        require_contains(render_shader, "frag_particle",
                         "water 3D renderer should support particle splats");
        require_contains(render_shader, "debug_view == 4u",
                         "water 3D renderer should expose the solid debug view");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "water_3d_config_tests: %s\n", error.what());
        return 1;
    }
}
