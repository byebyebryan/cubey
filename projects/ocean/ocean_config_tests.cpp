#include "ocean_config.h"
#include "ocean_mesh.h"
#include "ocean_ui.h"

#include <cubey/core/run_config.h>
#include <cubey/engine/atmosphere_environment_config.h>

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
        require(ocean::ocean_is_supported_map_size(256), "ocean should support 256 maps");
        require(ocean::ocean_is_supported_map_size(512), "ocean should support 512 maps");
        require(ocean::ocean_is_supported_map_size(1024), "ocean should support 1024 maps");
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

        const ocean::OceanMeshPatchList patches = ocean::ocean_mesh_clipmap_patches(defaults);
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
        require(domain0.active && domain1.active && !domain2.active && !domain3.active &&
                    domain4.active,
                "default cascade wavelength domains should leave crest carriers unfiltered");
        require(domain0.low_k < domain0.high_k && domain1.low_k < domain1.high_k &&
                    domain4.low_k < domain4.high_k,
                "default cascade domains should have increasing k bounds");
        require(domain0.low_wavelength < domain0.high_wavelength &&
                    domain1.low_wavelength < domain1.high_wavelength &&
                    domain4.low_wavelength < domain4.high_wavelength,
                "default cascade domains should expose low/high wavelength bounds");
        require(domain0.high_wavelength > domain1.high_wavelength &&
                    domain1.high_wavelength > domain4.high_wavelength,
                "active cascade diagnostic domains should run from macro to detail wavelengths");
        require_near(ocean::ocean_cascade_min_waves_per_domain(0), 3.0F, 0.001F,
                     "cascade 0 should keep a conservative macro spectral low cutoff");
        require_near(ocean::ocean_cascade_min_waves_per_domain(1), 2.0F, 0.001F,
                     "cascade 1 should keep more long macro chop wavelengths");
        require_near(ocean::ocean_cascade_min_waves_per_domain(2), 0.0F, 0.001F,
                     "cascade 2 should preserve the primary coherent whitecap carrier");
        require_near(ocean::ocean_cascade_min_waves_per_domain(3), 0.0F, 0.001F,
                     "cascade 3 should support but not dominate the crest carrier");
        require_near(ocean::ocean_cascade_min_waves_per_domain(4), 3.0F, 0.001F,
                     "cascade 4 should stay detail-biased");
        require_near(domain0.high_wavelength,
                     cascade0.tile_length / ocean::ocean_cascade_min_waves_per_domain(0), 0.01F,
                     "cascade 0 largest wavelength should follow its min waves per domain");
        require_near(domain0.low_wavelength,
                     cascade0.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 0 smallest wavelength should follow map sampling");
        require_near(domain1.high_wavelength,
                     cascade1.tile_length / ocean::ocean_cascade_min_waves_per_domain(1), 0.01F,
                     "cascade 1 largest wavelength should follow its min waves per domain");
        require_near(domain1.low_wavelength,
                     cascade1.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 1 smallest wavelength should follow map sampling");
        require(!domain2.active && domain2.low_k == 0.0F && domain2.high_k == 0.0F,
                "cascade 2 should disable spectral filtering for coherent whitecaps");
        require(!domain3.active && domain3.low_k == 0.0F && domain3.high_k == 0.0F,
                "cascade 3 should disable spectral filtering for coherent whitecaps");
        require_near(domain4.high_wavelength,
                     cascade4.tile_length / ocean::ocean_cascade_min_waves_per_domain(4), 0.01F,
                     "cascade 4 largest wavelength should follow its min waves per domain");
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
        require_near(cascade0.wind_speed, 32.0F, 0.001F, "cascade 0 should carry storm swell wind");
        require_near(cascade1.wind_speed, 30.0F, 0.001F, "cascade 1 should carry storm chop wind");
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
        require_near(cascade1.whitecap, 0.28F, 0.001F,
                     "cascade 1 macro chop should feed restrained breaking source");
        require_near(cascade1.foam_amount, 0.90F, 0.001F,
                     "cascade 1 macro chop should feed low accumulated foam by default");

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
        require_near(cascade2.whitecap, 0.50F, 0.001F,
                     "cascade 2 storm primary crest whitecap should match ref-style breaking");
        require_near(cascade2.foam_amount, 5.80F, 0.001F,
                     "cascade 2 storm primary crest foam should drive accumulated whitecaps");

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
        require_near(cascade3.whitecap, 0.48F, 0.001F,
                     "cascade 3 storm secondary wave whitecap should match ref-style breaking");
        require_near(cascade3.foam_amount, 4.80F, 0.001F,
                     "cascade 3 storm secondary wave foam should support accumulated whitecaps");

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
        require_near(cascade4.whitecap, 0.44F, 0.001F,
                     "cascade 4 storm detail whitecap should stay conservative");
        require_near(cascade4.foam_amount, 2.20F, 0.001F,
                     "cascade 4 storm detail foam should stay secondary");
        require_near(defaults.water_color_r, 0.1F, 0.001F, "water color should match Godot ref");
        require_near(defaults.foam_color_r, 0.73F, 0.001F, "foam color should match Godot ref");
        require_near(defaults.foam_density, 3.15F, 0.001F,
                     "foam density should default to visible persistent coverage");
        require_near(defaults.foam_sharpness, 0.62F, 0.001F,
                     "foam sharpness should default to a whitecap-biased response");
        const ocean::OceanDiagnosticsConfig diagnostics{};
        require_near(diagnostics.anti_repeat_strength, 1.0F, 0.001F,
                     "ocean diagnostics should default to anti-repeat sampling");
        ocean::validate_ocean_config(defaults);

        require(ocean::ocean_render_view_from_name("") == ocean::OceanRenderView::Final,
                "empty debug view should use final ocean rendering");
        require(ocean::ocean_render_view_from_name("height") == ocean::OceanRenderView::Height,
                "height debug view should parse");
        require(ocean::ocean_render_view_from_name("displacement") ==
                    ocean::OceanRenderView::Displacement,
                "displacement debug view should parse");
        require(ocean::ocean_render_view_from_name("normal") == ocean::OceanRenderView::Normal,
                "normal debug view should parse");
        require(ocean::ocean_render_view_from_name("foam") == ocean::OceanRenderView::Foam,
                "foam debug view should parse");
        require(ocean::ocean_render_view_from_name("foam-source") ==
                    ocean::OceanRenderView::FoamSource,
                "foam source debug view should parse");
        require(ocean::ocean_render_view_from_name("foam-history") ==
                    ocean::OceanRenderView::FoamHistory,
                "foam history debug view should parse");
        require(ocean::ocean_render_view_from_name("foam-macro") ==
                    ocean::OceanRenderView::FoamMacro,
                "foam macro debug view should parse");
        require(ocean::ocean_render_view_from_name("foam-crest") ==
                    ocean::OceanRenderView::FoamCrest,
                "foam crest debug view should parse");
        require(ocean::ocean_render_view_from_name("foam-detail") ==
                    ocean::OceanRenderView::FoamDetail,
                "foam detail debug view should parse");
        require(ocean::ocean_render_view_from_name("lod") == ocean::OceanRenderView::Lod,
                "lod debug view should parse");
        require(ocean::ocean_render_view_from_name("sky-radiance") ==
                    ocean::OceanRenderView::SkyRadiance,
                "sky radiance debug view should parse");
        require(ocean::ocean_render_view_from_name("reflection") ==
                    ocean::OceanRenderView::Reflection,
                "reflection debug view should parse");
        require(ocean::ocean_render_view_from_name("direct-light") ==
                    ocean::OceanRenderView::DirectLight,
                "direct light debug view should parse");
        require(ocean::ocean_render_view_from_name("ambient-light") ==
                    ocean::OceanRenderView::AmbientLight,
                "ambient light debug view should parse");
        require(ocean::ocean_render_view_from_name("exposure") == ocean::OceanRenderView::Exposure,
                "exposure debug view should parse");
        require(ocean::ocean_render_view_from_name("foam-raw") == ocean::OceanRenderView::FoamRaw,
                "raw foam debug view should parse");
        require(ocean::ocean_render_view_from_name("foam-lit") == ocean::OceanRenderView::FoamLit,
                "lit foam debug view should parse");
        require(ocean::ocean_render_view_from_name("terrain-depth") ==
                    ocean::OceanRenderView::TerrainDepth,
                "terrain depth debug view should parse");
        require(ocean::ocean_render_view_from_name("terrain-shore") ==
                    ocean::OceanRenderView::TerrainShore,
                "terrain shore debug view should parse");
        require(ocean::ocean_render_view_from_name("terrain-slope") ==
                    ocean::OceanRenderView::TerrainSlope,
                "terrain slope debug view should parse");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Foam) ==
                    ocean::OceanRenderView::FoamSource,
                "ocean debug view cycle should include the foam source view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamSource) ==
                    ocean::OceanRenderView::FoamHistory,
                "ocean debug view cycle should include the foam history view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamHistory) ==
                    ocean::OceanRenderView::FoamMacro,
                "ocean debug view cycle should include the macro foam view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamMacro) ==
                    ocean::OceanRenderView::FoamCrest,
                "ocean debug view cycle should include the crest foam view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamCrest) ==
                    ocean::OceanRenderView::FoamDetail,
                "ocean debug view cycle should include the detail foam view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamDetail) ==
                    ocean::OceanRenderView::Lod,
                "ocean debug view cycle should include the LOD view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Lod) ==
                    ocean::OceanRenderView::SkyRadiance,
                "ocean debug view cycle should include environment diagnostics after LOD");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamRaw) ==
                    ocean::OceanRenderView::FoamLit,
                "ocean debug view cycle should include lit foam after raw foam");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamLit) ==
                    ocean::OceanRenderView::TerrainDepth,
                "ocean debug view cycle should include terrain depth after lit foam");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::TerrainSlope) ==
                    ocean::OceanRenderView::Final,
                "ocean debug view cycle should wrap after terrain slope");

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
        run_config.ocean.terrain_fields = 1;
        run_config.pbr.exposure = 0.5F;
        const ocean::OceanConfig from_run_config = ocean::ocean_config_from_run_config(run_config);
        require(from_run_config.render_view == ocean::OceanRenderView::Foam,
                "run config should initialize ocean debug view");
        require(from_run_config.map_size == 128U, "run config should initialize ocean map size");
        require(!from_run_config.spectral_domains_enabled,
                "run config should initialize ocean spectral domain override");
        require(from_run_config.terrain_fields_enabled,
                "run config should initialize ocean terrain field override");
        require_near(from_run_config.exposure, 0.5F, 0.001F,
                     "run config should initialize ocean exposure");
        cubey::RunConfig solar_run_config;
        solar_run_config.atmosphere.time_of_day_mode = "solar";
        solar_run_config.atmosphere.time_hours = 0.0F;
        const cubey::AtmosphereEnvironmentRunState solar_state =
            cubey::atmosphere_environment_run_state_from_config(solar_run_config.atmosphere);
        require(solar_state.auto_exposure_enabled,
                "solar atmosphere run state should default to auto exposure");
        require(solar_state.resolved_exposure > 0.0F,
                "solar atmosphere run state should resolve a night exposure");

        cubey::RunConfig manual_run_config;
        manual_run_config.atmosphere.sun_elevation_degrees = 20.0F;
        const cubey::AtmosphereEnvironmentRunState manual_state =
            cubey::atmosphere_environment_run_state_from_config(manual_run_config.atmosphere);
        require(manual_state.auto_exposure_enabled,
                "manual atmosphere run state should keep the default auto exposure");

        const char* argv[] = {"ocean",
                              "--ocean-map-size",
                              "256",
                              "--no-ocean-spectral-domains",
                              "--ocean-terrain-fields",
                              "--ocean-cascade",
                              "4",
                              "--debug-view",
                              "normal",
                              "--ocean-wire-overlay",
                              "--ocean-wire-opacity",
                              "0.8"};
        cubey::RunConfig parsed = cubey::parse_run_config(12, const_cast<char**>(argv));
        require(parsed.ocean.map_size == 256U, "CLI parser should accept --ocean-map-size");
        require(parsed.ocean.spectral_domains == 0,
                "CLI parser should accept --no-ocean-spectral-domains");
        require(parsed.ocean.terrain_fields == 1,
                "CLI parser should accept --ocean-terrain-fields");
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
        const std::string unpack_shader = read_text_file(source_root / "shaders/ocean_unpack.comp");
        const std::string vertex_shader = read_text_file(source_root / "shaders/ocean.vert");
        const std::string fragment_shader = read_text_file(source_root / "shaders/ocean.frag");
        const std::string mesh_header = read_text_file(source_root / "ocean_mesh.h");
        const std::string app_source = read_text_file(source_root / "ocean_app.cpp");
        const std::string ui_source = read_text_file(source_root / "ocean_ui.cpp");
        const std::string gpu_resources_source =
            read_text_file(source_root / "ocean_gpu_resources.cpp");
        const std::string cmake_source = read_text_file(source_root / "CMakeLists.txt");

        require_contains(spectrum_shader, "xy = h0(k), zw = conj(h0(-k))",
                         "spectrum shader should document reference h0 packing");
        require_contains(spectrum_shader, "conj_complex(get_spectrum_amplitude(id1, dims))",
                         "spectrum shader should pack conjugated negative frequency");
        require_contains(spectrum_shader, "float spectral_domain_weight",
                         "spectrum shader should apply spectral source-domain filtering");
        require_contains(app_source, "ocean_cascade_domain(ocean_config_, cascade_index)",
                         "app should pass per-cascade spectral domain bounds");
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
        require_contains(unpack_shader, "float jacobian = jxx * jzz - jxz * jxz;",
                         "unpack shader should preserve Jacobian foam source");
        require_contains(unpack_shader, "float compression = 0.5 * (jxx + jzz)",
                         "unpack shader should derive compression diagnostics");
        require_contains(unpack_shader, "current_source = clamp(jacobian_source * foam_grow_rate",
                         "unpack shader should derive final foam source from Jacobian folding");
        require_contains(unpack_shader,
                         "persistent = clamp(persistent * exp(-foam_decay_rate) + current_source",
                         "unpack shader should use additive source/history foam accumulation");
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
        require_contains(mesh_header, "cubey/render/clipmap_grid_2d.h",
                         "ocean clipmap should use the shared render helper");
        require_contains(mesh_header, "clipmap_grid_2d_patches",
                         "ocean clipmap should delegate patch generation to the shared helper");
        require_contains(mesh_header, "clipmap_grid_2d_total_triangle_count",
                         "ocean clipmap should delegate triangle totals to the shared helper");
        require_contains(mesh_header, "clipmap_grid_2d_total_vertex_count",
                         "ocean clipmap should delegate vertex totals to the shared helper");
        require_contains(app_source, "diagnostics_.anti_repeat_strength",
                         "app should pass anti-repeat as diagnostics push data");
        require_contains(app_source, "kOceanSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT",
                         "app should define an HDR ocean scene color format");
        require_contains(
            app_source, "ocean scene color",
            "app should render atmosphere background and surface into an HDR scene target");
        require_contains(app_source, "HdrPostFrame",
                         "app should use the shared HDR post frame helper");
        require_contains(app_source, "display_exposure()",
                         "app should allow atmosphere auto exposure to drive display exposure");
        require_contains(app_source, "hdr_scene_color_texture_desc",
                         "app should use the shared HDR scene color descriptor helper");
        require_contains(app_source, "forward_pbr_post.frag.spv",
                         "app should load the shared PBR post shader");
        require_contains(cmake_source, "forward_pbr_post.frag",
                         "ocean build should compile the shared PBR post fragment shader");
        require_contains(cmake_source, "shaders/cubey/atmosphere/atmosphere.frag",
                         "ocean build should compile the shared atmosphere background shader");
        require_contains(cmake_source, "atmosphere_reflection_prefilter.frag",
                         "ocean build should compile the atmosphere reflection prefilter shader");
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
        require_contains(ui_source, "&ui.config.terrain_fields_enabled",
                         "UI should expose optional terrain field influence");
        require_contains(ui_source, "&ui.config.foam_density", "UI should expose foam density");
        require_contains(ui_source, "&ui.config.foam_sharpness", "UI should expose foam sharpness");
        require_contains(ui_source, "ocean_cascade_domain(ui.config, index)",
                         "UI should expose cascade wavelength domain diagnostics");
        require_contains(ui_source, "domain %.2f-%.2f m",
                         "UI should show cascade diagnostic wavelength bands");
        require_contains(fragment_shader, "struct OceanFoamData",
                         "fragment shader should keep foam role diagnostics grouped");
        require_contains(fragment_shader, "samplerCube atmosphere_sky_radiance_texture",
                         "fragment shader should sample the atmosphere sky radiance cube");
        require_contains(fragment_shader, "vec3 ocean_sky_radiance",
                         "fragment shader should centralize atmosphere sky radiance sampling");
        require_contains(fragment_shader, "float ocean_direct_light_scale",
                         "fragment shader should derive direct light from atmosphere intensity");
        require_contains(
            fragment_shader, "float ocean_ambient_light_scale",
            "fragment shader should derive ambient fill from atmosphere sky luminance");
        require_contains(fragment_shader, "data.gradient += normal_foam.xy * normal_scale",
                         "fragment shader should preserve normal map scale packing");
        require_contains(fragment_shader, "data.total += weighted_foam;",
                         "fragment shader should preserve accumulated foam sampling");
        require_contains(fragment_shader, "data.macro += weighted_foam;",
                         "fragment shader should accumulate macro foam diagnostics");
        require_contains(fragment_shader, "data.crest += weighted_foam;",
                         "fragment shader should accumulate crest foam diagnostics");
        require_contains(fragment_shader, "data.detail += weighted_foam;",
                         "fragment shader should accumulate detail foam diagnostics");
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
        require_contains(fragment_shader, "cascade != 4u",
                         "fragment shader should only break up detail foam mechanically");
        require_contains(
            fragment_shader,
            "sample_normal_foam_domain(cascade, position, tile_length, pixels_per_meter",
            "fragment shader should sample secondary normal/foam domains");
        require_contains(
            fragment_shader, "vec2 foam = vec2(1.0)",
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
        require_contains(fragment_shader,
                         "float ocean_persistent_foam(float persistent, float dist)",
                         "fragment shader should shape accumulated foam before presentation");
        require_contains(fragment_shader,
                         "float ocean_current_foam_core(float current, float dist)",
                         "fragment shader should keep current foam as a tight crest core");
        require_contains(fragment_shader,
                         "float ocean_foam_signal(float persistent, float current, float dist)",
                         "fragment shader should combine persistent foam and crest core");
        require_contains(fragment_shader, "float ocean_foam_coverage(OceanFoamData foam_data",
                         "fragment shader should derive presentation foam coverage from foam data");
        require_contains(fragment_shader, "float coherent_crest",
                         "fragment shader should keep crest foam as the coherent carrier");
        require_contains(fragment_shader, "float detail_gate",
                         "fragment shader should gate detail foam by existing support");
        require_contains(fragment_shader, "vec3 ocean_shaded_foam(",
                         "fragment shader should shade foam as a material");
        require_contains(
            fragment_shader, "ocean_primary_light_intensity()",
            "fragment shader should scale material lighting by atmosphere light energy");
        require_contains(fragment_shader,
                         "specular *= direct_light * mix(1.0, 0.35, material_distance)",
                         "fragment shader should reduce far and foam-covered specular");
        require_contains(fragment_shader,
                         "float ocean_horizon_fog_factor(vec3 view_dir, float dist)",
                         "fragment shader should use view-angle-aware horizon haze");
        require_contains(fragment_shader, "color = vec3(foam_coverage);",
                         "fragment shader should keep debug foam view as presentation coverage");
        require_contains(fragment_shader, "color = vec3(foam_current);",
                         "fragment shader should expose current foam source debug view");
        require_contains(fragment_shader, "color = vec3(foam_persistent);",
                         "fragment shader should expose accumulated foam history debug view");
        require_contains(fragment_shader, "color = vec3(foam_data.macro.x);",
                         "fragment shader should expose macro foam diagnostics");
        require_contains(fragment_shader, "color = vec3(foam_data.crest.x);",
                         "fragment shader should expose crest foam diagnostics");
        require_contains(fragment_shader, "color = vec3(foam_data.detail.x);",
                         "fragment shader should expose detail foam diagnostics");
        require_contains(vertex_shader, "triangle_barycentric(vertex_in_cell)",
                         "vertex shader should feed wireframe diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_FOAM_SOURCE = 5u",
                         "fragment shader should expose foam source diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_FOAM_HISTORY = 6u",
                         "fragment shader should expose foam history diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_LOD = 10u",
                         "fragment shader should expose LOD diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_SKY_RADIANCE = 11u",
                         "fragment shader should expose sky radiance diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_REFLECTION = 12u",
                         "fragment shader should expose reflection diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_FOAM_RAW = 16u",
                         "fragment shader should expose raw foam diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_TERRAIN_DEPTH = 18u",
                         "fragment shader should expose terrain depth diagnostics");
        require_contains(fragment_shader, "sampler2D terrain_ocean_fields_texture",
                         "fragment shader should sample shared terrain-ocean fields");
        require_contains(fragment_shader, "TerrainOceanFieldParams",
                         "fragment shader should consume terrain-ocean field metadata");
        require_contains(fragment_shader, "terrain_ocean.uv_transform",
                         "fragment shader should map terrain fields from shared grid metadata");
        require_contains(fragment_shader, "terrain_ocean.ranges_flags.w > 0.5",
                         "fragment shader should use an explicit terrain field influence flag");
        require_contains(fragment_shader, "sample_terrain_ocean_fields",
                         "fragment shader should centralize terrain-ocean field sampling");
        require_contains(fragment_shader, "ocean_terrain_fields_enabled",
                         "fragment shader should gate optional terrain field influence");
        require_contains(fragment_shader, "ocean_lit_foam_color",
                         "fragment shader should isolate lit foam diagnostics");
        require_contains(fragment_shader, "triangle_wire_factor(frag_barycentric)",
                         "fragment shader should expose wire diagnostics");
        require_contains(gpu_resources_source, ".size = sizeof(float) * 64U",
                         "surface pipeline push constants should match ocean shader layout");
        require_contains(
            gpu_resources_source, "kOceanCascadeCount * 3U",
            "surface layout should bind displacement, normal, and foam for every cascade");
        require_contains(gpu_resources_source, "cascade + kOceanCascadeCount",
                         "surface descriptors should expose normal maps for every cascade");
        require_contains(gpu_resources_source, "cascade + kOceanCascadeCount * 2U",
                         "surface descriptors should expose foam maps for every cascade");
        require_contains(gpu_resources_source, "kOceanSurfaceReflectionBinding",
                         "surface descriptors should expose the atmosphere reflection probe");
        require_contains(gpu_resources_source, "kOceanSurfaceSkyRadianceBinding",
                         "surface descriptors should expose the atmosphere sky radiance cube");
        require_contains(gpu_resources_source, "kOceanSurfaceTerrainFieldBinding",
                         "surface descriptors should expose terrain-ocean fields");
        require_contains(gpu_resources_source, "kOceanSurfaceTerrainFieldUniformBinding",
                         "surface descriptors should expose terrain-ocean metadata uniforms");
        require_contains(
            gpu_resources_source, "update_atmosphere_probe_descriptors",
            "surface descriptors should update both atmosphere probe bindings together");
        require_contains(gpu_resources_source, "update_terrain_ocean_field_descriptor",
                         "surface descriptors should update the terrain-ocean field binding");
        require_contains(gpu_resources_source, "update_terrain_ocean_field_uniform_descriptor",
                         "surface descriptors should update the terrain-ocean uniform binding");
        require_contains(app_source, "AtmosphereEnvironmentRuntime atmosphere_runtime_",
                         "ocean app should own the shared atmosphere runtime");
        require_contains(app_source, "make_ocean_diagnostic_terrain_fields",
                         "ocean app should create a diagnostic terrain-ocean field");
        require_contains(app_source, "create_uploaded_terrain_ocean_field_texture",
                         "ocean app should upload the shared terrain-ocean field texture");
        require_contains(app_source, "FrameUniformBuffer<OceanTerrainFieldUniforms>",
                         "ocean app should upload terrain-ocean metadata per frame");
        require_contains(app_source, "AtmosphereBackgroundFrame atmosphere_background_",
                         "ocean app should own the shared atmosphere background frame");
        require_contains(app_source, "create_atmosphere_background_generated_textures",
                         "ocean app should use shared generated atmosphere atlas textures");
        require_contains(app_source, "record_atmosphere_background",
                         "ocean app should draw the shared atmosphere background");
        require_contains(app_source, "record_atmosphere_environment_if_needed",
                         "ocean app should update the atmosphere probe before drawing water");
        require_contains(
            app_source, "atmosphere_environment_run_state_from_config",
            "ocean app should resolve atmosphere run options through the shared helper");
        require_contains(
            app_source, "atmosphere_environment_advance_time",
            "ocean app should advance dynamic atmosphere time through the shared helper");
        require_contains(
            app_source, "kOceanSunElevationDegrees",
            "ocean app should keep the current default sun elevation as its atmosphere fallback");
        require_contains(app_source, "atmosphere_environment_lighting",
                         "ocean app should derive sun direction from the shared lighting helper");
        require_contains(
            vertex_shader, "vec4 sun_direction",
            "ocean vertex push constants should reserve the shared light direction slot");
        require_contains(
            fragment_shader, "ocean.sun_direction.w",
            "ocean surface shader should read shared light intensity from push constants");
        require_contains(
            fragment_shader, "samplerCube atmosphere_reflection_texture",
            "ocean surface shader should sample the shared atmosphere reflection probe");
        require_contains(fragment_shader, "ocean_environment_reflection",
                         "ocean surface shader should isolate atmosphere reflection lookup");

        require_not_contains(vertex_shader, "ocean_macro_waves",
                             "ocean vertex shader should not use Cubey macro waves");
        require_not_contains(fragment_shader, "ocean_macro_waves",
                             "ocean fragment shader should not use Cubey macro waves");
        require_not_contains(unpack_shader, "bounded_choppy",
                             "ocean unpack shader should not use bounded choppy clamps");
        require_not_contains(
            fragment_shader, "normal_foam_cascade",
            "ocean fragment shader should not pack normals and foam in one sampler");
        require_not_contains(
            fragment_shader, "cubey_pbr_apply_display_transform",
            "ocean surface shader should leave display transform to the post pass");
        require_not_contains(
            fragment_shader, "ocean_sky_color",
            "ocean fragment shader should not use the removed local ocean sky model");
        require_not_contains(fragment_shader, "diffuse_light = 0.36",
                             "ocean foam should not keep a fixed daylight diffuse floor");
        require_not_contains(fragment_shader, "foam_color * 0.58",
                             "ocean far foam should not keep a fixed daylight color floor");
        require_not_contains(fragment_shader, "ocean.mesh_options.w < 0.0",
                             "terrain field influence should not overload horizon fog sign");
        require_not_contains(gpu_resources_source, "sky_pipeline",
                             "ocean GPU resources should not keep the old local sky pipeline");
        require_not_contains(cmake_source, "ocean_sky.frag",
                             "ocean build should not compile the removed local sky shader");
        require_not_contains(cmake_source, "ocean_atmosphere.glsl",
                             "ocean build should not depend on the removed local sky include");

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ocean_config_tests: " << error.what() << '\n';
        return 1;
    }
}
