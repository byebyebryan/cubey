#include "ocean_config.h"
#include "ocean_mesh.h"

#include <cubey/core/run_config.h>

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

void require_not_contains(const std::string& text, const std::string& needle, const char* message) {
    require(text.find(needle) == std::string::npos, message);
}

} // namespace

int main() {
    try {
        namespace ocean = cubey::projects::ocean;

        const ocean::OceanConfig defaults{};
        require(defaults.map_size == ocean::kOceanDefaultMapSize,
                "ocean should default to the reference 1024 map size");
        require(ocean::ocean_is_supported_map_size(128),
                "ocean should support 128 maps for smoke tests");
        require(ocean::ocean_is_supported_map_size(256),
                "ocean should support 256 maps");
        require(ocean::ocean_is_supported_map_size(512),
                "ocean should support 512 maps");
        require(ocean::ocean_is_supported_map_size(1024),
                "ocean should support 1024 maps");
        require(!ocean::ocean_is_supported_map_size(192),
                "ocean should reject non-reference map sizes");
        require(defaults.mesh_cells >= ocean::kOceanMinMeshCells &&
                    defaults.mesh_cells <= ocean::kOceanMaxMeshCells,
                "default mesh resolution should be in supported range");
        require(defaults.mesh_lod_levels >= ocean::kOceanMinMeshLodLevels &&
                    defaults.mesh_lod_levels <= ocean::kOceanMaxMeshLodLevels,
                "default mesh LOD count should be in supported range");
        require(ocean::ocean_mesh_vertex_count(defaults) ==
                    defaults.mesh_cells * defaults.mesh_cells * 6U,
                "ocean vertex count should match generated grid triangles");

        const ocean::OceanMeshPatchList patches =
            ocean::ocean_mesh_clipmap_patches(defaults);
        require(patches.count == ocean::ocean_mesh_patch_count(defaults),
                "ocean clipmap helper should emit all expected patches");
        require(patches.patches[0].level == defaults.mesh_lod_levels - 1U,
                "ocean clipmap patches should be ordered far to near");
        require(patches.patches[patches.count - 1U].level == 0U,
                "ocean clipmap should finish with the center patch");
        require(ocean::ocean_mesh_total_triangle_count(defaults) >
                    defaults.mesh_cells * defaults.mesh_cells * 2U,
                "ocean clipmap should add coarser horizon geometry");

        require(ocean::kOceanCascadeCount == 5U, "ocean should use five active cascades");
        require_near(defaults.sea_state.wind_speed, 10.0F, 0.001F,
                     "sea state wind speed should default to the reference primary wind");
        require_near(defaults.sea_state.wind_direction_degrees, 20.0F, 0.001F,
                     "sea state wind direction should default to the reference primary wind");
        require_near(defaults.sea_state.fetch_length_km, 150.0F, 0.001F,
                     "sea state fetch should default to the reference primary fetch");
        require_near(defaults.sea_state.spread, 0.2F, 0.001F,
                     "sea state spread should preserve the reference primary directionality");

        const ocean::OceanCascadeConfig& cascade0 = defaults.cascades[0];
        const ocean::OceanCascadeConfig& cascade1 = defaults.cascades[1];
        const ocean::OceanCascadeConfig& cascade2 = defaults.cascades[2];
        const ocean::OceanCascadeConfig& cascade3 = defaults.cascades[3];
        const ocean::OceanCascadeConfig& cascade4 = defaults.cascades[4];
        require_near(cascade0.tile_length, 512.0F, 0.001F,
                     "cascade 0 should be the macro tile");
        require_near(cascade0.min_wavelength, 224.0F, 0.001F,
                     "cascade 0 should start at macro wavelengths");
        require_near(cascade0.max_wavelength, 768.0F, 0.001F,
                     "cascade 0 should have an overlapping macro window");
        require_near(cascade0.displacement_scale, 0.25F, 0.001F,
                     "cascade 0 displacement scale should be restrained");

        require_near(cascade1.tile_length, 224.0F, 0.001F,
                     "cascade 1 should bridge macro and primary waves");
        require_near(cascade1.min_wavelength, 88.0F, 0.001F,
                     "cascade 1 should begin above the primary band");
        require_near(cascade1.max_wavelength, 320.0F, 0.001F,
                     "cascade 1 should have an overlapping long-wave window");
        require_near(cascade1.displacement_scale, 0.35F, 0.001F,
                     "cascade 1 displacement scale should stay below primary wave scales");

        require_near(cascade2.tile_length, 88.0F, 0.001F,
                     "cascade 2 should retain the reference primary tile length");
        require_near(cascade2.min_wavelength, 12.0F, 0.001F,
                     "cascade 2 should keep short wavelengths for sharp crests");
        require_near(cascade2.max_wavelength, 128.0F, 0.001F,
                     "cascade 2 should overlap the long bridge band");
        require_near(cascade2.displacement_scale, 1.0F, 0.001F,
                     "cascade 2 should preserve reference primary displacement");
        require_near(cascade2.foam_amount, 8.0F, 0.001F,
                     "cascade 2 should preserve reference primary foam");

        require_near(cascade3.tile_length, 57.0F, 0.001F,
                     "cascade 3 should preserve the reference secondary tile length");
        require_near(cascade3.min_wavelength, 6.0F, 0.001F,
                     "cascade 3 should keep short wavelengths for crest interference");
        require_near(cascade3.max_wavelength, 88.0F, 0.001F,
                     "cascade 3 should overlap the primary band");
        require_near(cascade3.displacement_scale, 0.75F, 0.001F,
                     "cascade 3 should preserve reference secondary displacement");

        require_near(cascade4.tile_length, 16.0F, 0.001F,
                     "cascade 4 should retain the reference detail tile length");
        require_near(cascade4.min_wavelength, 1.5F, 0.001F,
                     "cascade 4 should cover short detail wavelengths");
        require_near(cascade4.max_wavelength, 32.0F, 0.001F,
                     "cascade 4 should overlap primary crest detail");
        require_near(cascade4.displacement_scale, 0.0F, 0.001F,
                     "cascade 4 should remain normal-only detail");
        require_near(cascade4.normal_scale, 0.25F, 0.001F,
                     "cascade 4 normal scale should match reference detail");
        require_near(cascade4.foam_amount, 3.0F, 0.001F,
                     "cascade 4 foam amount should match reference detail");
        require_near(defaults.water_color_r, 0.1F, 0.001F, "water color should match Godot ref");
        require_near(defaults.foam_color_r, 0.73F, 0.001F, "foam color should match Godot ref");
        ocean::validate_ocean_config(defaults);

        require(ocean::ocean_render_view_from_name("") ==
                    ocean::OceanRenderView::Final,
                "empty debug view should use final ocean rendering");
        require(ocean::ocean_render_view_from_name("height") ==
                    ocean::OceanRenderView::Height,
                "height debug view should parse");
        require(ocean::ocean_render_view_from_name("displacement") ==
                    ocean::OceanRenderView::Displacement,
                "displacement debug view should parse");
        require(ocean::ocean_render_view_from_name("normal") ==
                    ocean::OceanRenderView::Normal,
                "normal debug view should parse");
        require(ocean::ocean_render_view_from_name("foam") ==
                    ocean::OceanRenderView::Foam,
                "foam debug view should parse");
        require(ocean::ocean_render_view_from_name("lod") ==
                    ocean::OceanRenderView::Lod,
                "lod debug view should parse");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Foam) ==
                    ocean::OceanRenderView::Lod,
                "ocean debug view cycle should include the LOD view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Lod) ==
                    ocean::OceanRenderView::Final,
                "ocean debug view cycle should wrap");

        bool rejected = false;
        try {
            static_cast<void>(ocean::ocean_render_view_from_name("detail"));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should not expose the experimental detail debug view");

        cubey::RunConfig run_config;
        run_config.debug_view = "foam";
        run_config.ocean.map_size = 128;
        run_config.pbr.exposure = 0.5F;
        const ocean::OceanConfig from_run_config =
            ocean::ocean_config_from_run_config(run_config);
        require(from_run_config.render_view == ocean::OceanRenderView::Foam,
                "run config should initialize ocean debug view");
        require(from_run_config.map_size == 128U,
                "run config should initialize ocean map size");
        require_near(from_run_config.exposure, 0.5F, 0.001F,
                     "run config should initialize ocean exposure");

        const char* argv[] = {"ocean",
                              "--ocean-map-size",
                              "256",
                              "--ocean-cascade",
                              "4",
                              "--debug-view",
                              "normal",
                              "--ocean-wire-overlay",
                              "--ocean-wire-opacity",
                              "0.8"};
        cubey::RunConfig parsed = cubey::parse_run_config(10, const_cast<char**>(argv));
        require(parsed.ocean.map_size == 256U, "CLI parser should accept --ocean-map-size");
        require(parsed.ocean.cascade == 4, "CLI parser should accept --ocean-cascade");
        require(parsed.debug_view == "normal", "CLI parser should preserve debug view");
        require(parsed.ocean.wire_overlay, "CLI parser should accept ocean wire overlay");
        require_near(parsed.ocean.wire_opacity, 0.8F, 0.001F,
                     "CLI parser should accept ocean wire opacity");

        const char* all_cascade_argv[] = {"ocean", "--ocean-cascade", "all"};
        parsed = cubey::parse_run_config(3, const_cast<char**>(all_cascade_argv));
        require(parsed.ocean.cascade == -1, "CLI parser should accept all ocean cascades");

        ocean::OceanConfig invalid_map = defaults;
        invalid_map.map_size = 192U;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_map);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject unsupported map sizes");

        ocean::OceanConfig invalid_cascade = defaults;
        invalid_cascade.cascades[0].tile_length = 0.0F;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_cascade);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject invalid cascade dimensions");

        const std::filesystem::path source_root(CUBEY_OCEAN_SOURCE_DIR);
        const std::string spectrum_shader =
            read_text_file(source_root / "shaders/ocean_spectrum.comp");
        const std::string modulate_shader =
            read_text_file(source_root / "shaders/ocean_modulate.comp");
        const std::string unpack_shader =
            read_text_file(source_root / "shaders/ocean_unpack.comp");
        const std::string vertex_shader = read_text_file(source_root / "shaders/ocean.vert");
        const std::string fragment_shader = read_text_file(source_root / "shaders/ocean.frag");
        const std::string gpu_resources_source =
            read_text_file(source_root / "ocean_gpu_resources.cpp");

        require_contains(spectrum_shader, "xy = h0(k), zw = conj(h0(-k))",
                         "spectrum shader should document reference h0 packing");
        require_contains(spectrum_shader, "conj_complex(get_spectrum_amplitude(id1, dims))",
                         "spectrum shader should pack conjugated negative frequency");
        require_contains(spectrum_shader, "wavelength_band_weight(wavelength)",
                         "spectrum shader should band-limit each active cascade");
        require_contains(modulate_shader, "const uint NUM_SPECTRA = 4U",
                         "modulate shader should preserve four reference spectra");
        require_contains(modulate_shader, "vec2 dhy_dx = h_inv * k_vec.y;",
                         "modulate shader should preserve swapped derivative axis");
        require_contains(modulate_shader, "vec2 dhx_dx = -h * k_vec.y * k_unit.y;",
                         "modulate shader should preserve reference horizontal derivative");
        require_contains(modulate_shader,
                         "imageStore(fft_field0_image, id, vec4(hx.x - hy.y, hx.y + hy.x",
                         "modulate shader should preserve layer 0 packing");
        require_contains(modulate_shader,
                         "imageStore(fft_field3_image, id, vec4(dhz_dz.x - dhz_dx.y",
                         "modulate shader should preserve layer 3 packing");
        require_contains(unpack_shader,
                         "float sign_shift = -2.0 * float((id.x & 1) ^ (id.y & 1)) + 1.0;",
                         "unpack shader should apply checkerboard ifft shift");
        require_contains(unpack_shader,
                         "float jacobian = (1.0 + dhx_dx) * (1.0 + dhz_dz) - dhz_dx * dhz_dx;",
                         "unpack shader should preserve Jacobian foam source");
        require_contains(
            unpack_shader,
            "vec2 gradient = vec2(dhy_dx, dhy_dz) / (1.0 + abs(vec2(dhx_dx, dhz_dz)));",
            "unpack shader should preserve reference gradient denominator");
        require_contains(vertex_shader,
                         "distance_factor = min(exp(-(camera_distance - 150.0) * 0.007), 1.0)",
                         "vertex shader should preserve reference distance falloff");
        require_contains(vertex_shader, "const uint OCEAN_CASCADE_COUNT = 5u",
                         "vertex shader should sample all active cascades");
        require_contains(vertex_shader, "texture(displacement_cascade4_texture, uv)",
                         "vertex shader should sample the fifth cascade displacement");
        require_contains(vertex_shader, "if (!ocean_cascade_enabled(cascade))",
                         "vertex shader should gate displacement by inspected cascade");
        require_contains(fragment_shader,
                         "gradient += normal_foam.xyw * vec3(normal_scale, normal_scale, 1.0)",
                         "fragment shader should preserve normal/foam map scale packing");
        require_contains(fragment_shader, "texture(normal_foam_cascade4_texture, uv)",
                         "fragment shader should sample the fifth cascade normal/foam map");
        require_contains(fragment_shader, "if (!ocean_cascade_enabled(cascade))",
                         "fragment shader should gate normal and foam by inspected cascade");
        require_contains(fragment_shader,
                         "smoothstep(0.0, 1.0, gradient.z * 0.75) * exp(-dist * 0.0075)",
                         "fragment shader should preserve reference foam attenuation");
        require_contains(fragment_shader,
                         "mix(0.015, ocean.inspection_options.z, exp(-dist * 0.0175))",
                         "fragment shader should preserve reference normal fade");
        require_contains(vertex_shader, "triangle_barycentric(vertex_in_cell)",
                         "vertex shader should feed wireframe diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_LOD = 5u",
                         "fragment shader should expose LOD diagnostics");
        require_contains(fragment_shader, "triangle_wire_factor(frag_barycentric)",
                         "fragment shader should expose wire diagnostics");
        require_contains(gpu_resources_source, ".size = sizeof(float) * 64U",
                         "surface pipeline push constants should match ocean shader layout");

        require_not_contains(vertex_shader, "ocean_macro_waves",
                             "ocean vertex shader should not use Cubey macro waves");
        require_not_contains(fragment_shader, "ocean_macro_waves",
                             "ocean fragment shader should not use Cubey macro waves");
        require_not_contains(unpack_shader, "bounded_choppy",
                             "ocean unpack shader should not use bounded choppy clamps");
        require_not_contains(fragment_shader, "foam_history",
                             "ocean fragment shader should not use separate foam history");

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ocean_config_tests: " << error.what() << '\n';
        return 1;
    }
}
