#include "ocean_config.h"
#include "ocean_mesh.h"

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

void require_near(float value, float expected, float tolerance, const char* message) {
    require(value >= expected - tolerance && value <= expected + tolerance, message);
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
        require(defaults.mesh_lod_levels >= ocean::kOceanMinMeshLodLevels &&
                    defaults.mesh_lod_levels <= ocean::kOceanMaxMeshLodLevels,
                "default mesh LOD level count should be in supported range");
        require(ocean::ocean_mesh_vertex_count(defaults) ==
                    defaults.mesh_cells * defaults.mesh_cells * 6U,
                "ocean vertex count should match generated grid triangles");
        require(ocean::ocean_mesh_patch_count(defaults) ==
                    1U + (4U * (defaults.mesh_lod_levels - 1U)),
                "ocean clipmap patch count should match center plus annular rings");
        const ocean::OceanMeshPatchList patches = ocean::ocean_mesh_clipmap_patches(defaults);
        require(patches.count == ocean::ocean_mesh_patch_count(defaults),
                "ocean clipmap helper should emit all expected patches");
        require(patches.patches[0].level == defaults.mesh_lod_levels - 1U,
                "ocean clipmap patches should be ordered far to near for overdraw seams");
        const ocean::OceanMeshPatch& center_patch = patches.patches[patches.count - 1U];
        require(center_patch.level == 0U, "ocean clipmap should finish with the center patch");
        require(center_patch.cells_x == defaults.mesh_cells &&
                    center_patch.cells_z == defaults.mesh_cells,
                "ocean center clipmap patch should use the base mesh resolution");
        require_near(center_patch.bounds.max_x, ocean::ocean_mesh_near_half_extent(defaults),
                     0.001F, "ocean center clipmap extent should match near half extent");
        require_near(patches.patches[0].bounds.max_x, defaults.mesh_extent, 0.001F,
                     "outer ocean clipmap patch should reach configured mesh extent");
        require(ocean::ocean_mesh_total_triangle_count(defaults) >
                    defaults.mesh_cells * defaults.mesh_cells * 2U,
                "ocean clipmap should add coarser horizon geometry around the center patch");
        require(ocean::ocean_mesh_near_cell_size(defaults) > 0.0F,
                "ocean clipmap should expose a positive near cell size");
        const float center_parent_cell_size = ocean::ocean_mesh_level_cell_size(defaults, 1U);
        const float center_transition_width = ocean::ocean_mesh_transition_width(
            center_parent_cell_size, ocean::ocean_mesh_level_half_extent(defaults, 0U));
        require(center_transition_width > center_parent_cell_size,
                "ocean clipmap transition should cover multiple coarse cells");
        require(center_transition_width <= ocean::ocean_mesh_level_half_extent(defaults, 0U) *
                                               ocean::kOceanMeshMaxTransitionRatio,
                "ocean clipmap transition should be bounded by patch extent");
        bool found_level_one_transition_overlap = false;
        for (const ocean::OceanMeshPatch& patch : patches) {
            if (patch.level == 1U && patch.bounds.min_z > 0.0F) {
                require_near(patch.bounds.min_z,
                             ocean::ocean_mesh_level_half_extent(defaults, 0U) -
                                 center_transition_width,
                             0.001F, "ocean parent patch should overlap child transition band");
                found_level_one_transition_overlap = true;
                break;
            }
        }
        require(found_level_one_transition_overlap,
                "ocean clipmap should include a parent overlap for the center transition");
        require(defaults.mesh_extent > 1000.0F,
                "default ocean mesh should target horizon-scale rendering");
        require(defaults.disturbance_radius > 0.0F && defaults.disturbance_strength == 0.0F,
                "default ocean config should expose interaction hooks without radial rings");
        require(defaults.spectrum_resolution == 256,
                "ocean should default to a practical FFT spectrum resolution");
        require(defaults.sea_state > 0.0F && defaults.animation_speed > 0.0F,
                "default ocean config should split sea-state shape from animation speed");
        require(defaults.detail_chop > 0.0F && defaults.detail_spread >= 0.0F &&
                    defaults.detail_geometry > 0.0F && defaults.crest_sharpness > 0.0F,
                "default ocean config should expose directional geometric detail waves");
        require(defaults.foam_drift >= 0.0F,
                "default ocean config should expose independent foam drift");
        require(defaults.foam_breakup > 0.0F,
                "default ocean config should expose crest foam breakup");
        require(defaults.refraction_pixels > 1.0F,
                "default ocean config should expose pixel-scale scene refraction");
        require(defaults.water_opacity > 0.0F && defaults.water_opacity <= 1.0F,
                "default ocean config should expose bounded water opacity");
        require(defaults.scattering_strength > 0.0F,
                "default ocean config should expose water scattering strength");
        require(defaults.seafloor_depth > 0.0F && defaults.seafloor_brightness > 0.0F,
                "default ocean config should expose procedural seafloor controls");
        require(ocean::ocean_is_power_of_two(defaults.spectrum_resolution),
                "default ocean spectrum resolution should be a power of two");
        require(ocean::ocean_cascade_patch_length(defaults, 0) <
                    ocean::ocean_cascade_patch_length(defaults, 1),
                "near ocean cascade should be smaller than mid cascade");
        require(ocean::ocean_cascade_patch_length(defaults, 1) <
                    ocean::ocean_cascade_patch_length(defaults, 2),
                "mid ocean cascade should be smaller than far cascade");
        ocean::validate_ocean_config(defaults);

        require(ocean::ocean_render_view_from_name("") == ocean::OceanRenderView::Final,
                "empty debug view should use final ocean rendering");
        require(ocean::ocean_render_view_from_name("normal") == ocean::OceanRenderView::Normal,
                "normal debug view should parse");
        require(ocean::ocean_render_view_from_name("detail") == ocean::OceanRenderView::Detail,
                "detail debug view should parse");
        require(ocean::ocean_render_view_from_name("spectrum") == ocean::OceanRenderView::Spectrum,
                "spectrum debug view should parse");
        require(ocean::ocean_render_view_from_name("wireframe") ==
                    ocean::OceanRenderView::Wireframe,
                "wireframe debug view should parse");
        require(ocean::ocean_render_view_from_name("thickness") ==
                    ocean::OceanRenderView::Thickness,
                "thickness debug view should parse");
        require(ocean::ocean_render_view_from_name("refraction-offset") ==
                    ocean::OceanRenderView::RefractionOffset,
                "refraction offset debug view should parse");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::RefractionOffset) ==
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

        ocean::OceanConfig invalid_lod = defaults;
        invalid_lod.mesh_lod_levels = ocean::kOceanMaxMeshLodLevels + 1U;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_lod);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean mesh validation should reject unsupported LOD levels");

        ocean::OceanConfig single_lod = defaults;
        single_lod.mesh_lod_levels = 1U;
        const ocean::OceanMeshPatchList single_lod_patches =
            ocean::ocean_mesh_clipmap_patches(single_lod);
        require(single_lod_patches.count == 1U, "single-level ocean clipmap should emit one patch");
        require(ocean::ocean_mesh_total_triangle_count(single_lod) ==
                    single_lod.mesh_cells * single_lod.mesh_cells * 2U,
                "single-level ocean clipmap should match the legacy grid triangle count");

        ocean::OceanConfig invalid_spectrum = defaults;
        invalid_spectrum.spectrum_resolution = 192;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_spectrum);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean spectrum sizing should reject non-power-of-two resolutions");

        ocean::OceanConfig invalid_cascade = defaults;
        invalid_cascade.spectrum_patch_length_mid = invalid_cascade.spectrum_patch_length_near;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_cascade);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean spectrum cascades should require ordered patch lengths");

        const std::filesystem::path source_root(CUBEY_OCEAN_SOURCE_DIR);
        const std::string vertex_shader = read_text_file(source_root / "shaders/ocean.vert");
        const std::string fragment_shader = read_text_file(source_root / "shaders/ocean.frag");
        const std::string sky_shader = read_text_file(source_root / "shaders/ocean_sky.frag");
        const std::string scene_shader = read_text_file(source_root / "shaders/ocean_scene.frag");
        const std::string atmosphere_shader =
            read_text_file(source_root / "shaders/ocean_atmosphere.glsl");
        const std::string init_shader =
            read_text_file(source_root / "shaders/ocean_spectrum_init.comp");
        const std::string evolve_shader =
            read_text_file(source_root / "shaders/ocean_spectrum_evolve.comp");
        const std::string fft_shader = read_text_file(source_root / "shaders/ocean_fft.comp");
        const std::string finalize_shader =
            read_text_file(source_root / "shaders/ocean_finalize.comp");
        const std::string detail_shader = read_text_file(source_root / "shaders/ocean_detail.comp");
        const std::string foam_update_shader =
            read_text_file(source_root / "shaders/ocean_foam_update.comp");
        require_contains(
            vertex_shader, "clipmap_patch_position",
            "ocean vertex shader should map generated vertices through clipmap patches");
        require_contains(vertex_shader, "clipmap_patch_alpha",
                         "ocean vertex shader should compute world-space clipmap ownership");
        require_contains(vertex_shader, "clipmap_transition_width",
                         "ocean vertex shader should share transition-width policy");
        require_contains(vertex_shader, "OCEAN_MESH_TRANSITION_CELLS",
                         "ocean vertex shader should name the transition cell policy");
        require_contains(vertex_shader, "patch_bounds",
                         "ocean vertex shader should receive clipmap patch bounds");
        require_contains(vertex_shader, "cascade_sample_position",
                         "ocean vertex shader should decorrelate cascade sampling");
        require_contains(vertex_shader, "displacement_near_texture",
                         "ocean vertex shader should sample the near displacement cascade");
        require_contains(vertex_shader, "cascade_patch_length",
                         "ocean vertex shader should sample cascaded ocean patch lengths");
        require_contains(vertex_shader, "cascade_geometry_detail_scale",
                         "ocean vertex shader should filter geometric detail displacement");
        require_contains(vertex_shader, "sample_detail_wave",
                         "ocean vertex shader should sample the detail wave field");
        require_contains(vertex_shader, "triangle_barycentric",
                         "ocean vertex shader should emit barycentric wireframe data");
        require_contains(vertex_shader, "noperspective layout(location = 5)",
                         "ocean vertex shader should keep wireframe interpolation screen-space");
        require_contains(vertex_shader, "ocean.cascade_options.w",
                         "ocean vertex shader should use configured seafloor depth");
        require_contains(fragment_shader, "cubey_pbr_apply_display_transform",
                         "ocean fragment shader should use the shared display transform");
        require_contains(fragment_shader, "scene_color_texture",
                         "ocean fragment shader should sample the scene color texture");
        require_contains(fragment_shader, "scene_depth_texture",
                         "ocean fragment shader should sample the scene depth texture");
        require_contains(fragment_shader, "scene_refraction_color",
                         "ocean fragment shader should derive refraction from scene data");
        require_contains(fragment_shader, "cascade_detail_filter",
                         "ocean fragment shader should filter detail by pixel footprint");
        require_contains(fragment_shader, "foam_breakup",
                         "ocean fragment shader should localize detail to foam breakup");
        require_contains(fragment_shader, "sun_glint",
                         "ocean fragment shader should include directional sun reflection");
        require_contains(fragment_shader, "OCEAN_VIEW_REFLECTION",
                         "ocean fragment shader should expose reflection debug view");
        require_contains(fragment_shader, "sample_foam_history",
                         "ocean fragment shader should sample persistent foam history");
        require_contains(fragment_shader, "sample_detail_wave",
                         "ocean fragment shader should sample detail wave field");
        require_contains(fragment_shader, "OCEAN_VIEW_DETAIL",
                         "ocean fragment shader should expose detail debug view");
        require_contains(fragment_shader, "wireframe_line",
                         "ocean fragment shader should expose a shader wireframe debug view");
        require_contains(fragment_shader, "wireframe_lod_tint",
                         "ocean fragment shader should tint wireframe by clipmap LOD level");
        require_contains(fragment_shader, "OCEAN_VIEW_THICKNESS",
                         "ocean fragment shader should expose water thickness debug view");
        require_contains(fragment_shader, "OCEAN_VIEW_TRANSMITTANCE",
                         "ocean fragment shader should expose transmittance debug view");
        require_contains(fragment_shader, "noperspective layout(location = 5)",
                         "ocean fragment shader should keep wireframe interpolation screen-space");
        require_contains(sky_shader, "camera_forward",
                         "ocean sky shader should reconstruct rays from camera basis");
        require_contains(scene_shader, "gl_FragDepth",
                         "ocean scene shader should write procedural seafloor depth");
        require_contains(scene_shader, "seafloor_color",
                         "ocean scene shader should shade the refracted seabed layer");
        require_contains(scene_shader, "scene.scene_options.x",
                         "ocean scene shader should receive seafloor depth from config");
        require_contains(atmosphere_shader, "ocean_sky_color",
                         "ocean atmosphere include should share sky color with water");
        require_contains(init_shader, "gaussian_pair",
                         "ocean spectrum init shader should seed a frequency-domain spectrum");
        require_contains(init_shader, "sea_state",
                         "ocean spectrum init shader should use sea state for spectral shape");
        require_contains(evolve_shader, "animation_speed",
                         "ocean spectrum evolve shader should use an independent animation speed");
        require_contains(evolve_shader, "height_dx_spectrum_image",
                         "ocean spectrum evolve shader should output packed derived fields");
        require_contains(evolve_shader, "slope_z_curvature_spectrum_image",
                         "ocean spectrum evolve shader should derive slope and crest fields");
        require_contains(fft_shader, "twiddle",
                         "ocean FFT shader should perform staged butterfly passes");
        require_contains(fft_shader, "load_packed_complex",
                         "ocean FFT shader should preserve packed complex field lanes");
        require_contains(finalize_shader, "normal_foam_image",
                         "ocean finalize shader should write normal and foam output");
        require_contains(finalize_shader, "jacobian",
                         "ocean finalize shader should derive crest compression");
        require_contains(detail_shader, "add_detail_wave",
                         "ocean detail shader should generate analytic short-wave bands");
        require_contains(detail_shader, "foam_coverage_mask",
                         "ocean detail shader should gate crest foam with patchy coverage");
        require_contains(detail_shader, "crest_sharpness",
                         "ocean detail shader should sharpen crest geometry");
        require_contains(detail_shader, "vec4(height * detail_geometry, slope.x, slope.y",
                         "ocean detail shader should write height, slope, and foam source");
        require_contains(foam_update_shader, "previous_foam_image",
                         "ocean foam update shader should read prior foam history");
        require_contains(foam_update_shader, "next_foam_image",
                         "ocean foam update shader should write ping-ponged foam history");
        require_contains(foam_update_shader, "detail_normal_foam_image",
                         "ocean foam update shader should read detail foam source");
        require_contains(foam_update_shader, "freshness",
                         "ocean foam update shader should preserve fresh foam state");
        require_contains(foam_update_shader, "foam_drift",
                         "ocean foam update shader should drift foam independently");

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ocean_config_tests: " << error.what() << '\n';
        return 1;
    }
}
