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
        constexpr std::size_t kExpectedInitialParticleCapacity =
            std::size_t{235} * std::size_t{132} * 4U;
        constexpr std::size_t kExpectedHoseParticleCapacity = 262144U;
        constexpr std::size_t kExpectedParticleCapacity =
            kExpectedInitialParticleCapacity + kExpectedHoseParticleCapacity;
        constexpr std::size_t kExpectedBinIndexCount = kExpectedCellCount * 64U;

        require(config.grid_width == 256, "water grid should default to 256 columns");
        require(config.grid_height == 144, "water grid should default to 144 rows");
        require(cubey::projects::fluid::water_2d::kWater2DWallCells == 2,
                "water should use a two-cell solid border");
        require(cubey::projects::fluid::water_2d::kWater2DMinimumGridWidth == 16,
                "water should reject degenerate grid widths");
        require(cubey::projects::fluid::water_2d::kWater2DMinimumGridHeight == 16,
                "water should reject degenerate grid heights");
        require(cubey::projects::fluid::water_2d::kWater2DMaxExactShaderInteger == (1U << 24U),
                "water float-backed shader counts should stay in the exact integer range");
        require(config.pressure_iterations == 256,
                "water pressure solve should default to a stronger Jacobi pass count");
        require(config.particles_per_cell == 4, "water should seed four particles per cell");
        require(config.max_particles_per_cell == 64,
                "water particle bins should reserve a bounded overflow margin");
        require(config.active_particle_count == kExpectedActiveParticleCount,
                "water active particle count should come from the default fill area");
        require(config.initial_particle_capacity == kExpectedInitialParticleCapacity,
                "water initial particle capacity should cover the maximum editable fill area");
        require(config.hose.particle_capacity == kExpectedHoseParticleCapacity,
                "water hose pool should reserve a bounded inactive particle range");
        require(config.particle_capacity == kExpectedParticleCapacity,
                "water particle capacity should include initial fill and hose-pool ranges");
        require(config.substeps == 1, "water should default to one simulation substep");
        require(config.scenario == cubey::projects::fluid::water_2d::Water2DScenario::DamBreak,
                "water should default to the dam-break scenario");
        require(config.transfer_mode == cubey::projects::fluid::water_2d::Water2DTransferMode::Apic,
                "water should default to APIC transfer");
        require(config.flip_ratio == 0.78F, "water should default to a stable PIC/FLIP blend");
        require(config.gravity < 0.0F, "water gravity should pull downward by default");
        require(config.initial_fill_height == 0.70F,
                "water fill height should default to a readable dam-break slab");
        require(config.initial_fill_width == 0.50F,
                "water fill width should default to a readable dam-break slab");
        require(config.velocity_limit == 3.0F, "water should default to a bounded velocity limit");
        require(config.particle_damping == 0.999F,
                "water should default to light particle damping");
        require(config.particle_separation_radius == 0.58F,
                "water should default to a particle separation radius");
        require(config.particle_separation_strength == 0.32F,
                "water should default to particle separation");
        require(config.particle_volume_strength == 18.0F,
                "water should default to particle volume expansion");
        require(config.boundary_restitution == 0.18F,
                "water should default to a soft boundary bounce");
        require(config.obstacle_friction == 0.86F,
                "water should default to obstacle collision friction");
        require(config.surface_threshold == 0.82F,
                "water should default to the current surface threshold");
        require(config.edge_strength == 0.52F, "water should default to readable surface edges");
        require(config.surface_splat_radius_scale == 1.65F && config.surface_density_scale == 0.42F,
                "water should default to implicit surface splat controls");
        require(config.surface_smoothing_radius_px == 7.0F &&
                    config.surface_smoothing_iterations == 3,
                "water should default to a lightly smoothed implicit surface");
        require(config.surface_refraction_strength == 0.018F,
                "water should default to subtle surface refraction");
        require(config.surface_caustic_strength == 0.18F,
                "water should default to subtle caustic highlights");
        require(config.surface_specular_strength == 0.35F,
                "water should default to a bounded specular highlight");
        require(config.foam_strength == 0.32F, "water should default to subtle foam");
        require(config.foam_sharpness == 1.35F && config.foam_breakup == 0.45F,
                "water should default to shaped foam breakup");
        require(config.obstacle_shape ==
                    cubey::projects::fluid::water_2d::Water2DObstacleShape::None,
                "water should default to no obstacle");
        require(!config.hose.enabled, "water should default with hose emission disabled");
        require(!config.drain.enabled, "water should default with drain disabled");
        require(!config.wave.enabled, "water should default with wave forcing disabled");
        require(config.drain.pull_speed > 0.0F && config.drain.pull_radius > 0.0F,
                "water drain should expose pull controls");
        require(sizeof(cubey::projects::fluid::water_2d::Water2DSimulationUniforms) ==
                    sizeof(float) *
                        cubey::projects::fluid::water_2d::kWater2DSimulationUniformFloatCount,
                "water simulation uniform struct should match the shader contract size");
        require(sizeof(cubey::projects::fluid::water_2d::Water2DDispatchPushConstants) ==
                    sizeof(float) *
                        cubey::projects::fluid::water_2d::kWater2DSimulationPushConstantFloatCount,
                "water dispatch push constants should match the shader contract size");
        require(cubey::projects::fluid::water_2d::kWater2DRenderPushConstantFloatCount == 20,
                "water render push constants should include style controls");

        require(cubey::projects::fluid::water_2d::cell_count(config) == kExpectedCellCount,
                "water cell count should multiply dimensions");
        require(cubey::projects::fluid::water_2d::u_face_count(config) == kExpectedUFaceCount,
                "water U faces should include one more vertical face column");
        require(cubey::projects::fluid::water_2d::v_face_count(config) == kExpectedVFaceCount,
                "water V faces should include one more horizontal face row");
        require(cubey::projects::fluid::water_2d::particle_bin_index_count(config) ==
                    kExpectedBinIndexCount,
                "water particle bins should allocate fixed cell slots");
        require(
            cubey::projects::fluid::water_2d::water_2d_shader_count_float(
                cubey::projects::fluid::water_2d::kWater2DMaxExactShaderInteger, "test") ==
                static_cast<float>(cubey::projects::fluid::water_2d::kWater2DMaxExactShaderInteger),
            "water shader count helper should accept the exact float integer cap");
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
        require(cubey::projects::fluid::water_2d::particle_affine_buffer_byte_size(config) ==
                    cubey::projects::fluid::water_2d::particle_buffer_byte_size(config),
                "water APIC affine state should use one vec4 per particle");
        require(cubey::projects::fluid::water_2d::diagnostics_buffer_byte_size(config) ==
                    sizeof(std::uint32_t) *
                        cubey::projects::fluid::water_2d::kWater2DDiagnosticSlotCount,
                "water diagnostics should store a fixed uint slot buffer");
        require(cubey::projects::fluid::water_2d::hose_particle_start_for_config(config) ==
                    kExpectedActiveParticleCount,
                "water hose particles should start after the active reset particles");
        require(cubey::projects::fluid::water_2d::hose_particle_pool_capacity_for_config(config) ==
                    (kExpectedParticleCapacity - kExpectedActiveParticleCount),
                "water hose pool should include every inactive particle slot");

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
        require(cubey::projects::fluid::water_2d::water_2d_debug_view_from_name("foam") ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Foam,
                "debug view parser should accept foam");
        require(cubey::projects::fluid::water_2d::next_debug_view(
                    cubey::projects::fluid::water_2d::Water2DDebugView::Solid) ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Foam,
                "debug view should move from solid to foam");
        require(cubey::projects::fluid::water_2d::next_debug_view(
                    cubey::projects::fluid::water_2d::Water2DDebugView::Foam) ==
                    cubey::projects::fluid::water_2d::Water2DDebugView::Surface,
                "debug view should cycle back to surface after foam");

        require(std::string(cubey::projects::fluid::water_2d::water_2d_scenario_name(
                    cubey::projects::fluid::water_2d::Water2DScenario::ObstacleSplash)) ==
                    "Obstacle splash",
                "water scenario names should include the obstacle splash preset");
        require(std::string(cubey::projects::fluid::water_2d::water_2d_scenario_name(
                    cubey::projects::fluid::water_2d::Water2DScenario::HoseFill)) == "Hose fill",
                "water scenario names should include the hose fill preset");
        require(std::string(cubey::projects::fluid::water_2d::water_2d_obstacle_shape_name(
                    cubey::projects::fluid::water_2d::Water2DObstacleShape::Box)) == "Box",
                "water obstacle shape names should include box obstacles");
        require(std::string(cubey::projects::fluid::water_2d::water_2d_transfer_mode_name(
                    cubey::projects::fluid::water_2d::Water2DTransferMode::Apic)) == "APIC",
                "water transfer mode names should include APIC");
        require(std::string(cubey::projects::fluid::water_2d::water_2d_transfer_mode_name(
                    cubey::projects::fluid::water_2d::Water2DTransferMode::PicFlip)) == "PIC/FLIP",
                "water transfer mode names should include PIC/FLIP");

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

        bool threw_for_tiny_grid = false;
        try {
            cubey::projects::fluid::water_2d::Water2DConfig tiny_grid = config;
            tiny_grid.grid_width = 15;
            static_cast<void>(cubey::projects::fluid::water_2d::cell_count(tiny_grid));
        } catch (const std::runtime_error&) {
            threw_for_tiny_grid = true;
        }
        require(threw_for_tiny_grid, "water config should reject grids narrower than 16 cells");

        bool threw_for_inexact_grid = false;
        try {
            cubey::projects::fluid::water_2d::Water2DConfig inexact_grid = config;
            inexact_grid.grid_width =
                cubey::projects::fluid::water_2d::kWater2DMaxExactShaderInteger + 1U;
            static_cast<void>(cubey::projects::fluid::water_2d::cell_count(inexact_grid));
        } catch (const std::runtime_error&) {
            threw_for_inexact_grid = true;
        }
        require(threw_for_inexact_grid,
                "water config should reject grid dimensions outside exact float range");

        bool threw_for_inexact_face_count = false;
        try {
            cubey::projects::fluid::water_2d::Water2DConfig inexact_faces = config;
            inexact_faces.grid_width = 4096;
            inexact_faces.grid_height = 4096;
            static_cast<void>(cubey::projects::fluid::water_2d::u_face_count(inexact_faces));
        } catch (const std::runtime_error&) {
            threw_for_inexact_face_count = true;
        }
        require(threw_for_inexact_face_count,
                "water config should reject face counts outside exact float range");

        bool threw_for_inexact_particle_capacity = false;
        try {
            cubey::projects::fluid::water_2d::Water2DConfig inexact_particles = config;
            inexact_particles.hose.particle_capacity =
                cubey::projects::fluid::water_2d::kWater2DMaxExactShaderInteger;
            cubey::projects::fluid::water_2d::refresh_particle_counts(inexact_particles);
        } catch (const std::runtime_error&) {
            threw_for_inexact_particle_capacity = true;
        }
        require(threw_for_inexact_particle_capacity,
                "water config should reject particle capacity outside exact float range");

        bool threw_for_inexact_shader_count = false;
        try {
            static_cast<void>(cubey::projects::fluid::water_2d::water_2d_shader_count_float(
                static_cast<std::size_t>(
                    cubey::projects::fluid::water_2d::kWater2DMaxExactShaderInteger) +
                    1U,
                "test"));
        } catch (const std::runtime_error&) {
            threw_for_inexact_shader_count = true;
        }
        require(threw_for_inexact_shader_count,
                "water shader count helper should reject inexact float integers");

        cubey::projects::fluid::water_2d::Water2DConfig minimum_grid = config;
        minimum_grid.grid_width = 16;
        minimum_grid.grid_height = 16;
        cubey::projects::fluid::water_2d::refresh_particle_counts(minimum_grid);
        require(minimum_grid.active_particle_count == (8U * 11U * 4U),
                "water minimum grid should size active particles like the reset shader");
        require(minimum_grid.initial_particle_capacity == (12U * 12U * 4U),
                "water minimum grid should clamp maximum fill to the interior border");

        const cubey::RunConfig default_run_config;
        const cubey::projects::fluid::water_2d::Water2DConfig default_from_run_config =
            cubey::projects::fluid::water_2d::water_2d_config_from_run_config(default_run_config);
        require(default_from_run_config.grid_width == config.grid_width,
                "default run config should preserve water grid width");
        require(default_from_run_config.grid_height == config.grid_height,
                "default run config should preserve water grid height");
        require(default_from_run_config.active_particle_count == config.active_particle_count,
                "default run config should preserve water active particle count");
        require(default_from_run_config.initial_particle_capacity ==
                    config.initial_particle_capacity,
                "default run config should preserve water initial particle capacity");
        require(default_from_run_config.particle_capacity == config.particle_capacity,
                "default run config should preserve water particle capacity");
        require(!default_from_run_config.profile_diagnostics &&
                    default_from_run_config.profile_diagnostic_interval == 1U,
                "water diagnostics should be opt-in with per-frame sampling by default");

        cubey::RunConfig run_config;
        run_config.grid.width = 320;
        run_config.grid.height = 180;
        const cubey::projects::fluid::water_2d::Water2DConfig configured =
            cubey::projects::fluid::water_2d::water_2d_config_from_run_config(run_config);
        require(configured.grid_width == 320, "water config should honor run config grid width");
        require(configured.grid_height == 180, "water config should honor run config grid height");
        require(configured.active_particle_count == (160U * 126U * 4U),
                "water config should size active particles from configured grid dimensions");
        require(configured.initial_particle_capacity == (294U * 165U * 4U),
                "water config should size initial capacity from configured grid dimensions");
        require(configured.particle_capacity ==
                    ((294U * 165U * 4U) + kExpectedHoseParticleCapacity),
                "water config should add hose capacity to configured particle capacity");

        cubey::RunConfig transfer_run_config;
        transfer_run_config.water2d.transfer_mode = "pic-flip";
        transfer_run_config.water2d.transfer_limit = 48;
        transfer_run_config.water2d.hose = 1;
        transfer_run_config.water2d.drain = 1;
        transfer_run_config.water2d.wave = 1;
        const cubey::projects::fluid::water_2d::Water2DConfig transfer_config =
            cubey::projects::fluid::water_2d::water_2d_config_from_run_config(transfer_run_config);
        require(transfer_config.transfer_mode ==
                    cubey::projects::fluid::water_2d::Water2DTransferMode::PicFlip,
                "water run-config should parse transfer mode");
        require(transfer_config.max_particles_per_cell == 48,
                "water run-config should parse transfer sample limit");
        require(transfer_config.hose.enabled && transfer_config.drain.enabled &&
                    transfer_config.wave.enabled,
                "water run-config should parse hose, drain, and wave toggles");

        cubey::RunConfig diagnostics_run_config;
        diagnostics_run_config.headless = true;
        diagnostics_run_config.profile_diagnostics = true;
        diagnostics_run_config.profile_diagnostic_interval = 7U;
        const cubey::projects::fluid::water_2d::Water2DConfig diagnostics_config =
            cubey::projects::fluid::water_2d::water_2d_config_from_run_config(
                diagnostics_run_config);
        require(diagnostics_config.profile_diagnostics &&
                    diagnostics_config.profile_diagnostic_interval == 7U,
                "water run-config should preserve profile diagnostics flags");
        bool rejected_windowed_diagnostics = false;
        try {
            cubey::RunConfig windowed_diagnostics;
            windowed_diagnostics.profile_diagnostics = true;
            static_cast<void>(cubey::projects::fluid::water_2d::water_2d_config_from_run_config(
                windowed_diagnostics));
        } catch (const std::runtime_error&) {
            rejected_windowed_diagnostics = true;
        }
        require(rejected_windowed_diagnostics,
                "water profile diagnostics should require headless mode");

        cubey::projects::fluid::water_2d::Water2DConfig edited_fill = config;
        edited_fill.initial_fill_width = 0.25F;
        edited_fill.initial_fill_height = 0.25F;
        cubey::projects::fluid::water_2d::refresh_particle_counts(edited_fill);
        require(edited_fill.active_particle_count == (64U * 36U * 4U),
                "water runtime fill edits should update the active particle count");
        require(edited_fill.initial_particle_capacity == config.initial_particle_capacity,
                "water runtime fill edits should retain the initial particle capacity");
        require(edited_fill.particle_capacity == config.particle_capacity,
                "water runtime fill edits should retain the allocated particle capacity");
        cubey::projects::fluid::water_2d::Water2DConfig obstacle_splash = config;
        obstacle_splash.scenario =
            cubey::projects::fluid::water_2d::Water2DScenario::ObstacleSplash;
        cubey::projects::fluid::water_2d::apply_water_2d_scenario_defaults(obstacle_splash);
        require(obstacle_splash.obstacle_shape ==
                    cubey::projects::fluid::water_2d::Water2DObstacleShape::Circle,
                "obstacle splash scenario should enable a circle obstacle");
        require(obstacle_splash.active_particle_count == (158U * 89U * 4U),
                "obstacle splash scenario should resize active particles from preset fill");
        cubey::projects::fluid::water_2d::Water2DConfig wave_slab = config;
        wave_slab.scenario = cubey::projects::fluid::water_2d::Water2DScenario::WaveSlab;
        cubey::projects::fluid::water_2d::apply_water_2d_scenario_defaults(wave_slab);
        require(wave_slab.obstacle_shape ==
                    cubey::projects::fluid::water_2d::Water2DObstacleShape::None,
                "wave slab scenario should leave obstacles disabled by default");
        require(wave_slab.wave.enabled, "wave slab scenario should enable wave forcing");
        require(wave_slab.active_particle_count == (215U * 60U * 4U),
                "wave slab scenario should resize active particles from preset fill");
        cubey::projects::fluid::water_2d::Water2DConfig hose_fill = config;
        hose_fill.scenario = cubey::projects::fluid::water_2d::Water2DScenario::HoseFill;
        cubey::projects::fluid::water_2d::apply_water_2d_scenario_defaults(hose_fill);
        require(hose_fill.hose.enabled, "hose fill scenario should enable hose emission");
        require(hose_fill.drain.enabled, "hose fill scenario should enable bottom drain");
        require(hose_fill.active_particle_count == (71U * 25U * 4U),
                "hose fill scenario should start from a smaller settled water volume");
        require(hose_fill.particle_capacity == config.particle_capacity,
                "hose fill scenario should keep the same total particle capacity");
        require(cubey::projects::fluid::water_2d::hose_particle_pool_capacity_for_config(
                    hose_fill) == (config.particle_capacity - hose_fill.active_particle_count),
                "hose fill scenario should be able to emit into all inactive particle slots");
        cubey::projects::fluid::water_2d::Water2DRuntimeState runtime_state;
        require(!runtime_state.pressure_read_b,
                "water runtime should default pressure debug to pressure A");
        require(cubey::projects::fluid::water_2d::water_2d_runtime_particle_scan_count(
                    config, runtime_state) == config.active_particle_count,
                "water runtime particle scan count should default to active reset particles");
        runtime_state.particle_scan_count = config.active_particle_count + 64U;
        require(cubey::projects::fluid::water_2d::water_2d_runtime_particle_scan_count(
                    config, runtime_state) == (config.active_particle_count + 64U),
                "water runtime particle scan count should include touched hose slots");
        runtime_state.particle_scan_count = config.particle_capacity + 64U;
        require(cubey::projects::fluid::water_2d::water_2d_runtime_particle_scan_count(
                    config, runtime_state) == config.particle_capacity,
                "water runtime particle scan count should clamp to particle capacity");
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
        const std::string contract_shader =
            read_text_file(source_root / "shaders/water_2d_contract.glsl");
        const std::string reset_shader =
            read_text_file(source_root / "shaders/water_2d_reset.comp");
        const std::string commands_source = read_text_file(source_root / "water_2d_commands.cpp");
        const std::string app_source = read_text_file(source_root / "water_2d_app.cpp");
        const std::string diagnostics_source =
            read_text_file(source_root / "water_2d_diagnostics.cpp");
        const std::string gpu_resources_source =
            read_text_file(source_root / "water_2d_gpu_resources.cpp");
        const std::string build_bins_shader =
            read_text_file(source_root / "shaders/water_2d_build_bins.comp");
        const std::string emit_shader =
            read_text_file(source_root / "shaders/water_2d_emit_particles.comp");
        const std::string p2g_shader =
            read_text_file(source_root / "shaders/water_2d_particle_to_grid.comp");
        const std::string force_shader =
            read_text_file(source_root / "shaders/water_2d_force.comp");
        const std::string divergence_shader =
            read_text_file(source_root / "shaders/water_2d_divergence.comp");
        const std::string pressure_shader =
            read_text_file(source_root / "shaders/water_2d_pressure.comp");
        const std::string projection_shader =
            read_text_file(source_root / "shaders/water_2d_projection.comp");
        const std::string g2p_shader =
            read_text_file(source_root / "shaders/water_2d_grid_to_particle.comp");
        const std::string advect_shader =
            read_text_file(source_root / "shaders/water_2d_advect_particles.comp");
        const std::string diagnostics_shader =
            read_text_file(source_root / "shaders/water_2d_diagnostics.comp");
        const std::string render_shader =
            read_text_file(source_root / "shaders/water_2d_render.frag");
        const std::string surface_density_vert =
            read_text_file(source_root / "shaders/water_2d_surface_density.vert");
        const std::string surface_density_frag =
            read_text_file(source_root / "shaders/water_2d_surface_density.frag");
        const std::string surface_smooth_frag =
            read_text_file(source_root / "shaders/water_2d_surface_smooth.frag");
        const std::string surface_composite_frag =
            read_text_file(source_root / "shaders/water_2d_surface_composite.frag");
        require_contains(contract_shader, "WATER2D_BINDING_SIM_PARAMS 14",
                         "water shader contract should define simulation uniform binding");
        require_contains(contract_shader, "WATER2D_BINDING_PARTICLE_AFFINE 15",
                         "water shader contract should define the APIC affine binding");
        require_contains(contract_shader, "WATER2D_BINDING_DIAGNOSTICS 16",
                         "water shader contract should define diagnostics storage binding");
        require_contains(contract_shader, "WATER2D_TRANSFER_MODE",
                         "water shader contract should expose transfer mode");
        require_contains(contract_shader, "WATER2D_PARTICLE_SCAN_COUNT",
                         "water shader contract should expose particle scan count");
        require_contains(contract_shader, "WATER2D_VOLUME_STRENGTH",
                         "water shader contract should expose particle volume strength");
        require_contains(contract_shader, "uniform SimulationParams",
                         "water shader contract should move stable simulation params to a UBO");
        require_contains(contract_shader, "uniform DispatchParams",
                         "water shader contract should keep per-dispatch values in push constants");
        require_contains(contract_shader, "style_options",
                         "water shader contract should expose surface style controls");
        require_contains(contract_shader, "wave_options0",
                         "water shader contract should expose wave source controls");
        require_contains(contract_shader, "WATER2D_ACTIVE_PARTICLE_COUNT",
                         "water shader contract should expose active initial particles");
        require_contains(contract_shader, "WATER2D_PARTICLE_CAPACITY",
                         "water shader contract should expose full particle capacity");
        require_contains(reset_shader, "ParticlePositions",
                         "water reset shader should initialize particle positions");
        require_contains(reset_shader, "water_2d_contract.glsl",
                         "water reset shader should use the shared water shader contract");
        require_contains(reset_shader, "uint scenario",
                         "water reset shader should branch on scenario presets");
        require_contains(reset_shader, "obstacle_extents",
                         "water reset shader should support obstacle box extents");
        require_contains(reset_shader,
                         "particle_positions.values[id] = vec4(-10.0, -10.0, 0.0, 0.0)",
                         "water reset shader should mark unused particle slots inactive");
        require_contains(reset_shader, "ParticleAffine",
                         "water reset shader should bind APIC affine state");
        require_contains(reset_shader, "particle_affine.values[id] = vec4(0.0)",
                         "water reset shader should clear APIC affine state");
        require_contains(reset_shader, "kWater2DWallCells",
                         "water reset shader should use a named wall-cell border");
        require_contains(reset_shader, "width - (kWater2DWallCells * 2u)",
                         "water reset shader should clamp fill width to the usable interior");
        require_contains(reset_shader, "relocate_spawn_outside_obstacle",
                         "water reset shader should avoid spawning particles inside obstacles");
        require_contains(commands_source, "runtime_state.pressure_read_b = final_pressure_is_b",
                         "water commands should track the last solved pressure buffer");
        require_contains(commands_source, "runtime_state.pressure_read_b ? 1.0F : 0.0F",
                         "water render should use the tracked pressure buffer");
        require_contains(commands_source, "diagnostics_workload_dispatch_groups",
                         "water commands should profile workload diagnostics");
        require_contains(commands_source, "should_record_diagnostics_for_frame",
                         "water commands should gate diagnostics by sample interval");
        require_contains(commands_source, "water surface density",
                         "water commands should build an offscreen density surface");
        require_contains(commands_source, "water surface smooth x",
                         "water commands should smooth the implicit density surface");
        require_contains(commands_source, "surface_smoothing_iterations",
                         "water commands should gate configurable surface smoothing passes");
        require_contains(app_source, "resolved_sampled_texture_view",
                         "water app should bind graph transient surface textures");
        require_contains(build_bins_shader, "WATER2D_BINDING_CELL_COUNTS",
                         "water bin build should use shared descriptor binding names");
        require_contains(build_bins_shader, "atomicAdd",
                         "water bin build should atomically count particles per cell");
        require_contains(build_bins_shader, "max_particles_per_cell",
                         "water bin build should clamp fixed-capacity cell slots");
        require_contains(build_bins_shader, "particle_positions.values[particle_id].w < 0.5",
                         "water bin build should skip inactive hose-pool particles");
        require_contains(build_bins_shader, "WATER2D_PARTICLE_SCAN_COUNT",
                         "water bin build should scan only touched particle slots");
        require_contains(emit_shader, "WATER2D_HOSE_POOL_START",
                         "water emit shader should target the reserved hose particle range");
        require_contains(emit_shader, "WATER2D_EMIT_CURSOR",
                         "water emit shader should use the ring cursor from dispatch constants");
        require_contains(emit_shader,
                         "particle_positions.values[particle_id] = vec4(position, 0.0, 1.0)",
                         "water emit shader should activate emitted material particles");
        require_contains(emit_shader, "particle_velocities.values[particle_id]",
                         "water emit shader should inject particle momentum");
        require_contains(emit_shader, "particle_affine.values[particle_id] = vec4(0.0)",
                         "water emit shader should clear emitted APIC affine state");
        require_contains(p2g_shader, "gather_face_velocity",
                         "water particle-to-grid shader should gather face velocities");
        require_contains(p2g_shader, "u_previous.values",
                         "water particle-to-grid shader should preserve pre-solve face velocity");
        require_contains(p2g_shader, "particle_affine",
                         "water particle-to-grid shader should read APIC affine state");
        require_contains(p2g_shader, "particle_velocity += affine * delta",
                         "water particle-to-grid shader should apply APIC local velocity");
        require_contains(divergence_shader, "WATER2D_VOLUME_STRENGTH",
                         "water divergence shader should add particle-volume expansion");
        require_contains(divergence_shader, "stored_particles = float(cell_counts.values[index])",
                         "water divergence shader should use raw occupancy for volume pressure");
        require_contains(divergence_shader, "divergence.values[index] = velocity_divergence -",
                         "water divergence shader should turn overpacked cells into sources");
        require_contains(pressure_shader, "cell_counts.values[index] > 0u",
                         "water pressure shader should solve only occupied liquid cells");
        require_contains(projection_shader, "read_pressure",
                         "water projection shader should read the selected pressure buffer");
        require_contains(g2p_shader, "flip_velocity",
                         "water grid-to-particle shader should support FLIP velocity updates");
        require_contains(g2p_shader, "params.particle_options.w",
                         "water grid-to-particle shader should use the configured PIC/FLIP blend");
        require_contains(g2p_shader, "affine_from_grid",
                         "water grid-to-particle shader should reconstruct APIC affine state");
        require_contains(g2p_shader, "particle_affine.values[particle_id]",
                         "water grid-to-particle shader should write APIC affine state");
        require_contains(g2p_shader, "WATER2D_PARTICLE_SCAN_COUNT",
                         "water grid-to-particle shader should scan only touched particle slots");
        require_contains(g2p_shader, "WATER2D_BINDING_CELL_PARTICLE_INDICES",
                         "water grid-to-particle shader should inspect neighbor particle bins");
        require_contains(g2p_shader, "separation_velocity",
                         "water grid-to-particle shader should add particle separation");
        require_contains(g2p_shader, "center_count <= target_particles_per_cell",
                         "water particle separation should only run for overpacked cells");
        require_contains(g2p_shader, "sample_velocity_confidence",
                         "water grid-to-particle shader should sample transfer confidence");
        require_contains(g2p_shader, "sparse_droplet_blend",
                         "water grid-to-particle shader should keep unsupported droplets "
                         "on ballistic fallback");
        require_contains(g2p_shader, "params.solve_options.y",
                         "water grid-to-particle shader should use the velocity limit");
        require_contains(advect_shader, "collide_obstacle",
                         "water particle advection should collide against the optional obstacle");
        require_contains(advect_shader, "velocity = -velocity * restitution",
                         "water particle advection should reflect wall collision velocity");
        require_contains(advect_shader, "bool collided = false",
                         "water particle advection should track collision state");
        require_contains(
            advect_shader, "if (collided)",
            "water particle advection should clear APIC affine state after collisions");
        require_contains(advect_shader, "inside_drain",
                         "water particle advection should support a box drain sink");
        require_contains(advect_shader,
                         "particle_positions.values[particle_id] = vec4(-10.0, -10.0, 0.0, 0.0)",
                         "water particle advection should deactivate drained particles");
        require_contains(advect_shader, "particle_affine.values[particle_id] = vec4(0.0)",
                         "water particle advection should clear drained APIC affine state");
        require_contains(advect_shader, "params.solve_options.z",
                         "water particle advection should use configured damping");
        require_contains(advect_shader, "WATER2D_PARTICLE_SCAN_COUNT",
                         "water particle advection should scan only touched particle slots");
        require_contains(diagnostics_shader, "SLOT_ACTIVE_PARTICLES",
                         "water diagnostics should count active particles");
        require_contains(diagnostics_shader, "SLOT_NONEMPTY_CELLS",
                         "water diagnostics should count occupied cells");
        require_contains(diagnostics_shader, "SLOT_TRANSFER_TRUNCATED_PARTICLES",
                         "water diagnostics should report transfer truncation pressure");
        require_contains(diagnostics_shader, "atomicMax",
                         "water diagnostics should record max cell occupancy");
        require_contains(force_shader, "wave_gate",
                         "water force shader should support an optional wave driver");
        require_contains(force_shader, "drain_target_velocity",
                         "water force shader should pull liquid toward the drain");
        require_contains(force_shader, "drive_toward",
                         "water force shader should blend source targets into grid velocity");
        require_contains(app_source, "record_gpu_timings(context.profile_recorder()",
                         "water windowed path should export GPU timings");
        require_contains(app_source, "resources_.diagnostics().handle()",
                         "water headless path should read back diagnostics metrics");
        require_contains(diagnostics_source, "water_2d.workload",
                         "water diagnostics readback should export workload metrics");
        require_contains(gpu_resources_source, "water_2d_diagnostics.comp.spv",
                         "water GPU resources should create diagnostics compute pipeline");
        require_contains(gpu_resources_source, "water_2d_surface_density.frag.spv",
                         "water GPU resources should create the surface density pipeline");
        require_contains(gpu_resources_source, "water_2d_surface_smooth.frag.spv",
                         "water GPU resources should create the surface smoothing pipeline");
        require_contains(gpu_resources_source, "water_2d_surface_composite.frag.spv",
                         "water GPU resources should create the surface composite pipeline");
        require_contains(gpu_resources_source, "update_surface_descriptors",
                         "water GPU resources should update transient surface descriptors");
        require_contains(gpu_resources_source, "VK_BUFFER_USAGE_TRANSFER_SRC_BIT",
                         "water diagnostics buffer should be readable by GPU readback");
        require_contains(render_shader, "particle_density",
                         "water render shader should draw from particle bins");
        require_contains(render_shader, "params.surface_options",
                         "water render shader should use configurable surface shading");
        require_contains(render_shader, "params.foam_options",
                         "water render shader should use configurable foam shaping");
        require_contains(render_shader, "value_noise",
                         "water render shader should break up foam with procedural noise");
        require_contains(render_shader, "debug_mode == 7",
                         "water render shader should expose a foam debug view");
        require_contains(render_shader, "vec2 uv = vec2(screen_uv.x, 1.0 - screen_uv.y)",
                         "water render shader should flip screen Y to solver Y");
        require_contains(surface_density_vert, "gl_InstanceIndex",
                         "water surface density pass should draw instanced particle splats");
        require_contains(surface_density_frag, "out_density",
                         "water surface density pass should output scalar density");
        require_contains(surface_smooth_frag, "kMaxSmoothRadius",
                         "water surface smoothing should clamp its sample radius");
        require_contains(surface_smooth_frag, "params.foam_options.w < 0.5",
                         "water surface smoothing should switch between x/y passes");
        require_contains(surface_composite_frag, "fwidth(density)",
                         "water surface composite should antialias the implicit threshold");
        require_contains(surface_composite_frag, "density_gradient",
                         "water surface composite should shade from smoothed density gradients");
        require_contains(surface_composite_frag, "scene_backdrop",
                         "water surface composite should render a tank backdrop");
        require_contains(surface_composite_frag, "caustic_pattern",
                         "water surface composite should add procedural caustics");
        require_contains(surface_composite_frag, "curvature",
                         "water surface composite should use curvature for thin foam");
        require_contains(surface_composite_frag, "params.style_options.x",
                         "water surface composite should use configurable refraction");
        require_contains(surface_composite_frag, "params.style_options.y",
                         "water surface composite should use configurable caustics");
        require_contains(surface_composite_frag, "params.style_options.z",
                         "water surface composite should use configurable specular");
        require_contains(surface_composite_frag, "params.style_options.w",
                         "water surface composite should use frame time for animated styling");
        require_contains(surface_composite_frag, "layout(set = 1",
                         "water surface composite should still sample solver fields");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "water_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
