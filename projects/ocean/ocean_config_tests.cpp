#include "ocean_config.h"
#include "ocean_mesh.h"
#include "ocean_ui.h"

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
        require(defaults.spectral_domains_enabled,
                "ocean should default to spectral source-domain filtering");
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

        const ocean::OceanCascadeConfig& cascade0 = defaults.cascades[0];
        const ocean::OceanCascadeConfig& cascade1 = defaults.cascades[1];
        const ocean::OceanCascadeConfig& cascade2 = defaults.cascades[2];
        const ocean::OceanCascadeConfig& cascade3 = defaults.cascades[3];
        const ocean::OceanCascadeConfig& cascade4 = defaults.cascades[4];
        const ocean::OceanCascadeDomain domain0 = ocean::ocean_cascade_domain(defaults, 0);
        const ocean::OceanCascadeDomain domain1 = ocean::ocean_cascade_domain(defaults, 1);
        const ocean::OceanCascadeDomain domain2 = ocean::ocean_cascade_domain(defaults, 2);
        const ocean::OceanCascadeDomain domain3 = ocean::ocean_cascade_domain(defaults, 3);
        const ocean::OceanCascadeDomain domain4 = ocean::ocean_cascade_domain(defaults, 4);
        require(ocean::kOceanMacroCascadeCount == 2U,
                "ocean should place two macro cascades first");
        require(ocean::kOceanReferenceCascadeCount == 3U,
                "ocean should keep three reference-derived cascades");
        require(domain0.active && domain1.active && domain2.active && domain3.active &&
                    domain4.active,
                "default cascade wavelength domains should all be active");
        require(domain0.low_k < domain0.high_k && domain1.low_k < domain1.high_k &&
                    domain2.low_k < domain2.high_k && domain3.low_k < domain3.high_k &&
                    domain4.low_k < domain4.high_k,
                "default cascade domains should have increasing k bounds");
        require(domain0.low_wavelength < domain0.high_wavelength &&
                    domain1.low_wavelength < domain1.high_wavelength &&
                    domain2.low_wavelength < domain2.high_wavelength &&
                    domain3.low_wavelength < domain3.high_wavelength &&
                    domain4.low_wavelength < domain4.high_wavelength,
                "default cascade domains should expose low/high wavelength bounds");
        require(domain0.high_wavelength > domain1.high_wavelength &&
                    domain1.high_wavelength > domain2.high_wavelength &&
                    domain2.high_wavelength > domain3.high_wavelength &&
                    domain3.high_wavelength > domain4.high_wavelength,
                "cascade diagnostic domains should run from macro to detail wavelengths");
        require_near(domain0.high_wavelength,
                     cascade0.tile_length / ocean::kOceanCascadeMinWavesPerDomain, 0.01F,
                     "cascade 0 largest wavelength should follow min waves per domain");
        require_near(domain0.low_wavelength,
                     cascade0.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 0 smallest wavelength should follow map sampling");
        require_near(domain1.high_wavelength,
                     cascade1.tile_length / ocean::kOceanCascadeMinWavesPerDomain, 0.01F,
                     "cascade 1 largest wavelength should follow min waves per domain");
        require_near(domain1.low_wavelength,
                     cascade1.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 1 smallest wavelength should follow map sampling");
        require_near(domain2.high_wavelength,
                     cascade2.tile_length / ocean::kOceanCascadeMinWavesPerDomain, 0.01F,
                     "cascade 2 largest wavelength should follow min waves per domain");
        require_near(domain2.low_wavelength,
                     cascade2.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 2 smallest wavelength should follow map sampling");
        require_near(domain3.high_wavelength,
                     cascade3.tile_length / ocean::kOceanCascadeMinWavesPerDomain, 0.01F,
                     "cascade 3 largest wavelength should follow min waves per domain");
        require_near(domain3.low_wavelength,
                     cascade3.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 3 smallest wavelength should follow map sampling");
        require_near(domain4.high_wavelength,
                     cascade4.tile_length / ocean::kOceanCascadeMinWavesPerDomain, 0.01F,
                     "cascade 4 largest wavelength should follow min waves per domain");
        require_near(domain4.low_wavelength,
                     cascade4.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 4 smallest wavelength should follow map sampling");
        require_near(cascade0.tile_length, 1531.0F, 0.001F,
                     "cascade 0 should be the largest decorrelated macro swell");
        require_near(cascade1.tile_length, 421.0F, 0.001F,
                     "cascade 1 should be the decorrelated mid macro layer");
        require_near(cascade0.displacement_scale, 0.55F, 0.001F,
                     "cascade 0 storm macro displacement should be tuned");
        require_near(cascade1.displacement_scale, 0.95F, 0.001F,
                     "cascade 1 storm macro displacement should be tuned");
        require_near(cascade0.wind_speed, 32.0F, 0.001F,
                     "cascade 0 should carry storm swell wind");
        require_near(cascade1.wind_speed, 30.0F, 0.001F,
                     "cascade 1 should carry storm chop wind");
        require(cascade0.tile_length > cascade1.tile_length &&
                    cascade1.tile_length > cascade2.tile_length,
                "cascades should run from macro scale into crest scale");
        require(cascade0.displacement_scale < cascade1.displacement_scale &&
                    cascade1.displacement_scale < cascade2.displacement_scale,
                "macro cascades should stay lower amplitude than the primary crest");
        require(cascade0.normal_scale > 0.0F && cascade0.normal_scale < cascade1.normal_scale &&
                    cascade1.normal_scale < cascade2.normal_scale,
                "macro cascades should feed lower-amplitude normals than the crest");
        require_near(cascade0.whitecap, 0.12F, 0.001F,
                     "cascade 0 macro swell should keep its whitecap threshold");
        require_near(cascade0.foam_amount, 0.0F, 0.001F,
                     "cascade 0 macro swell should not generate foam by default");
        require_near(cascade1.whitecap, 0.18F, 0.001F,
                     "cascade 1 macro chop should only feed strong breaking source");
        require_near(cascade1.foam_amount, 0.50F, 0.001F,
                     "cascade 1 macro chop should feed low foam by default");

        require_near(cascade2.tile_length, 88.0F, 0.001F,
                     "cascade 2 should keep the reference primary crest scale");
        require_near(cascade2.displacement_scale, 1.35F, 0.001F,
                     "cascade 2 storm primary crest displacement should be tuned");
        require_near(cascade2.normal_scale, 1.35F, 0.001F,
                     "cascade 2 storm primary crest normal should be tuned");
        require_near(cascade2.wind_speed, 18.0F, 0.001F,
                     "cascade 2 storm primary crest wind should be tuned");
        require_near(cascade2.wind_direction_degrees, 20.0F, 0.001F,
                     "cascade 2 wind direction should match Godot ref primary crest");
        require_near(cascade2.fetch_length_km, 350.0F, 0.001F,
                     "cascade 2 storm primary crest fetch should be tuned");
        require_near(cascade2.spread, 0.14F, 0.001F,
                     "cascade 2 storm primary crest spread should be tuned");
        require_near(cascade2.whitecap, 0.46F, 0.001F,
                     "cascade 2 storm primary crest whitecap should drive main breaking");
        require_near(cascade2.foam_amount, 3.20F, 0.001F,
                     "cascade 2 storm primary crest foam should stay sharp");

        require_near(cascade3.tile_length, 57.0F, 0.001F,
                     "cascade 3 tile length should match Godot ref secondary wave");
        require_near(cascade3.displacement_scale, 1.08F, 0.001F,
                     "cascade 3 storm secondary wave displacement should be tuned");
        require_near(cascade3.normal_scale, 1.35F, 0.001F,
                     "cascade 3 storm secondary wave normal should be tuned");
        require_near(cascade3.wind_speed, 16.0F, 0.001F,
                     "cascade 3 storm secondary wave wind should be tuned");
        require_near(cascade3.wind_direction_degrees, 17.0F, 0.001F,
                     "cascade 3 storm secondary wave direction should be tuned");
        require_near(cascade3.fetch_length_km, 330.0F, 0.001F,
                     "cascade 3 storm secondary wave fetch should be tuned");
        require_near(cascade3.spread, 0.25F, 0.001F,
                     "cascade 3 storm secondary wave spread should be tuned");
        require_near(cascade3.whitecap, 0.44F, 0.001F,
                     "cascade 3 storm secondary wave whitecap should drive secondary breaking");
        require_near(cascade3.foam_amount, 2.80F, 0.001F,
                     "cascade 3 storm secondary wave foam should stay sharp");

        require_near(cascade4.tile_length, 16.0F, 0.001F,
                     "cascade 4 tile length should match Godot ref detail normals");
        require_near(cascade4.displacement_scale, 0.0F, 0.001F,
                     "cascade 4 geometry scale should match Godot ref detail normals");
        require_near(cascade4.normal_scale, 0.50F, 0.001F,
                     "cascade 4 storm detail normal should be tuned");
        require_near(cascade4.wind_speed, 30.0F, 0.001F,
                     "cascade 4 storm detail wind should be tuned");
        require_near(cascade4.fetch_length_km, 850.0F, 0.001F,
                     "cascade 4 storm detail fetch should be tuned");
        require_near(cascade4.whitecap, 0.32F, 0.001F,
                     "cascade 4 storm detail whitecap should feed fine breakup");
        require_near(cascade4.foam_amount, 1.60F, 0.001F,
                     "cascade 4 storm detail foam should stay secondary");
        require_near(defaults.water_color_r, 0.1F, 0.001F, "water color should match Godot ref");
        require_near(defaults.foam_color_r, 0.73F, 0.001F, "foam color should match Godot ref");
        require_near(defaults.foam_density, 2.80F, 0.001F,
                     "foam density should default to a visible crest coverage response");
        require_near(defaults.foam_sharpness, 0.50F, 0.001F,
                     "foam sharpness should default to a whitecap-biased response");
        const ocean::OceanDiagnosticsConfig diagnostics{};
        require_near(diagnostics.anti_repeat_strength, 1.0F, 0.001F,
                     "ocean diagnostics should default to anti-repeat sampling");
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
        require(ocean::ocean_render_view_from_name("foam-source") ==
                    ocean::OceanRenderView::FoamSource,
                "foam source debug view should parse");
        require(ocean::ocean_render_view_from_name("lod") ==
                    ocean::OceanRenderView::Lod,
                "lod debug view should parse");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Foam) ==
                    ocean::OceanRenderView::FoamSource,
                "ocean debug view cycle should include the foam source view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamSource) ==
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
        run_config.ocean.spectral_domains = 0;
        run_config.pbr.exposure = 0.5F;
        const ocean::OceanConfig from_run_config =
            ocean::ocean_config_from_run_config(run_config);
        require(from_run_config.render_view == ocean::OceanRenderView::Foam,
                "run config should initialize ocean debug view");
        require(from_run_config.map_size == 128U,
                "run config should initialize ocean map size");
        require(!from_run_config.spectral_domains_enabled,
                "run config should initialize ocean spectral domain override");
        require_near(from_run_config.exposure, 0.5F, 0.001F,
                     "run config should initialize ocean exposure");

        const char* argv[] = {"ocean",
                              "--ocean-map-size",
                              "256",
                              "--no-ocean-spectral-domains",
                              "--ocean-cascade",
                              "4",
                              "--debug-view",
                              "normal",
                              "--ocean-wire-overlay",
                              "--ocean-wire-opacity",
                              "0.8"};
        cubey::RunConfig parsed = cubey::parse_run_config(11, const_cast<char**>(argv));
        require(parsed.ocean.map_size == 256U, "CLI parser should accept --ocean-map-size");
        require(parsed.ocean.spectral_domains == 0,
                "CLI parser should accept --no-ocean-spectral-domains");
        require(parsed.ocean.cascade == 4, "CLI parser should accept --ocean-cascade");
        require(parsed.debug_view == "normal", "CLI parser should preserve debug view");
        require(parsed.ocean.wire_overlay, "CLI parser should accept ocean wire overlay");
        require_near(parsed.ocean.wire_opacity, 0.8F, 0.001F,
                     "CLI parser should accept ocean wire opacity");

        const char* all_cascade_argv[] = {"ocean", "--ocean-cascade", "all"};
        parsed = cubey::parse_run_config(3, const_cast<char**>(all_cascade_argv));
        require(parsed.ocean.cascade == -1, "CLI parser should accept all ocean cascades");

        const char* spectral_domains_argv[] = {"ocean", "--ocean-spectral-domains"};
        parsed = cubey::parse_run_config(2, const_cast<char**>(spectral_domains_argv));
        require(parsed.ocean.spectral_domains == 1,
                "CLI parser should accept --ocean-spectral-domains");

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
        const std::string app_source = read_text_file(source_root / "ocean_app.cpp");
        const std::string ui_source = read_text_file(source_root / "ocean_ui.cpp");
        const std::string gpu_resources_source =
            read_text_file(source_root / "ocean_gpu_resources.cpp");

        require_contains(spectrum_shader, "xy = h0(k), zw = conj(h0(-k))",
                         "spectrum shader should document reference h0 packing");
        require_contains(spectrum_shader, "conj_complex(get_spectrum_amplitude(id1, dims))",
                         "spectrum shader should pack conjugated negative frequency");
        require_contains(spectrum_shader, "float spectral_domain_weight",
                         "spectrum shader should apply spectral source-domain filtering");
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
                         "float jacobian = jxx * jzz - jxz * jxz;",
                         "unpack shader should preserve Jacobian foam source");
        require_contains(unpack_shader, "float compression = 0.5 * (jxx + jzz)",
                         "unpack shader should derive compression foam source");
        require_contains(unpack_shader, "persistent = max(current_source, persistent)",
                         "unpack shader should use max source/history foam accumulation");
        require_contains(
            unpack_shader,
            "vec2 gradient = vec2(dhy_dx, dhy_dz) / (1.0 + abs(vec2(dhx_dx, dhz_dz)));",
            "unpack shader should preserve reference gradient denominator");
        require_contains(unpack_shader, "imageLoad(foam_image, id).x",
                         "unpack shader should keep persistent foam in a separate texture");
        require_contains(unpack_shader, "imageStore(normal_image, id, vec4(gradient",
                         "unpack shader should write normal data separately from foam");
        require_contains(unpack_shader,
                         "imageStore(foam_image, id, vec4(persistent, current_source",
                         "unpack shader should write persistent and current foam separately");
        require_contains(vertex_shader, "float cascade_displacement_lod_weight",
                         "vertex shader should apply per-cascade displacement LOD weights");
        require_contains(vertex_shader, "float horizon_displacement_weight",
                         "vertex shader should fade displacement only near the horizon");
        require_contains(vertex_shader, "if (!ocean_cascade_enabled(cascade))",
                         "vertex shader should gate displacement by inspected cascade");
        require_contains(vertex_shader, "for (uint cascade = 0u; cascade < 5u; ++cascade)",
                         "vertex shader should include all regular displacement cascades");
        require_contains(vertex_shader, "cascade < 2u && ocean.inspection_options.y > 0.0",
                         "vertex shader should gate macro anti-repeat to macro cascades");
        require_contains(vertex_shader, "sample_ocean_displacement(cascade, position, tile_length)",
                         "vertex shader should apply macro anti-repeat displacement sampling");
        require_contains(app_source, "diagnostics_.anti_repeat_strength",
                         "app should pass anti-repeat as diagnostics push data");
        require_contains(app_source, "ocean_config_.foam_density",
                         "app should pass foam density as diagnostics push data");
        require_contains(app_source, "ocean_config_.foam_sharpness",
                         "app should pass foam sharpness as diagnostics push data");
        require_contains(app_source, "ocean_config_.spectral_domains_enabled",
                         "app should pass spectral domain bounds to spectrum generation");
        require_contains(ui_source, "&ui.diagnostics.anti_repeat_strength",
                         "UI should expose anti-repeat as a diagnostics control");
        require_contains(ui_source, "&ui.config.spectral_domains_enabled",
                         "UI should expose spectral domain filtering");
        require_contains(ui_source, "&ui.config.foam_density",
                         "UI should expose foam density");
        require_contains(ui_source, "&ui.config.foam_sharpness",
                         "UI should expose foam sharpness");
        require_contains(ui_source, "ocean_cascade_domain(ui.config, index)",
                         "UI should expose cascade wavelength domain diagnostics");
        require_contains(ui_source, "domain %.2f-%.2f m",
                         "UI should show cascade diagnostic wavelength bands");
        require_contains(fragment_shader,
                         "gradient += normal_foam * vec4(normal_scale, normal_scale, 1.0, 1.0)",
                         "fragment shader should preserve normal/foam map scale packing");
        require_contains(fragment_shader, "if (!ocean_cascade_enabled(cascade))",
                         "fragment shader should gate normal and foam by inspected cascade");
        require_contains(fragment_shader, "for (uint cascade = 0u; cascade < 5u; ++cascade)",
                         "fragment shader should include all regular normal/foam cascades");
        require_contains(fragment_shader, "cascade >= 1u && factor > 0.0",
                         "fragment shader should gate far anti-repeat to foamy cascades");
        require_contains(fragment_shader, "value_noise(position * 0.0011",
                         "fragment shader should use stable world-space noise weights");
        require_contains(fragment_shader, "float foam_breakup_weight",
                         "fragment shader should use distance-gated world-space foam breakup");
        require_contains(fragment_shader,
                         "sample_normal_foam_domain(cascade, position, tile_length, pixels_per_meter",
                         "fragment shader should sample secondary normal/foam domains");
        require_contains(fragment_shader, "vec2 foam = vec2(1.0)",
                         "fragment shader should combine persistent and current foam with a soft union");
        require_contains(fragment_shader, "OCEAN_FAR_ANTI_REPEAT_START",
                         "fragment shader should distance-gate far anti-repeat");
        require_contains(fragment_shader, "float map_size = ocean.cascade4_options.w;",
                         "fragment shader should read map size from packed cascade controls");
        require_contains(fragment_shader, "float cascade_surface_lod_weight",
                         "fragment shader should apply per-cascade normal and foam LOD weights");
        require_contains(fragment_shader, "mix(0.015, ocean.foam_color.w, exp(-dist * 0.0175))",
                         "fragment shader should preserve reference normal fade");
        require_contains(fragment_shader, "float ocean_material_distance_factor(float dist)",
                         "fragment shader should distance-filter material response");
        require_contains(fragment_shader, "float ocean_foam_signal(float persistent, float current)",
                         "fragment shader should mix current and persistent foam");
        require_contains(fragment_shader,
                         "float ocean_foam_coverage(float persistent, float current",
                         "fragment shader should derive presentation foam coverage from foam data");
        require_contains(fragment_shader, "vec3 ocean_shaded_foam(",
                         "fragment shader should shade foam as a material");
        require_contains(fragment_shader, "specular *= mix(1.0, 0.35, material_distance)",
                         "fragment shader should reduce far and foam-covered specular");
        require_contains(fragment_shader, "float ocean_horizon_fog_factor(vec3 view_dir, float dist)",
                         "fragment shader should use view-angle-aware horizon haze");
        require_contains(fragment_shader, "color = vec3(foam_coverage);",
                         "fragment shader should keep debug foam view as presentation coverage");
        require_contains(fragment_shader, "color = vec3(foam_current);",
                         "fragment shader should expose current foam source debug view");
        require_contains(vertex_shader, "triangle_barycentric(vertex_in_cell)",
                         "vertex shader should feed wireframe diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_FOAM_SOURCE = 5u",
                         "fragment shader should expose foam source diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_LOD = 6u",
                         "fragment shader should expose LOD diagnostics");
        require_contains(fragment_shader, "triangle_wire_factor(frag_barycentric)",
                         "fragment shader should expose wire diagnostics");
        require_contains(gpu_resources_source, ".size = sizeof(float) * 64U",
                         "surface pipeline push constants should match ocean shader layout");
        require_contains(gpu_resources_source, "kOceanCascadeCount * 3U",
                         "surface layout should bind displacement, normal, and foam for every cascade");
        require_contains(gpu_resources_source, "cascade + kOceanCascadeCount",
                         "surface descriptors should expose normal maps for every cascade");
        require_contains(gpu_resources_source, "cascade + kOceanCascadeCount * 2U",
                         "surface descriptors should expose foam maps for every cascade");

        require_not_contains(vertex_shader, "ocean_macro_waves",
                             "ocean vertex shader should not use Cubey macro waves");
        require_not_contains(fragment_shader, "ocean_macro_waves",
                             "ocean fragment shader should not use Cubey macro waves");
        require_not_contains(unpack_shader, "bounded_choppy",
                             "ocean unpack shader should not use bounded choppy clamps");
        require_not_contains(fragment_shader, "normal_foam_cascade",
                             "ocean fragment shader should not pack normals and foam in one sampler");

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ocean_config_tests: " << error.what() << '\n';
        return 1;
    }
}
