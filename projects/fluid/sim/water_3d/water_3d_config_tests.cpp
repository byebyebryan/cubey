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
        constexpr std::size_t kExpectedTotalFaceCount =
            kExpectedUFaceCount + kExpectedVFaceCount + kExpectedWFaceCount;
        constexpr std::size_t kExpectedActiveParticleCount = std::size_t{32} * 44U * 32U * 4U;
        constexpr std::size_t kExpectedParticleCapacity = std::size_t{48} * 48U * 48U * 4U;

        require(config.grid_width == 64 && config.grid_height == 64 && config.grid_depth == 64,
                "water 3D should default to a 64^3 grid");
        require(config.pressure_iterations == 32,
                "water 3D should default to a bounded pressure solve");
        require(config.particles_per_cell == 4, "water 3D should seed four particles per cell");
        require(config.max_particles_per_cell == 32,
                "water 3D P2G should cap per-cell transfer samples");
        require(config.active_particle_count == kExpectedActiveParticleCount,
                "water 3D active particles should come from the default fill volume");
        require(config.particle_capacity == kExpectedParticleCapacity,
                "water 3D capacity should cover the maximum editable fill volume");
        require(config.transfer_mode == water::Water3DTransferMode::Apic,
                "water 3D should default to APIC transfer");
        require(config.p2g_mode == water::Water3DP2GMode::ActiveFaces,
                "water 3D should default to the proven active-face P2G path");
        require(config.initial_fill_width == 0.50F && config.initial_fill_height == 0.70F &&
                    config.initial_fill_depth == 0.50F,
                "water 3D should default to the planned centered dam fill");
        require(config.surface_thickness_scale == 0.65F,
                "water 3D should default to clearer surface thickness");
        require(config.surface_gap_fill_radius_px == 1.0F,
                "water 3D should default to conservative surface gap fill");
        require(config.surface_smoothing_radius_world == 0.010F &&
                    config.surface_smoothing_iterations == 2,
                "water 3D should default to moderate world-space surface smoothing");
        require(config.surface_depth_sigma == 0.025F && config.surface_thickness_smoothing == 0.5F,
                "water 3D should soften thickness footprints by default");
        require(config.surface_absorption == 0.8F && config.surface_refraction_strength == 0.025F,
                "water 3D should default to clearer water shading");
        require(config.foam_amount == 0.70F && config.foam_sharpness == 2.2F,
                "water 3D should default to a visible screen-space foam layer");
        require(config.whitewater_enabled && config.whitewater_capacity == 65536U &&
                    config.whitewater_max_emit_per_frame == 2048U,
                "water 3D should default to bounded visual whitewater");
        require(config.whitewater_intensity == 1.0F && config.whitewater_speed_threshold == 1.1F &&
                    config.whitewater_lifetime == 1.6F && config.whitewater_radius == 0.010F,
                "water 3D should default to visible whitewater emission");
        require(config.whitewater_drag == 0.94F && config.whitewater_gravity_scale == 0.55F,
                "water 3D should default to damped spray whitewater");
        require(!config.profile_diagnostics && config.profile_diagnostic_interval == 1U,
                "water 3D diagnostics should be opt-in with per-frame sampling by default");
        require(sizeof(water::Water3DSimulationUniforms) ==
                    sizeof(float) * water::kWater3DSimulationUniformFloatCount,
                "water 3D simulation uniforms should match the shader contract");
        require(sizeof(water::Water3DDispatchPushConstants) ==
                    sizeof(float) * water::kWater3DSimulationPushConstantFloatCount,
                "water 3D dispatch constants should match the shader contract");
        require(water::kWater3DRenderPushConstantFloatCount >= 40,
                "water 3D render constants should leave room for surface reconstruction");

        require(water::cell_count(config) == kExpectedCellCount,
                "water 3D cell count should multiply dimensions");
        require(water::u_face_count(config) == kExpectedUFaceCount,
                "water 3D U faces should include one extra x face");
        require(water::v_face_count(config) == kExpectedVFaceCount,
                "water 3D V faces should include one extra y face");
        require(water::w_face_count(config) == kExpectedWFaceCount,
                "water 3D W faces should include one extra z face");
        require(water::total_face_count(config) == kExpectedTotalFaceCount,
                "water 3D total face count should include all staggered grids");
        require(water::particle_buffer_byte_size(config) ==
                    sizeof(float) * kExpectedParticleCapacity * 4U,
                "water 3D particle buffers should store vec4 capacity");
        require(water::particle_affine_buffer_byte_size(config) ==
                    sizeof(float) * kExpectedParticleCapacity * 12U,
                "water 3D APIC affine buffers should store three vec4 rows per particle");
        require(water::whitewater_buffer_byte_size(config) ==
                    sizeof(float) * water::kWater3DDefaultWhitewaterCapacity * 4U,
                "water 3D whitewater buffers should store vec4 capacity");
        require(water::whitewater_counter_byte_size(config) == sizeof(std::uint32_t) * 4U,
                "water 3D whitewater counters should store four uint counters");
        require(water::whitewater_active_index_byte_size(config) ==
                    sizeof(std::uint32_t) * water::kWater3DDefaultWhitewaterCapacity,
                "water 3D whitewater active indices should store one uint per particle");
        require(water::whitewater_draw_arg_byte_size(config) == sizeof(std::uint32_t) * 4U,
                "water 3D whitewater draw args should store one indirect draw command");
        require(water::diagnostics_buffer_byte_size(config) ==
                    sizeof(std::uint32_t) * water::kWater3DDiagnosticSlotCount,
                "water 3D diagnostics should store a fixed uint slot buffer");
        require(water::particle_sort_scan_level0_count(config) == 1024U,
                "water 3D particle sorting should scan one block per 256 cells");
        require(water::particle_sort_scan_level1_count(config) == 4U,
                "water 3D particle sorting should reserve second-level scan blocks");
        require(water::particle_sort_scan_level2_count(config) == 1U,
                "water 3D particle sorting should terminate scan hierarchy at one block");
        require(water::active_work_count_byte_size(config) == sizeof(std::uint32_t) * 4U,
                "water 3D active work counters should reserve four uint counters");
        require(water::active_face_flag_byte_size(config) ==
                    sizeof(std::uint32_t) * kExpectedTotalFaceCount,
                "water 3D active face flags should cover every staggered face");
        require(water::active_face_index_byte_size(config) ==
                    sizeof(std::uint32_t) * kExpectedTotalFaceCount,
                "water 3D active face indices should cover every staggered face");
        require(water::active_face_dispatch_arg_byte_size(config) == sizeof(std::uint32_t) * 3U,
                "water 3D active face dispatch args should store one indirect dispatch command");
        require(water::water_3d_p2g_tile_count_x(config) == 16U &&
                    water::water_3d_p2g_tile_count_y(config) == 16U &&
                    water::water_3d_p2g_tile_count_z(config) == 16U,
                "water 3D P2G tiles should default to 4x4x4 cell groups");
        require(water::water_3d_p2g_tile_count(config) == 4096U,
                "water 3D P2G tile count should cover the grid");
        require(water::active_tile_flag_byte_size(config) == sizeof(std::uint32_t) * 4096U,
                "water 3D active tile flags should cover every P2G tile");
        require(water::active_tile_index_byte_size(config) == sizeof(std::uint32_t) * 4096U,
                "water 3D active tile indices should cover every P2G tile");
        require(water::active_tile_dispatch_arg_byte_size(config) == sizeof(std::uint32_t) * 3U,
                "water 3D active tile dispatch args should store one indirect dispatch command");
        require(std::string(water::water_3d_p2g_mode_name(water::Water3DP2GMode::ActiveFaces)) ==
                    "active",
                "water 3D P2G mode names should include active");
        require(water::water_3d_p2g_mode_from_name("tiled") == water::Water3DP2GMode::TiledFaces,
                "water 3D P2G mode parser should accept tiled");
        require(std::string(water::water_3d_render_view_name(water::Water3DRenderView::Surface)) ==
                    "Surface",
                "water 3D render view names should include surface");
        require(water::water_3d_render_view_from_name("") == water::Water3DRenderView::Surface,
                "water 3D empty render view should default to surface");
        require(water::water_3d_render_view_from_name("surface-depth") ==
                    water::Water3DRenderView::SurfaceDepth,
                "water 3D render view parsing should include surface depth");
        require(water::water_3d_render_view_from_name("surface-thickness") ==
                    water::Water3DRenderView::SurfaceThickness,
                "water 3D render view parsing should include surface thickness");
        require(water::water_3d_render_view_from_name("surface-normals") ==
                    water::Water3DRenderView::SurfaceNormals,
                "water 3D render view parsing should include surface normals");
        require(water::water_3d_render_view_from_name("surface-foam") ==
                    water::Water3DRenderView::SurfaceFoam,
                "water 3D render view parsing should include surface foam");
        require(water::water_3d_render_view_from_name("whitewater") ==
                    water::Water3DRenderView::Whitewater,
                "water 3D render view parsing should include whitewater");
        require(water::is_water_3d_surface_view(water::Water3DRenderView::Surface),
                "water 3D surface render should classify the default surface view");
        require(water::is_water_3d_surface_view(water::Water3DRenderView::SurfaceDepth),
                "water 3D surface render should classify depth diagnostics");
        require(water::is_water_3d_surface_view(water::Water3DRenderView::SurfaceFoam),
                "water 3D surface render should classify foam diagnostics");
        require(water::is_water_3d_surface_view(water::Water3DRenderView::Whitewater),
                "water 3D surface render should classify whitewater diagnostics");
        require(!water::is_water_3d_surface_view(water::Water3DRenderView::Particles),
                "water 3D surface render should keep particle splats as a debug path");
        require(std::string(water::water_3d_render_view_name(water::Water3DRenderView::Overpack)) ==
                    "Overpack",
                "water 3D render view names should include overpack");
        require(water::water_3d_render_view_from_name("overpack") ==
                    water::Water3DRenderView::Overpack,
                "water 3D render view parsing should include overpack");
        require(water::next_render_view(water::Water3DRenderView::Surface) ==
                    water::Water3DRenderView::Particles,
                "water 3D render view cycle should start with surface");
        require(water::next_render_view(water::Water3DRenderView::Solid) ==
                    water::Water3DRenderView::Overpack,
                "water 3D render view cycle should include overpack after solid");
        require(water::next_render_view(water::Water3DRenderView::Overpack) ==
                    water::Water3DRenderView::SurfaceDepth,
                "water 3D render view cycle should include surface diagnostics after overpack");
        require(water::next_render_view(water::Water3DRenderView::SurfaceNormals) ==
                    water::Water3DRenderView::SurfaceFoam,
                "water 3D render view cycle should include surface foam after normals");
        require(water::next_render_view(water::Water3DRenderView::SurfaceFoam) ==
                    water::Water3DRenderView::Whitewater,
                "water 3D render view cycle should include whitewater after surface foam");
        require(water::next_render_view(water::Water3DRenderView::Whitewater) ==
                    water::Water3DRenderView::Surface,
                "water 3D render view cycle should wrap after whitewater");

        cubey::RunConfig run_config;
        run_config.grid_width = 32;
        run_config.grid_height = 48;
        run_config.grid_depth = 40;
        run_config.profile_diagnostics = true;
        run_config.profile_diagnostic_interval = 7;
        run_config.water3d_p2g_mode = "tiled";
        const water::Water3DConfig overridden = water::water_3d_config_from_run_config(run_config);
        require(overridden.grid_width == 32 && overridden.grid_height == 48 &&
                    overridden.grid_depth == 40,
                "water 3D should accept CLI grid dimensions");
        require(overridden.active_particle_count ==
                    water::active_particle_count_for_fill(overridden),
                "water 3D run-config construction should refresh active particle counts");
        require(overridden.particle_capacity == water::particle_capacity_for_config(overridden),
                "water 3D run-config construction should refresh particle capacity");
        require(overridden.profile_diagnostics && overridden.profile_diagnostic_interval == 7U,
                "water 3D run-config construction should preserve profile diagnostics flags");
        require(overridden.p2g_mode == water::Water3DP2GMode::TiledFaces,
                "water 3D run-config construction should parse P2G mode");

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
        const std::string build_bins = read_text_file(shader_dir / "water_3d_build_bins.comp");
        const std::string clear_bins = read_text_file(shader_dir / "water_3d_clear_bins.comp");
        const std::string p2g_shader =
            read_text_file(shader_dir / "water_3d_particle_to_grid.comp");
        const std::string build_active_tiles =
            read_text_file(shader_dir / "water_3d_build_active_tiles.comp");
        const std::string active_tile_dispatch_args =
            read_text_file(shader_dir / "water_3d_active_tile_dispatch_args.comp");
        const std::string p2g_tiled_shader =
            read_text_file(shader_dir / "water_3d_particle_to_grid_tiled.comp");
        const std::string copy_sort_source =
            read_text_file(shader_dir / "water_3d_copy_particle_sort_source.comp");
        const std::string scan_offsets = read_text_file(shader_dir / "water_3d_scan_offsets.comp");
        const std::string scan_add_offsets =
            read_text_file(shader_dir / "water_3d_scan_add_offsets.comp");
        const std::string scatter_sorted_particles =
            read_text_file(shader_dir / "water_3d_scatter_sorted_particles.comp");
        const std::string active_face_dispatch_args =
            read_text_file(shader_dir / "water_3d_active_face_dispatch_args.comp");
        const std::string advect_shader =
            read_text_file(shader_dir / "water_3d_advect_particles.comp");
        const std::string whitewater_clear =
            read_text_file(shader_dir / "water_3d_whitewater_clear.comp");
        const std::string whitewater_advect =
            read_text_file(shader_dir / "water_3d_whitewater_advect.comp");
        const std::string whitewater_emit =
            read_text_file(shader_dir / "water_3d_whitewater_emit.comp");
        const std::string whitewater_active_indices =
            read_text_file(shader_dir / "water_3d_whitewater_active_indices.comp");
        const std::string whitewater_draw_args =
            read_text_file(shader_dir / "water_3d_whitewater_draw_args.comp");
        const std::string diagnostics_shader =
            read_text_file(shader_dir / "water_3d_diagnostics.comp");
        const std::string whitewater_vert = read_text_file(shader_dir / "water_3d_whitewater.vert");
        const std::string whitewater_frag = read_text_file(shader_dir / "water_3d_whitewater.frag");
        const std::string extrapolate_shader =
            read_text_file(shader_dir / "water_3d_extrapolate_velocity.comp");
        const std::string g2p_shader =
            read_text_file(shader_dir / "water_3d_grid_to_particle.comp");
        const std::string divergence_shader =
            read_text_file(shader_dir / "water_3d_divergence.comp");
        const std::string render_shader = read_text_file(shader_dir / "water_3d_render.frag");
        const std::string surface_common =
            read_text_file(shader_dir / "water_3d_surface_common.glsl");
        const std::string surface_depth =
            read_text_file(shader_dir / "water_3d_surface_depth.frag");
        const std::string scene_shader = read_text_file(shader_dir / "water_3d_scene.frag");
        const std::string surface_thickness =
            read_text_file(shader_dir / "water_3d_surface_thickness.frag");
        const std::string surface_repair =
            read_text_file(shader_dir / "water_3d_surface_repair.frag");
        const std::string surface_smooth =
            read_text_file(shader_dir / "water_3d_surface_smooth.frag");
        const std::string surface_composite =
            read_text_file(shader_dir / "water_3d_surface_composite.frag");
        const std::string gpu_resources = read_text_file(
            std::filesystem::path(CUBEY_WATER_3D_SOURCE_DIR) / "water_3d_gpu_resources.cpp");
        const std::string commands = read_text_file(
            std::filesystem::path(CUBEY_WATER_3D_SOURCE_DIR) / "water_3d_commands.cpp");
        const std::string app =
            read_text_file(std::filesystem::path(CUBEY_WATER_3D_SOURCE_DIR) / "water_3d_app.cpp");

        require_contains(contract, "WATER3D_BINDING_W_FIELD",
                         "water 3D contract should expose the W face field");
        require_contains(contract, "WATER3D_BINDING_U_SCRATCH",
                         "water 3D contract should expose velocity extrapolation scratch fields");
        require_contains(contract, "WATER3D_BINDING_W_WEIGHT_SCRATCH",
                         "water 3D contract should expose extrapolation validity scratch fields");
        require_contains(contract, "WATER3D_BINDING_WHITEWATER_POSITIONS",
                         "water 3D contract should expose whitewater particles");
        require_contains(contract, "WATER3D_BINDING_WHITEWATER_ACTIVE_INDICES",
                         "water 3D contract should expose compacted whitewater draw indices");
        require_contains(contract, "WATER3D_BINDING_WHITEWATER_DRAW_ARGS",
                         "water 3D contract should expose whitewater indirect draw args");
        require_contains(contract, "WATER3D_BINDING_ACTIVE_WORK_COUNTS",
                         "water 3D contract should expose active work counters");
        require_contains(contract, "WATER3D_BINDING_ACTIVE_FACE_INDICES",
                         "water 3D contract should expose compacted active face indices");
        require_contains(contract, "WATER3D_BINDING_ACTIVE_FACE_DISPATCH_ARGS",
                         "water 3D contract should expose active face indirect dispatch args");
        require_contains(contract, "WATER3D_BINDING_ACTIVE_TILE_INDICES",
                         "water 3D contract should expose compacted active tile indices");
        require_contains(contract, "WATER3D_BINDING_ACTIVE_TILE_DISPATCH_ARGS",
                         "water 3D contract should expose active tile indirect dispatch args");
        require_contains(contract, "WATER3D_BINDING_DIAGNOSTICS",
                         "water 3D contract should expose diagnostics readback storage");
        require_contains(contract, "WATER3D_BINDING_CELL_OFFSETS",
                         "water 3D contract should expose sorted cell particle ranges");
        require_contains(contract, "WATER3D_BINDING_PARTICLE_POSITIONS_SOURCE",
                         "water 3D contract should expose particle sort source buffers");
        require_contains(contract, "WATER3D_BINDING_SORT_SCAN_LEVEL2_OFFSETS",
                         "water 3D contract should expose scan hierarchy scratch buffers");
        require_contains(contract, "WATER3D_BUILD_ACTIVE_FACES",
                         "water 3D contract should expose binning active-face control");
        require_contains(contract, "WATER3D_WHITEWATER_CAPACITY",
                         "water 3D contract should expose whitewater capacity");
        require_contains(contract, "whitewater_options",
                         "water 3D contract should expose whitewater uniforms");
        require_contains(reset_shader, "particle_affine.values[id * 3u + 2u]",
                         "water 3D reset should clear all APIC affine rows");
        require_contains(reset_shader, "whitewater_positions.values[id]",
                         "water 3D reset should clear whitewater particles");
        require_contains(build_bins, "WATER3D_BUILD_ACTIVE_FACES",
                         "water 3D binning should optionally build active face work");
        require_contains(build_bins, "atomicExchange(active_face_flags.values[face_id]",
                         "water 3D binning should deduplicate active face work");
        require_contains(build_bins, "active_face_indices.values[active_slot]",
                         "water 3D binning should compact active face indices");
        require_contains(clear_bins, "active_face_dispatch_args.values[id] = 1u",
                         "water 3D bin clearing should reset indirect dispatch args");
        require_contains(clear_bins, "active_tile_flags.values[id] = 0u",
                         "water 3D bin clearing should reset active tile flags");
        require_contains(clear_bins, "active_tile_dispatch_args.values[id] = 1u",
                         "water 3D bin clearing should reset active tile indirect args");
        require_contains(clear_bins, "cell_write_counts.values[id] = 0u",
                         "water 3D bin clearing should reset sorted scatter counters");
        require_contains(copy_sort_source, "particle_positions_source.values[particle_id]",
                         "water 3D particle sort should copy canonical positions to source");
        require_contains(scan_offsets, "scan_values[256]",
                         "water 3D particle sort should scan cell counts on GPU");
        require_contains(scan_add_offsets, "cell_offsets.values[id] +=",
                         "water 3D particle sort should apply hierarchical scan offsets");
        require_contains(scatter_sorted_particles, "cell_offsets.values[cell_id] + slot",
                         "water 3D particle sort should scatter into compact cell ranges");
        require_contains(p2g_shader, "gather_face_velocity",
                         "water 3D particle-to-grid should gather face velocities");
        require_contains(p2g_shader, "int ox_min, int ox_max",
                         "water 3D particle-to-grid should use support-aware face gathers");
        require_contains(p2g_shader, "max_particles_per_cell, -1, 0, -1",
                         "water 3D U-face P2G should skip impossible positive-x cells");
        require_contains(p2g_shader, "cell_offsets.values[cell_id]",
                         "water 3D P2G should walk sorted compact cell ranges");
        require_contains(p2g_shader, "velocity += unpack_affine(particle_id) * delta",
                         "water 3D particle-to-grid should apply APIC local velocity");
        require_contains(p2g_shader, "active_work_counts.values[0]",
                         "water 3D particle-to-grid should consume compacted active face count");
        require_contains(p2g_shader, "active_face_indices.values[active_slot]",
                         "water 3D particle-to-grid should process compacted active face indices");
        require_contains(build_active_tiles, "atomicExchange(active_tile_flags.values[tile_id]",
                         "water 3D active tile build should deduplicate tile work");
        require_contains(build_active_tiles, "active_work_counts.values[1]",
                         "water 3D active tile build should use the second active-work counter");
        require_contains(active_tile_dispatch_args, "active_tile_dispatch_args.values[0]",
                         "water 3D active tile dispatch args should write indirect group counts");
        require_contains(p2g_tiled_shader, "layout(local_size_x = 192",
                         "water 3D tiled P2G should run one workgroup per active tile");
        require_contains(p2g_tiled_shader, "active_tile_indices.values[active_tile_slot]",
                         "water 3D tiled P2G should consume compacted active tiles");
        require_contains(p2g_tiled_shader, "active_face_flags.values[global_face_id]",
                         "water 3D tiled P2G should skip inactive face slots inside active tiles");
        require_contains(p2g_tiled_shader, "cell_offsets.values[cell_id]",
                         "water 3D tiled P2G should walk sorted compact cell ranges");
        require_contains(active_face_dispatch_args, "active_face_dispatch_args.values[0]",
                         "water 3D should write indirect dispatch args for active faces");
        require_contains(advect_shader, "apply_side_wall_friction",
                         "water 3D particle advection should damp side-wall impacts");
        require_contains(whitewater_clear, "whitewater_counters.values[id] = 0u",
                         "water 3D whitewater should clear per-frame counters");
        require_contains(whitewater_advect, "sample_velocity",
                         "water 3D whitewater advection should follow the grid velocity");
        require_contains(whitewater_advect, "WATER3D_WHITEWATER_INTENSITY",
                         "water 3D whitewater advection should respect disabled emission");
        require_contains(whitewater_advect, "WATER3D_WHITEWATER_DRAG",
                         "water 3D whitewater advection should damp particles");
        require_contains(whitewater_advect, "WATER3D_WHITEWATER_GRAVITY_SCALE",
                         "water 3D whitewater advection should apply scaled gravity");
        require_contains(whitewater_advect, "classify_whitewater_kind",
                         "water 3D whitewater should classify foam, spray, and bubbles");
        require_contains(whitewater_advect, "WHITEWATER_KIND_BUBBLE",
                         "water 3D whitewater should carry explicit bubble state");
        require_contains(whitewater_emit, "free_surface_direction",
                         "water 3D whitewater emission should require free-surface particles");
        require_contains(whitewater_emit, "side_penalty",
                         "water 3D whitewater emission should damp side-surface columns");
        require_contains(whitewater_emit, "atomicAdd(whitewater_counters.values[0]",
                         "water 3D whitewater emission should bound emitted particles");
        require_contains(whitewater_emit, "uint target = (frame_seed + emit_slot) % capacity",
                         "water 3D whitewater emission should write unique target slots");
        require_contains(whitewater_emit, "WATER3D_WHITEWATER_MAX_EMIT_PER_FRAME",
                         "water 3D whitewater emission should clamp per-frame emission");
        require_contains(whitewater_active_indices, "whitewater_active_indices.values[active_slot]",
                         "water 3D whitewater should compact active particles for rendering");
        require_contains(whitewater_active_indices, "atomicAdd(whitewater_counters.values[1]",
                         "water 3D whitewater should count compacted active particles");
        require_contains(whitewater_draw_args, "whitewater_draw_args.values[1] = active_count",
                         "water 3D whitewater should write indirect draw instance count");
        require_contains(diagnostics_shader, "SLOT_ACTIVE_PARTICLES",
                         "water 3D diagnostics should count active particles");
        require_contains(diagnostics_shader, "SLOT_ACTIVE_FACE_DISPATCH_GROUPS",
                         "water 3D diagnostics should capture active-face dispatch work");
        require_contains(diagnostics_shader, "SLOT_P2G_ACTIVE_TILES",
                         "water 3D diagnostics should capture active P2G tile work");
        require_contains(diagnostics_shader, "SLOT_P2G_TILE_INACTIVE_FACE_SLOTS",
                         "water 3D diagnostics should capture inactive tiled face slots");
        require_contains(diagnostics_shader, "record_projection_residual",
                         "water 3D diagnostics should measure post-projection residuals");
        require_contains(diagnostics_shader, "SLOT_WHITEWATER_ACTIVE",
                         "water 3D diagnostics should capture whitewater active counts");
        require_contains(diagnostics_shader, "DIVERGENCE_SCALE",
                         "water 3D diagnostics should fixed-point encode divergence metrics");
        require_contains(diagnostics_shader, "DIAGNOSTIC_MODE_P2G_SCAN",
                         "water 3D diagnostics should expose a P2G scan mode");
        require_contains(diagnostics_shader, "SLOT_P2G_CELL_PARTICLE_SLOTS_SCANNED",
                         "water 3D diagnostics should count P2G particle-slot traversal");
        require_contains(diagnostics_shader, "record_p2g_face_scan",
                         "water 3D diagnostics should mirror P2G face neighborhood traversal");
        require_contains(diagnostics_shader, "int ox_min, int ox_max",
                         "water 3D diagnostics should mirror support-aware P2G gathers");
        require_contains(diagnostics_shader, "cell_offsets.values[cell_id]",
                         "water 3D diagnostics should mirror compact cell ranges");
        require_contains(diagnostics_shader, "active_face_indices.values[active_slot]",
                         "water 3D diagnostics should scan compacted active faces");
        require_contains(diagnostics_shader, "record_candidate_slot_bucket",
                         "water 3D diagnostics should bucket P2G candidate slots per face");
        require_contains(diagnostics_shader, "SLOT_P2G_CANDIDATE_SLOTS_BIN_385_PLUS",
                         "water 3D diagnostics should capture heavy P2G candidate faces");
        require_contains(diagnostics_shader, "SLOT_P2G_OVERPACKED_NEIGHBOR_CELLS_BIN_8_PLUS",
                         "water 3D diagnostics should bucket overpacked P2G neighborhoods");
        require_contains(whitewater_vert, "WATER3D_BINDING_WHITEWATER_POSITIONS",
                         "water 3D whitewater renderer should instance whitewater particles");
        require_contains(whitewater_vert, "whitewater_active_indices.values[active_slot]",
                         "water 3D whitewater renderer should draw compacted active particles");
        require_contains(whitewater_frag, "alpha * frag_linear_depth",
                         "water 3D whitewater renderer should output packed premultiplied depth");
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
        require_contains(
            g2p_shader, "fallback_velocity",
            "water 3D grid-to-particle should preserve gravity when confidence is low");
        require_contains(g2p_shader, "CellCounts",
                         "water 3D grid-to-particle should classify droplets from local occupancy");
        require_contains(g2p_shader, "sparse_droplet_blend",
                         "water 3D grid-to-particle should keep isolated droplets ballistic");
        require_contains(g2p_shader, "cell_sparsity",
                         "water 3D sparse droplets should use neighborhood cell sparsity");
        require_contains(g2p_shader, "velocity = mix(velocity, fallback_velocity, droplet_blend)",
                         "water 3D sparse droplets should blend back to gravity-driven motion");
        require_contains(divergence_shader, "side_boundary_volume_scale",
                         "water 3D divergence should reduce volume correction near side walls");
        require_contains(divergence_shader, "boundary_limited_volume_source",
                         "water 3D divergence should clamp boundary volume correction");
        require_contains(divergence_shader, "raw_volume_source",
                         "water 3D divergence should separate raw and boundary-limited source");
        require_contains(render_shader, "frag_particle",
                         "water 3D renderer should support particle splats");
        require_contains(render_shader, "out_color = vec4(linear_color, 1.0)",
                         "water 3D particle debug splats should be opaque");
        require_contains(gpu_resources, ".blend_enable = false",
                         "water 3D debug render pipeline should not alpha blend particles");
        require_contains(surface_common, "WATER3D_SURFACE_DEPTH_SENTINEL",
                         "water 3D surface pass should use an explicit empty-depth sentinel");
        require_contains(surface_common, "display_transform",
                         "water 3D surface pass should carry final display transform settings");
        require_contains(surface_common, "WATER3D_SURFACE_VIEW_FOAM",
                         "water 3D surface pass should expose a foam diagnostic view");
        require_contains(surface_common, "WATER3D_SURFACE_VIEW_WHITEWATER",
                         "water 3D surface pass should expose a whitewater diagnostic view");
        require_contains(scene_shader, "gl_FragDepth",
                         "water 3D scene pass should preserve sampleable scene depth");
        require_contains(scene_shader, "environment_cube",
                         "water 3D scene pass should use the shared environment cube");
        require_contains(surface_depth, "gl_FragDepth",
                         "water 3D surface depth pass should write sphere-correct depth");
        require_contains(surface_thickness, "out_thickness",
                         "water 3D surface thickness pass should emit additive thickness");
        require_contains(gpu_resources, ".dst_color_blend_factor = VK_BLEND_FACTOR_ONE",
                         "water 3D surface thickness pass should use additive blending");
        require_contains(surface_repair, "WATER3D_SURFACE_MAX_FILL_RADIUS",
                         "water 3D surface repair should cap hole filling");
        require_contains(surface_repair, "required_support",
                         "water 3D surface repair should reject weakly supported fills");
        require_contains(surface_smooth, "bilateral_weight",
                         "water 3D surface smoothing should be bilateral against depth");
        require_contains(surface_smooth, "screen_space_radius_px",
                         "water 3D surface smoothing should convert world radius to pixels");
        require_contains(surface_smooth, "thickness_smoothing",
                         "water 3D surface smoothing should filter thickness separately");
        require_contains(surface_composite, "dpdx_right",
                         "water 3D surface normals should use edge-aware derivatives");
        require_contains(surface_composite, "fresnel",
                         "water 3D surface composite should include water Fresnel");
        require_contains(surface_composite, "exp(-absorption",
                         "water 3D surface composite should apply Beer-Lambert absorption");
        require_contains(surface_composite, "scene_color_texture",
                         "water 3D surface composite should refract through scene color");
        require_contains(surface_composite, "scene_depth_texture",
                         "water 3D surface composite should occlude against scene depth");
        require_contains(surface_composite, "cubey_pbr_apply_display_transform",
                         "water 3D surface composite should apply the shared display transform");
        require_contains(surface_composite, "foam_mask",
                         "water 3D surface composite should derive screen-space foam");
        require_contains(surface_composite, "surface_gate",
                         "water 3D surface foam should prefer upward-facing free surfaces");
        require_contains(surface_composite, "WATER3D_SURFACE_VIEW_FOAM",
                         "water 3D surface composite should render the foam diagnostic view");
        require_contains(surface_composite, "read_whitewater_field",
                         "water 3D surface composite should decode packed whitewater fields");
        require_contains(surface_composite, "whitewater_foam",
                         "water 3D surface composite should integrate whitewater into foam");
        require_contains(surface_composite, "whitewater_surface_gate",
                         "water 3D surface composite should depth-gate whitewater foam");
        require_contains(commands, "RenderGraphFrameExecutor",
                         "water 3D surface render should be recorded through the render graph");
        require_contains(commands, "water scene",
                         "water 3D surface render should include an offscreen scene pass");
        require_contains(commands, "water surface repair",
                         "water 3D surface render should repair small surface holes");
        require_contains(commands, "surface_smoothing_iterations",
                         "water 3D surface render should support iterative smoothing");
        require_contains(commands, "update_surface_descriptors",
                         "water 3D surface render should bind graph transient textures");
        require_contains(commands, "record_whitewater_compute",
                         "water 3D simulation should run visual whitewater compute passes");
        require_contains(commands, "record_dispatch_indirect",
                         "water 3D simulation should support indirect compute dispatch");
        require_contains(commands, "active_face_dispatch_args().handle()",
                         "water 3D particle-to-grid should dispatch over active faces indirectly");
        require_contains(commands, "build active p2g tiles",
                         "water 3D simulation should profile active P2G tile compaction");
        require_contains(commands, "particle to grid tiled",
                         "water 3D simulation should profile tiled P2G dispatch");
        require_contains(commands, "diagnostics workload",
                         "water 3D simulation should profile workload diagnostics");
        require_contains(commands, "diagnostics p2g scan",
                         "water 3D simulation should profile P2G scan diagnostics");
        require_contains(commands, "diagnostics projection",
                         "water 3D simulation should profile solver residual diagnostics");
        require_contains(commands, "diagnostics whitewater",
                         "water 3D simulation should profile whitewater diagnostics");
        require_contains(commands, "copy particle sort source",
                         "water 3D simulation should profile particle sort source copies");
        require_contains(commands, "scatter sorted particles",
                         "water 3D simulation should profile sorted particle scatter");
        require_contains(commands, "should_record_diagnostics_for_frame",
                         "water 3D simulation should gate diagnostics by sample interval");
        require_contains(app, "record_gpu_timings(context.profile_recorder()",
                         "water 3D windowed path should export GPU timings to profiles");
        require_contains(app, "record_gpu_timings(profile_recorder",
                         "water 3D headless simulation path should export GPU timings to profiles");
        require_contains(app, "resources_.diagnostics().handle()",
                         "water 3D headless path should read back diagnostics metrics");
        require_contains(app, "recorder.record_metric(frame_index, category, name, value)",
                         "water 3D headless diagnostics should record profile metrics");
        require_contains(app, "water_3d.p2g",
                         "water 3D headless diagnostics should export P2G scan metrics");
        require_contains(app, "water_3d.p2g.histogram",
                         "water 3D headless diagnostics should export P2G histogram metrics");
        require_contains(app, "high_candidate_face_ratio",
                         "water 3D headless diagnostics should derive heavy candidate ratios");
        require_contains(app, "tile_slot_to_active_face_ratio",
                         "water 3D headless diagnostics should derive tiled P2G work inflation");
        require_contains(commands, "water whitewater",
                         "water 3D surface render should render whitewater before composite");
        require_contains(gpu_resources, "water_3d_active_face_dispatch_args.comp.spv",
                         "water 3D GPU resources should create active-face dispatch args");
        require_contains(gpu_resources, "water_3d_build_active_tiles.comp.spv",
                         "water 3D GPU resources should create active-tile build pipeline");
        require_contains(gpu_resources, "water_3d_active_tile_dispatch_args.comp.spv",
                         "water 3D GPU resources should create active-tile dispatch args");
        require_contains(gpu_resources, "water_3d_particle_to_grid_tiled.comp.spv",
                         "water 3D GPU resources should create tiled P2G pipeline");
        require_contains(gpu_resources, "water_3d_whitewater_emit.comp.spv",
                         "water 3D GPU resources should create whitewater compute pipelines");
        require_contains(gpu_resources, "water_3d_whitewater_active_indices.comp.spv",
                         "water 3D GPU resources should create active whitewater compaction");
        require_contains(gpu_resources, "water_3d_whitewater_draw_args.comp.spv",
                         "water 3D GPU resources should create whitewater draw args");
        require_contains(gpu_resources, "water_3d_diagnostics.comp.spv",
                         "water 3D GPU resources should create diagnostics compute pipelines");
        require_contains(gpu_resources, "water_3d_scan_offsets.comp.spv",
                         "water 3D GPU resources should create particle sort scan pipelines");
        require_contains(gpu_resources, "water_3d_scatter_sorted_particles.comp.spv",
                         "water 3D GPU resources should create sorted particle scatter");
        require_contains(gpu_resources, "VK_BUFFER_USAGE_TRANSFER_SRC_BIT",
                         "water 3D diagnostics buffer should be readable by GPU readback");
        require_contains(gpu_resources, "water_3d_whitewater.vert.spv",
                         "water 3D GPU resources should create the whitewater render pipeline");
        require_contains(gpu_resources, "water_3d.whitewater",
                         "water 3D GPU resources should label the whitewater render pass");
        require_contains(gpu_resources, ".depth_test = true",
                         "water 3D whitewater render should depth-test against the surface");
        require_contains(commands, "VK_ATTACHMENT_LOAD_OP_LOAD",
                         "water 3D whitewater render should keep the reconstructed surface depth");
        require_contains(commands, "draw_indirect(resources.whitewater_draw_args().handle()",
                         "water 3D whitewater render should use compacted indirect draw args");
        require_contains(render_shader, "render_view == 5u",
                         "water 3D renderer should expose the solid debug view");
        require_contains(render_shader, "render_view == 6u",
                         "water 3D renderer should expose the overpack debug view");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "water_3d_config_tests: %s\n", error.what());
        return 1;
    }
}
