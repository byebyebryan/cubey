#include "ocean_ref_config.h"
#include "ocean_ref_mesh.h"

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
        namespace ocean_ref = cubey::projects::ocean_ref;

        const ocean_ref::OceanRefConfig defaults{};
        require(defaults.map_size == ocean_ref::kOceanRefDefaultMapSize,
                "ocean_ref should default to the reference 1024 map size");
        require(ocean_ref::ocean_ref_is_supported_map_size(128),
                "ocean_ref should support 128 maps for smoke tests");
        require(ocean_ref::ocean_ref_is_supported_map_size(256),
                "ocean_ref should support 256 maps");
        require(ocean_ref::ocean_ref_is_supported_map_size(512),
                "ocean_ref should support 512 maps");
        require(ocean_ref::ocean_ref_is_supported_map_size(1024),
                "ocean_ref should support 1024 maps");
        require(!ocean_ref::ocean_ref_is_supported_map_size(192),
                "ocean_ref should reject non-reference map sizes");
        require(defaults.mesh_cells >= ocean_ref::kOceanRefMinMeshCells &&
                    defaults.mesh_cells <= ocean_ref::kOceanRefMaxMeshCells,
                "default mesh resolution should be in supported range");
        require(defaults.mesh_lod_levels >= ocean_ref::kOceanRefMinMeshLodLevels &&
                    defaults.mesh_lod_levels <= ocean_ref::kOceanRefMaxMeshLodLevels,
                "default mesh LOD count should be in supported range");
        require(ocean_ref::ocean_ref_mesh_vertex_count(defaults) ==
                    defaults.mesh_cells * defaults.mesh_cells * 6U,
                "ocean_ref vertex count should match generated grid triangles");

        const ocean_ref::OceanRefMeshPatchList patches =
            ocean_ref::ocean_ref_mesh_clipmap_patches(defaults);
        require(patches.count == ocean_ref::ocean_ref_mesh_patch_count(defaults),
                "ocean_ref clipmap helper should emit all expected patches");
        require(patches.patches[0].level == defaults.mesh_lod_levels - 1U,
                "ocean_ref clipmap patches should be ordered far to near");
        require(patches.patches[patches.count - 1U].level == 0U,
                "ocean_ref clipmap should finish with the center patch");
        require(ocean_ref::ocean_ref_mesh_total_triangle_count(defaults) >
                    defaults.mesh_cells * defaults.mesh_cells * 2U,
                "ocean_ref clipmap should add coarser horizon geometry");

        const ocean_ref::OceanRefCascadeConfig& cascade0 = defaults.cascades[0];
        const ocean_ref::OceanRefCascadeConfig& cascade1 = defaults.cascades[1];
        const ocean_ref::OceanRefCascadeConfig& cascade2 = defaults.cascades[2];
        require_near(cascade0.tile_length, 88.0F, 0.001F,
                     "cascade 0 tile length should match Godot ref");
        require_near(cascade0.displacement_scale, 1.0F, 0.001F,
                     "cascade 0 displacement scale should match Godot ref");
        require_near(cascade0.wind_speed, 10.0F, 0.001F,
                     "cascade 0 wind speed should match Godot ref");
        require_near(cascade0.wind_direction_degrees, 20.0F, 0.001F,
                     "cascade 0 wind direction should match Godot ref");
        require_near(cascade0.fetch_length_km, 150.0F, 0.001F,
                     "cascade 0 fetch should match Godot ref");
        require_near(cascade0.spread, 0.2F, 0.001F, "cascade 0 spread should match Godot ref");
        require_near(cascade0.whitecap, 0.5F, 0.001F, "cascade 0 whitecap should match Godot ref");
        require_near(cascade0.foam_amount, 8.0F, 0.001F,
                     "cascade 0 foam amount should match Godot ref");

        require_near(cascade1.tile_length, 57.0F, 0.001F,
                     "cascade 1 tile length should match Godot ref");
        require_near(cascade1.displacement_scale, 0.75F, 0.001F,
                     "cascade 1 displacement scale should match Godot ref");
        require_near(cascade1.wind_speed, 5.0F, 0.001F,
                     "cascade 1 wind speed should match Godot ref");
        require_near(cascade1.wind_direction_degrees, 15.0F, 0.001F,
                     "cascade 1 wind direction should match Godot ref");
        require_near(cascade1.fetch_length_km, 150.0F, 0.001F,
                     "cascade 1 fetch should match Godot ref");
        require_near(cascade1.spread, 0.4F, 0.001F, "cascade 1 spread should match Godot ref");
        require_near(cascade1.whitecap, 0.5F, 0.001F, "cascade 1 whitecap should match Godot ref");
        require_near(cascade1.foam_amount, 0.0F, 0.001F,
                     "cascade 1 foam amount should match Godot ref");

        require_near(cascade2.tile_length, 16.0F, 0.001F,
                     "cascade 2 tile length should match Godot ref");
        require_near(cascade2.displacement_scale, 0.0F, 0.001F,
                     "cascade 2 geometry scale should match Godot ref");
        require_near(cascade2.normal_scale, 0.25F, 0.001F,
                     "cascade 2 normal scale should match Godot ref");
        require_near(cascade2.wind_speed, 20.0F, 0.001F,
                     "cascade 2 wind speed should match Godot ref");
        require_near(cascade2.fetch_length_km, 550.0F, 0.001F,
                     "cascade 2 fetch should match Godot ref");
        require_near(cascade2.whitecap, 0.25F, 0.001F, "cascade 2 whitecap should match Godot ref");
        require_near(cascade2.foam_amount, 3.0F, 0.001F,
                     "cascade 2 foam amount should match Godot ref");
        require_near(defaults.water_color_r, 0.1F, 0.001F, "water color should match Godot ref");
        require_near(defaults.foam_color_r, 0.73F, 0.001F, "foam color should match Godot ref");
        ocean_ref::validate_ocean_ref_config(defaults);

        require(ocean_ref::ocean_ref_render_view_from_name("") ==
                    ocean_ref::OceanRefRenderView::Final,
                "empty debug view should use final ocean_ref rendering");
        require(ocean_ref::ocean_ref_render_view_from_name("height") ==
                    ocean_ref::OceanRefRenderView::Height,
                "height debug view should parse");
        require(ocean_ref::ocean_ref_render_view_from_name("displacement") ==
                    ocean_ref::OceanRefRenderView::Displacement,
                "displacement debug view should parse");
        require(ocean_ref::ocean_ref_render_view_from_name("normal") ==
                    ocean_ref::OceanRefRenderView::Normal,
                "normal debug view should parse");
        require(ocean_ref::ocean_ref_render_view_from_name("foam") ==
                    ocean_ref::OceanRefRenderView::Foam,
                "foam debug view should parse");
        require(ocean_ref::next_ocean_ref_render_view(ocean_ref::OceanRefRenderView::Foam) ==
                    ocean_ref::OceanRefRenderView::Final,
                "ocean_ref debug view cycle should wrap");

        bool rejected = false;
        try {
            static_cast<void>(ocean_ref::ocean_ref_render_view_from_name("detail"));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean_ref should not expose the experimental detail debug view");

        cubey::RunConfig run_config;
        run_config.debug_view = "foam";
        run_config.ocean_ref.map_size = 128;
        run_config.pbr.exposure = 0.5F;
        const ocean_ref::OceanRefConfig from_run_config =
            ocean_ref::ocean_ref_config_from_run_config(run_config);
        require(from_run_config.render_view == ocean_ref::OceanRefRenderView::Foam,
                "run config should initialize ocean_ref debug view");
        require(from_run_config.map_size == 128U,
                "run config should initialize ocean_ref map size");
        require_near(from_run_config.exposure, 0.5F, 0.001F,
                     "run config should initialize ocean_ref exposure");

        const char* argv[] = {"ocean_ref", "--ocean-ref-map-size", "256", "--debug-view", "normal"};
        cubey::RunConfig parsed = cubey::parse_run_config(5, const_cast<char**>(argv));
        require(parsed.ocean_ref.map_size == 256U, "CLI parser should accept --ocean-ref-map-size");
        require(parsed.debug_view == "normal", "CLI parser should preserve debug view");

        ocean_ref::OceanRefConfig invalid_map = defaults;
        invalid_map.map_size = 192U;
        rejected = false;
        try {
            ocean_ref::validate_ocean_ref_config(invalid_map);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean_ref should reject unsupported map sizes");

        ocean_ref::OceanRefConfig invalid_cascade = defaults;
        invalid_cascade.cascades[0].tile_length = 0.0F;
        rejected = false;
        try {
            ocean_ref::validate_ocean_ref_config(invalid_cascade);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean_ref should reject invalid cascade dimensions");

        const std::filesystem::path source_root(CUBEY_OCEAN_REF_SOURCE_DIR);
        const std::string spectrum_shader =
            read_text_file(source_root / "shaders/ocean_ref_spectrum.comp");
        const std::string modulate_shader =
            read_text_file(source_root / "shaders/ocean_ref_modulate.comp");
        const std::string unpack_shader =
            read_text_file(source_root / "shaders/ocean_ref_unpack.comp");
        const std::string vertex_shader = read_text_file(source_root / "shaders/ocean_ref.vert");
        const std::string fragment_shader = read_text_file(source_root / "shaders/ocean_ref.frag");

        require_contains(spectrum_shader, "xy = h0(k), zw = conj(h0(-k))",
                         "spectrum shader should document reference h0 packing");
        require_contains(spectrum_shader, "conj_complex(get_spectrum_amplitude(id1, dims))",
                         "spectrum shader should pack conjugated negative frequency");
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
        require_contains(fragment_shader,
                         "gradient += normal_foam.xyw * vec3(normal_scale, normal_scale, 1.0)",
                         "fragment shader should preserve normal/foam map scale packing");
        require_contains(fragment_shader,
                         "smoothstep(0.0, 1.0, gradient.z * 0.75) * exp(-dist * 0.0075)",
                         "fragment shader should preserve reference foam attenuation");
        require_contains(fragment_shader, "mix(0.015, ocean.normal_scales.w, exp(-dist * 0.0175))",
                         "fragment shader should preserve reference normal fade");

        require_not_contains(vertex_shader, "ocean_macro_waves",
                             "ocean_ref vertex shader should not use Cubey macro waves");
        require_not_contains(fragment_shader, "ocean_macro_waves",
                             "ocean_ref fragment shader should not use Cubey macro waves");
        require_not_contains(unpack_shader, "bounded_choppy",
                             "ocean_ref unpack shader should not use bounded choppy clamps");
        require_not_contains(fragment_shader, "foam_history",
                             "ocean_ref fragment shader should not use separate foam history");

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ocean_ref_config_tests: " << error.what() << '\n';
        return 1;
    }
}
