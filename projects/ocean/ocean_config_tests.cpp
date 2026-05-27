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
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Spectrum) ==
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
        require_contains(vertex_shader, "projected_grid_position",
                         "ocean vertex shader should use a camera-relative projected grid");
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
        require_contains(fragment_shader, "cubey_pbr_apply_display_transform",
                         "ocean fragment shader should use the shared display transform");
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
        require_contains(sky_shader, "camera_forward",
                         "ocean sky shader should reconstruct rays from camera basis");
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
