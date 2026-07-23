#include "ocean_config.h"
#include "ocean_horizon.h"
#include "ocean_mesh.h"
#include "ocean_spectrum_diagnostics.h"
#include "ocean_surface_frame.h"
#include "ocean_surface_quality.h"
#include "ocean_ui.h"

#include <cubey/core/run_config.h>
#include <cubey/engine/atmosphere_environment_config.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
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

void require_before(const std::string& text, const std::string& first, const std::string& second,
                    const char* message) {
    const std::size_t first_pos = text.find(first);
    const std::size_t second_pos = text.find(second);
    require(first_pos != std::string::npos && second_pos != std::string::npos &&
                first_pos < second_pos,
            message);
}

} // namespace

int main() {
    try {
        namespace ocean = cubey::projects::ocean;

        const ocean::OceanConfig defaults{};
        require(defaults.sea_state == ocean::OceanSeaState::Windy,
                "ocean should default to the broadly useful windy sea state");
        require(ocean::ocean_infer_sea_state(defaults) == ocean::OceanSeaState::Windy,
                "default ocean values should exactly match the windy preset");
        require(std::string_view{ocean::ocean_sea_state_name(ocean::OceanSeaState::Calm)} ==
                        "calm" &&
                    std::string_view{ocean::ocean_sea_state_name(ocean::OceanSeaState::Windy)} ==
                        "windy" &&
                    std::string_view{ocean::ocean_sea_state_name(ocean::OceanSeaState::Stormy)} ==
                        "stormy" &&
                    std::string_view{ocean::ocean_sea_state_name(ocean::OceanSeaState::Custom)} ==
                        "custom",
                "ocean sea states should expose stable UI names");
        require(ocean::ocean_sea_state_from_name("") == ocean::OceanSeaState::Windy &&
                    ocean::ocean_sea_state_from_name("calm") == ocean::OceanSeaState::Calm &&
                    ocean::ocean_sea_state_from_name("stormy") == ocean::OceanSeaState::Stormy,
                "ocean sea-state config should parse all serializable presets");
        bool rejected_custom_name = false;
        try {
            static_cast<void>(ocean::ocean_sea_state_from_name("custom"));
        } catch (const std::runtime_error&) {
            rejected_custom_name = true;
        }
        require(rejected_custom_name,
                "custom sea state should remain an inferred UI state rather than config input");
        ocean::OceanConfig calm = defaults;
        ocean::apply_ocean_sea_state(calm, ocean::OceanSeaState::Calm);
        ocean::OceanConfig stormy = defaults;
        ocean::apply_ocean_sea_state(stormy, ocean::OceanSeaState::Stormy);
        require(ocean::ocean_infer_sea_state(calm) == ocean::OceanSeaState::Calm &&
                    ocean::ocean_infer_sea_state(stormy) == ocean::OceanSeaState::Stormy,
                "applied sea states should round-trip through owned-value inference");
        require(calm.cascade_enabled == defaults.cascade_enabled &&
                    stormy.cascade_enabled == defaults.cascade_enabled,
                "all sea states should keep the fixed-cost C0/C1 cascade pair");
        require(calm.map_size == defaults.map_size && stormy.map_size == defaults.map_size &&
                    calm.field_precision == defaults.field_precision &&
                    stormy.field_precision == defaults.field_precision &&
                    calm.cascade_map_sizes == defaults.cascade_map_sizes &&
                    stormy.cascade_map_sizes == defaults.cascade_map_sizes,
                "sea states should not change ocean quality or resource layout");
        require(calm.cascades != defaults.cascades && stormy.cascades != defaults.cascades,
                "sea states should change the wave source without reallocating resources");
        require(calm.cascades[0].tile_length == defaults.cascades[0].tile_length &&
                    stormy.cascades[0].wind_direction_degrees ==
                        defaults.cascades[0].wind_direction_degrees &&
                    calm.cascades[1].seed_x == defaults.cascades[1].seed_x,
                "sea states should preserve domains, directions, and deterministic seeds");
        require(calm.cascades[0].wind_speed < defaults.cascades[0].wind_speed &&
                    defaults.cascades[0].wind_speed < stormy.cascades[0].wind_speed &&
                    calm.surface_foam_strength < defaults.surface_foam_strength &&
                    defaults.surface_foam_strength < stormy.surface_foam_strength,
                "sea states should order wave and foam energy from calm through stormy");
        const ocean::OceanSpectrumDiagnostics calm_spectrum =
            ocean::ocean_spectrum_diagnostics(calm);
        const ocean::OceanSpectrumDiagnostics windy_spectrum =
            ocean::ocean_spectrum_diagnostics(defaults);
        const ocean::OceanSpectrumDiagnostics stormy_spectrum =
            ocean::ocean_spectrum_diagnostics(stormy);
        require_near(calm_spectrum.cascades[0].peak_wavelength_m, 19.0F, 0.5F,
                     "calm C0 should expose its finite-depth spectral peak");
        require_near(calm_spectrum.cascades[1].peak_wavelength_m, 13.1F, 0.5F,
                     "calm C1 should expose its finite-depth spectral peak");
        require_near(windy_spectrum.cascades[0].peak_wavelength_m, 78.1F, 0.8F,
                     "windy C0 should expose its finite-depth spectral peak");
        require_near(windy_spectrum.cascades[1].peak_wavelength_m, 58.6F, 0.8F,
                     "windy C1 should expose its finite-depth spectral peak");
        require_near(stormy_spectrum.cascades[0].peak_wavelength_m, 144.8F, 1.0F,
                     "stormy C0 should expose its finite-depth spectral peak");
        require_near(stormy_spectrum.cascades[1].peak_wavelength_m, 134.6F, 1.0F,
                     "stormy C1 should expose its finite-depth spectral peak");
        require(calm_spectrum.cascades[0].peak_supported &&
                    calm_spectrum.cascades[1].peak_supported,
                "calm peaks should fit in both active tiles");
        require(windy_spectrum.cascades[0].peak_supported &&
                    !windy_spectrum.cascades[1].peak_supported,
                "windy C1 should report the peak just beyond its tile period");
        require(!stormy_spectrum.cascades[0].peak_supported &&
                    !stormy_spectrum.cascades[1].peak_supported,
                "stormy peaks should report as omitted by both active tiles");
        require(!windy_spectrum.cascades[0].domain_active &&
                    !windy_spectrum.cascades[1].domain_active,
                "zero minimum-wave controls should keep active spectral domains inactive");
        require(windy_spectrum.overlapping_pair_count == 1U &&
                    windy_spectrum.max_overlap_octaves > 6.0F,
                "the active C0/C1 pair should report its broad full-spectrum overlap");
        ocean::OceanConfig custom = defaults;
        custom.cascades[0].wind_speed += 0.25F;
        require(ocean::ocean_infer_sea_state(custom) == ocean::OceanSeaState::Custom,
                "manual edits to preset-owned controls should infer a custom sea state");
        bool rejected_custom_preset = false;
        try {
            ocean::apply_ocean_sea_state(custom, ocean::OceanSeaState::Custom);
        } catch (const std::runtime_error&) {
            rejected_custom_preset = true;
        }
        require(rejected_custom_preset,
                "custom sea state should not be accepted as a serializable preset");
        require(defaults.map_size == ocean::kOceanDefaultMapSize,
                "ocean should default to the current reference map size");
        require(defaults.map_size == 512U, "ocean should default to the practical 512 map size");
        require(defaults.field_precision == ocean::OceanFieldPrecision::Half,
                "ocean should default to half precision fields");
        require(defaults.detail_filter == ocean::OceanDetailFilter::Adaptive,
                "ocean should preserve the current adaptive detail filter by default");
        require(defaults.surface_shading_policy ==
                    ocean::OceanSurfaceShadingPolicy::FootprintAdaptive,
                "ocean should default to validated footprint-adaptive shading");
        require(ocean::ocean_surface_shading_policy_from_name("") ==
                    ocean::OceanSurfaceShadingPolicy::FootprintAdaptive,
                "empty ocean shading policy should resolve to the adaptive default");
        require(ocean::ocean_surface_shading_policy_from_name("footprint") ==
                    ocean::OceanSurfaceShadingPolicy::FootprintAdaptive,
                "ocean should parse footprint-adaptive surface shading");
        require(ocean::ocean_detail_filter_from_name("bilinear") ==
                        ocean::OceanDetailFilter::Bilinear &&
                    ocean::ocean_detail_filter_from_name("bicubic") ==
                        ocean::OceanDetailFilter::Bicubic,
                "ocean should parse explicit detail filter ablations");
        require(ocean::ocean_render_view_from_name("background") ==
                    ocean::OceanRenderView::Background,
                "ocean should parse the background-only performance ablation");
        require(ocean::ocean_field_precision_from_name("") == ocean::OceanFieldPrecision::Half,
                "empty ocean field precision config should resolve to the default half precision");
        require(ocean::ocean_field_precision_from_name("full") == ocean::OceanFieldPrecision::Full,
                "ocean should still allow explicit full precision fields");
        require(ocean::ocean_is_supported_map_size(128),
                "ocean should support 128 maps for smoke tests");
        require(ocean::ocean_is_supported_map_size(256), "ocean should support 256 maps");
        require(ocean::ocean_is_supported_map_size(512), "ocean should support 512 maps");
        require(ocean::ocean_is_supported_map_size(1024), "ocean should support 1024 maps");
        require(!ocean::ocean_is_supported_map_size(192),
                "ocean should reject non-reference map sizes");

        const ocean::OceanMeshPatch near_quality_patch{
            .level = 0U,
            .cells_x = 100U,
            .cells_z = 100U,
            .bounds = {-50.0F, 50.0F, -50.0F, 50.0F},
        };
        const ocean::OceanMeshPatch far_quality_patch{
            .level = 3U,
            .cells_x = 100U,
            .cells_z = 100U,
            .bounds = {1000.0F, 2000.0F, -500.0F, 500.0F},
        };
        ocean::OceanConfig fixed_quality = defaults;
        fixed_quality.surface_shading_policy = ocean::OceanSurfaceShadingPolicy::Fixed;
        const ocean::OceanPatchShadingPlan fixed_far_quality =
            ocean::ocean_patch_shading_plan(fixed_quality, far_quality_patch, 0.0F, 0.0F, 10.0F,
                                             std::numbers::pi_v<float> / 3.0F, 900U);
        require(fixed_far_quality.detail_filter == ocean::OceanDetailFilter::Adaptive &&
                    fixed_far_quality.self_shadow_steps == 8U,
                "fixed ocean shading should preserve one filter and shadow count");

        ocean::OceanConfig footprint_quality = defaults;
        const ocean::OceanPatchShadingPlan near_quality =
            ocean::ocean_patch_shading_plan(footprint_quality, near_quality_patch, 0.0F, 0.0F,
                                             10.0F, std::numbers::pi_v<float> / 3.0F, 900U);
        require(near_quality.detail_filter == ocean::OceanDetailFilter::Adaptive &&
                    near_quality.self_shadow_steps == 8U &&
                    !near_quality.detail_filter_reduced && !near_quality.self_shadow_reduced,
                "footprint shading should preserve near-field quality");
        const ocean::OceanPatchShadingPlan far_quality =
            ocean::ocean_patch_shading_plan(footprint_quality, far_quality_patch, 0.0F, 0.0F,
                                             10.0F, std::numbers::pi_v<float> / 3.0F, 900U);
        require(far_quality.detail_filter == ocean::OceanDetailFilter::Bilinear &&
                    far_quality.self_shadow_steps == 4U && far_quality.detail_filter_reduced &&
                    far_quality.self_shadow_reduced,
                "footprint shading should reduce unresolved filter and shadow work");
        footprint_quality.detail_filter = ocean::OceanDetailFilter::Bicubic;
        footprint_quality.self_shadow_steps = 2U;
        const ocean::OceanPatchShadingPlan explicit_far_quality =
            ocean::ocean_patch_shading_plan(footprint_quality, far_quality_patch, 0.0F, 0.0F,
                                             10.0F, std::numbers::pi_v<float> / 3.0F, 900U);
        require(explicit_far_quality.detail_filter == ocean::OceanDetailFilter::Bicubic &&
                    explicit_far_quality.self_shadow_steps == 2U &&
                    !explicit_far_quality.detail_filter_reduced,
                "explicit detail filters and lower shadow counts should remain authoritative");

        require(defaults.mesh_cells >= ocean::kOceanMinMeshCells &&
                    defaults.mesh_cells <= ocean::kOceanMaxMeshCells,
                "default mesh resolution should be in supported range");
        require(defaults.mesh_lod_levels >= ocean::kOceanMinMeshLodLevels &&
                    defaults.mesh_lod_levels <= ocean::kOceanMaxMeshLodLevels,
                "default mesh LOD count should be in supported range");
        require(defaults.horizon_auto_extent,
                "ocean should default to horizon-derived mesh extent");
        require_near(defaults.horizon_extent_margin, 1.25F, 0.001F,
                     "ocean should default to a conservative horizon margin");
        require_near(defaults.horizon_target_near_cell_m, 2.0F, 0.001F,
                     "ocean should default to a near-field horizon mesh target");
        require_near(defaults.horizon_altitude_cell_ratio, 0.04F, 0.001F,
                     "ocean should default to altitude-aware horizon mesh thinning");
        require(defaults.surface_mode == ocean::OceanSurfaceMode::CurvedFar,
                "ocean should default to the curved far-surface mode");
        require_near(defaults.planet_radius_scale, 1.0F, 0.001F,
                     "ocean should default to physical atmosphere planet radius scale");
        require_near(defaults.curvature_start_ratio, 0.25F, 0.001F,
                     "ocean curvature should start after the near field");
        require_near(defaults.curvature_end_ratio, 0.75F, 0.001F,
                     "ocean curvature should finish before the horizon");
        require_near(defaults.curvature_strength, 1.0F, 0.001F,
                     "ocean should default far-surface curvature on");
        require(defaults.spectral_domains_enabled,
                "ocean should default to spectral source-domain filtering");
        const cubey::CloudEnvironmentConfig default_clouds =
            ocean::ocean_cloud_config_from_run_config(cubey::RunConfig{});
        require(default_clouds.enabled, "ocean clouds should be enabled by default");
        require(default_clouds.layer.distance_mode == cubey::render::CloudLayerDistanceMode::Auto,
                "ocean clouds should use the shared surface horizon handoff distance mode");
        require(default_clouds.layer.density_model ==
                    cubey::render::CloudLayerDensityModel::SurfaceVolume,
                "ocean clouds should use the shared surface-volume density model");
        require(default_clouds.layer.quality == cubey::render::CloudLayerQuality::Full &&
                    default_clouds.layer.sampling_mode ==
                        cubey::render::CloudLayerSamplingMode::Bayer &&
                    default_clouds.layer.resolve_mode ==
                        cubey::render::CloudLayerResolveMode::TerrainPost &&
                    !default_clouds.layer.temporal_enabled &&
                    default_clouds.layer.horizon_layer_enabled,
                "ocean clouds should inherit stable surface horizon defaults");
        cubey::RunConfig local_only_clouds_config;
        local_only_clouds_config.clouds.distance_mode = "local";
        local_only_clouds_config.clouds.horizon_layer = 0;
        const cubey::CloudEnvironmentConfig local_only_clouds =
            ocean::ocean_cloud_config_from_run_config(local_only_clouds_config);
        require(local_only_clouds.layer.distance_mode ==
                        cubey::render::CloudLayerDistanceMode::Local &&
                    !local_only_clouds.layer.horizon_layer_enabled,
                "ocean should preserve the local-only cloud fallback");
        cubey::RunConfig no_clouds_config;
        no_clouds_config.clouds.enabled = 0;
        require(!ocean::ocean_cloud_config_from_run_config(no_clouds_config).enabled,
                "ocean should honor --no-clouds");
        cubey::RunConfig deferred_clouds_config;
        deferred_clouds_config.clouds.quality = "half";
        deferred_clouds_config.clouds.view_steps = 48;
        deferred_clouds_config.clouds.view_samples = 2;
        deferred_clouds_config.clouds.coverage = 0.24F;
        deferred_clouds_config.clouds.distance_mode = "orbit-shell";
        deferred_clouds_config.clouds.density_model = "procedural";
        deferred_clouds_config.clouds.resolve_mode = "metadata-bilateral";
        deferred_clouds_config.clouds.view_sample_mode = "temporal-phased";
        deferred_clouds_config.clouds.temporal = 1;
        deferred_clouds_config.clouds.local_volume = 0;
        deferred_clouds_config.clouds.horizon_layer = 1;
        const cubey::CloudEnvironmentConfig clamped_clouds =
            ocean::ocean_cloud_config_from_run_config(deferred_clouds_config);
        require(clamped_clouds.layer.quality == cubey::render::CloudLayerQuality::Half,
                "ocean cloud policy should preserve surface quality overrides");
        require(clamped_clouds.layer.view_steps_override == 48 &&
                    clamped_clouds.layer.view_samples == 2,
                "ocean cloud policy should preserve surface sampling budget overrides");
        require_near(clamped_clouds.layer.coverage, 0.24F, 0.001F,
                     "ocean cloud policy should preserve surface coverage overrides");
        require(clamped_clouds.layer.distance_mode == cubey::render::CloudLayerDistanceMode::Auto,
                "ocean cloud policy should clamp deferred distance overrides to surface handoff");
        require(clamped_clouds.layer.density_model ==
                    cubey::render::CloudLayerDensityModel::SurfaceVolume,
                "ocean cloud policy should clamp deferred density overrides");
        require(clamped_clouds.layer.resolve_mode ==
                    cubey::render::CloudLayerResolveMode::TerrainPost,
                "ocean cloud policy should clamp deferred resolve overrides");
        require(clamped_clouds.layer.view_sample_mode ==
                    cubey::render::CloudLayerViewSampleMode::SingleFrame,
                "ocean cloud policy should clamp temporal sample mode overrides");
        require(!clamped_clouds.layer.temporal_enabled &&
                    clamped_clouds.layer.local_volume_enabled &&
                    clamped_clouds.layer.horizon_layer_enabled,
                "ocean cloud policy should keep the surface handoff cloud structure");
        require(std::string(ocean::ocean_surface_mode_name(ocean::OceanSurfaceMode::Flat)) ==
                    "flat",
                "ocean should name the flat surface mode");
        require(std::string(ocean::ocean_surface_mode_name(ocean::OceanSurfaceMode::CurvedFar)) ==
                    "curved-far",
                "ocean should name the curved far-surface mode");
        require(ocean::ocean_surface_mode_from_name("") == ocean::OceanSurfaceMode::CurvedFar,
                "empty ocean surface mode should resolve to the default curved mode");
        require(ocean::ocean_surface_mode_from_name("curved") == ocean::OceanSurfaceMode::CurvedFar,
                "ocean should accept curved as a shorthand surface mode");
        require(ocean::ocean_surface_mode_from_name("flat") == ocean::OceanSurfaceMode::Flat,
                "ocean should parse the flat surface mode");
        require(ocean::kOceanCascadeCount == 5U,
                "ocean should expose five configurable cascade slots");
        const std::array<bool, ocean::kOceanCascadeCount> expected_enabled{true, true, false, false,
                                                                           false};
        for (std::uint32_t cascade = 0; cascade < ocean::kOceanCascadeCount; ++cascade) {
            require(ocean::ocean_cascade_enabled(defaults, cascade) == expected_enabled[cascade],
                    "ocean should default to the C0/C1 core cascade slots");
            require(defaults.cascade_map_sizes[cascade] == 0U,
                    "ocean cascades should inherit the global FFT map size by default");
            require(ocean::ocean_cascade_map_size(defaults, cascade) == defaults.map_size,
                    "ocean cascade map size helper should resolve inherited sizes");
            require(defaults.cascade_update_intervals[cascade] == 1U,
                    "ocean cascades should update every frame by default");
            require(ocean::ocean_cascade_update_interval(defaults, cascade) == 1U,
                    "ocean cascade update helper should clamp to at least one frame");
        }
        ocean::OceanConfig cascade_policy = defaults;
        cascade_policy.cascade_map_sizes[1] = 256U;
        cascade_policy.cascade_update_intervals[1] = 3U;
        require(ocean::ocean_cascade_map_size(cascade_policy, 1) == 256U,
                "ocean should allow explicit per-cascade FFT map sizes");
        require(ocean::ocean_cascade_update_interval(cascade_policy, 1) == 3U,
                "ocean should allow per-cascade update intervals");
        ocean::OceanConfig invalid_cascade_size = defaults;
        invalid_cascade_size.cascade_map_sizes[0] = 192U;
        bool rejected_invalid_cascade_size = false;
        try {
            ocean::validate_ocean_config(invalid_cascade_size);
        } catch (const std::runtime_error&) {
            rejected_invalid_cascade_size = true;
        }
        require(rejected_invalid_cascade_size,
                "ocean should reject unsupported per-cascade FFT map sizes");
        ocean::OceanConfig invalid_cascade_interval = defaults;
        invalid_cascade_interval.cascade_update_intervals[0] = 0U;
        bool rejected_invalid_cascade_interval = false;
        try {
            ocean::validate_ocean_config(invalid_cascade_interval);
        } catch (const std::runtime_error&) {
            rejected_invalid_cascade_interval = true;
        }
        require(rejected_invalid_cascade_interval,
                "ocean should reject zero per-cascade update intervals");
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
        const float earth_radius_m = ocean::ocean_planet_radius_m(6371.0F);
        require_near(earth_radius_m, 6371000.0F, 1.0F,
                     "ocean should convert atmosphere planet radius to meters");
        const float twenty_meter_horizon = ocean::ocean_horizon_distance_m(earth_radius_m, 20.0F);
        require_near(twenty_meter_horizon, 15963.0F, 2.0F,
                     "ocean horizon distance should follow Earth-scale geometry");
        const ocean::OceanHorizonDiagnostics horizon =
            ocean::ocean_horizon_diagnostics(defaults, 20.0F, 0.0F, earth_radius_m, 1.25F);
        require_near(horizon.camera_altitude_m, 20.0F, 0.001F,
                     "ocean horizon diagnostics should preserve camera altitude");
        require_near(horizon.required_half_extent_m, twenty_meter_horizon * 1.25F, 3.0F,
                     "ocean horizon diagnostics should apply the safety margin");
        require_near(horizon.coverage_ratio, defaults.mesh_extent / horizon.required_half_extent_m,
                     0.001F, "ocean horizon diagnostics should report mesh coverage");
        require_near(horizon.near_cell_size_m, ocean::ocean_mesh_near_cell_size(defaults), 0.001F,
                     "ocean horizon diagnostics should include near mesh cell size");
        require_near(horizon.far_cell_size_m,
                     ocean::ocean_mesh_level_cell_size(defaults, defaults.mesh_lod_levels - 1U),
                     0.001F, "ocean horizon diagnostics should include far mesh cell size");
        const ocean::OceanConfig effective_horizon_mesh =
            ocean::ocean_horizon_effective_mesh_config(defaults, 20.0F, 0.0F, earth_radius_m);
        require(effective_horizon_mesh.mesh_extent > defaults.mesh_extent,
                "auto horizon mesh should expand beyond the manual minimum when needed");
        require(effective_horizon_mesh.mesh_lod_levels >= defaults.mesh_lod_levels,
                "auto horizon mesh should preserve or increase LOD levels");
        require(ocean::ocean_mesh_near_cell_size(effective_horizon_mesh) <=
                        defaults.horizon_target_near_cell_m ||
                    effective_horizon_mesh.mesh_lod_levels == ocean::kOceanMaxMeshLodLevels,
                "auto horizon mesh should target near cell size until max LOD");
        require(effective_horizon_mesh.mesh_cells == defaults.mesh_cells,
                "low camera auto horizon mesh should preserve the configured near resolution");
        const float high_camera_altitude_m = 900.0F;
        const float high_camera_target_cell =
            ocean::ocean_horizon_effective_near_cell_target_m(defaults, high_camera_altitude_m);
        require(high_camera_target_cell > defaults.horizon_target_near_cell_m,
                "high camera horizon target should grow from altitude");
        const ocean::OceanConfig high_camera_mesh = ocean::ocean_horizon_effective_mesh_config(
            defaults, high_camera_altitude_m, 0.0F, earth_radius_m);
        require(high_camera_mesh.mesh_cells < defaults.mesh_cells,
                "high camera auto horizon mesh should lower the effective patch resolution");
        require(ocean::ocean_mesh_near_cell_size(high_camera_mesh) <= high_camera_target_cell ||
                    high_camera_mesh.mesh_lod_levels == ocean::kOceanMaxMeshLodLevels,
                "high camera auto horizon mesh should preserve its effective near-cell target");
        ocean::OceanConfig full_res_high_camera_mesh = high_camera_mesh;
        full_res_high_camera_mesh.mesh_cells = defaults.mesh_cells;
        require(ocean::ocean_mesh_total_triangle_count(high_camera_mesh) <
                    ocean::ocean_mesh_total_triangle_count(full_res_high_camera_mesh),
                "high camera auto horizon mesh should reduce LOD0 triangle cost");
        ocean::OceanConfig manual_horizon_mesh = defaults;
        manual_horizon_mesh.horizon_auto_extent = false;
        const ocean::OceanConfig disabled_horizon_mesh = ocean::ocean_horizon_effective_mesh_config(
            manual_horizon_mesh, 20.0F, 0.0F, earth_radius_m);
        require(disabled_horizon_mesh.mesh_extent == defaults.mesh_extent &&
                    disabled_horizon_mesh.mesh_lod_levels == defaults.mesh_lod_levels,
                "disabled auto horizon mesh should keep manual mesh settings");
        const ocean::OceanSurfaceFrame surface_frame =
            ocean::ocean_surface_frame_from_camera(defaults, {0.0F, 20.0F, 0.0F}, earth_radius_m);
        require(surface_frame.surface_mode == ocean::OceanSurfaceMode::CurvedFar,
                "ocean surface frame should resolve the default curved far-surface mode");
        require(!surface_frame.flat_surface,
                "ocean surface frame should default to curved far-surface mapping");
        require_near(surface_frame.local_frame.water_datum_m, 0.0F, 0.001F,
                     "ocean surface frame should default to the current sea-level datum");
        require_near(surface_frame.camera_local_position_m.y, 20.0F, 0.001F,
                     "ocean surface frame should derive local camera height from the frame");
        require(surface_frame.mesh_config.mesh_extent == effective_horizon_mesh.mesh_extent,
                "ocean surface frame should own the effective mesh config");
        require_near(surface_frame.horizon.horizon_distance_m, twenty_meter_horizon, 2.0F,
                     "ocean surface frame should own horizon diagnostics");
        require_near(surface_frame.projection_far_plane_m,
                     ocean::ocean_horizon_projection_far_plane_m(surface_frame.horizon), 0.001F,
                     "ocean surface frame should own the projection far plane");
        require_near(surface_frame.curvature_start_m, twenty_meter_horizon * 0.25F, 1.0F,
                     "ocean surface frame should resolve curvature start from horizon distance");
        require_near(surface_frame.curvature_end_m, twenty_meter_horizon * 0.75F, 1.0F,
                     "ocean surface frame should resolve curvature end from horizon distance");
        require_near(surface_frame.curvature_strength, 1.0F, 0.001F,
                     "ocean surface frame should resolve active curvature strength");
        require_near(ocean::ocean_spherical_surface_drop_m(0.0F, earth_radius_m), 0.0F, 0.001F,
                     "spherical ocean surface should meet the local tangent datum at origin");
        const float clear_camera_pitch =
            ocean::ocean_above_surface_orbit_pitch(100.0F, 0.35F, 8.0F);
        require(clear_camera_pitch < 0.0F,
                "ocean orbit camera should remain above the unsupported water volume");
        require_near(std::sin(-clear_camera_pitch) * 100.0F, 8.0F, 0.01F,
                     "ocean orbit camera clamp should preserve requested surface clearance");
        require_near(ocean::ocean_above_surface_orbit_pitch(100.0F, -0.45F, 8.0F), -0.45F, 0.001F,
                     "ocean orbit camera should preserve already-clear views");
        require_near(ocean::ocean_cloud_shadow_half_extent_m(100.0F), 16000.0F, 0.001F,
                     "ocean cloud shadows should retain a useful near-camera footprint");
        require_near(ocean::ocean_cloud_shadow_half_extent_m(1000.0F), 40000.0F, 0.001F,
                     "ocean cloud shadows should scale with camera distance");
        require_near(ocean::ocean_cloud_shadow_half_extent_m(4000.0F), 80000.0F, 0.001F,
                     "ocean cloud shadows should stay within the V1 texture budget");
        require_near(ocean::ocean_spherical_surface_drop_m(twenty_meter_horizon, earth_radius_m),
                     -20.0F, 0.01F,
                     "spherical ocean surface should drop by camera altitude near the horizon");
        require_near(ocean::ocean_surface_curvature_drop_m(
                         surface_frame.curvature_start_m, earth_radius_m,
                         surface_frame.curvature_start_m, surface_frame.curvature_end_m,
                         surface_frame.curvature_strength),
                     0.0F, 0.001F, "curved far-surface blend should keep the near boundary flat");
        require_near(
            ocean::ocean_surface_curvature_drop_m(
                surface_frame.curvature_end_m, earth_radius_m, surface_frame.curvature_start_m,
                surface_frame.curvature_end_m, surface_frame.curvature_strength),
            ocean::ocean_spherical_surface_drop_m(surface_frame.curvature_end_m, earth_radius_m),
            0.001F,
            "curved far-surface blend should reach full spherical drop at the far boundary");
        ocean::OceanConfig flat_surface = defaults;
        flat_surface.surface_mode = ocean::OceanSurfaceMode::Flat;
        const ocean::OceanSurfaceFrame flat_surface_frame = ocean::ocean_surface_frame_from_camera(
            flat_surface, {0.0F, 20.0F, 0.0F}, earth_radius_m);
        require(flat_surface_frame.flat_surface &&
                    flat_surface_frame.surface_mode == ocean::OceanSurfaceMode::Flat,
                "flat ocean surface frame should disable curvature");
        require_near(flat_surface_frame.curvature_strength, 0.0F, 0.001F,
                     "flat ocean surface frame should resolve zero curvature strength");

        const ocean::OceanCascadeConfig& cascade0 = stormy.cascades[0];
        const ocean::OceanCascadeConfig& cascade1 = stormy.cascades[1];
        const ocean::OceanCascadeConfig& cascade2 = stormy.cascades[2];
        const ocean::OceanCascadeConfig& cascade3 = stormy.cascades[3];
        const ocean::OceanCascadeConfig& cascade4 = stormy.cascades[4];
        const ocean::OceanCascadeDomain domain0 = ocean::ocean_cascade_domain(defaults, 0);
        const ocean::OceanCascadeDomain domain1 = ocean::ocean_cascade_domain(defaults, 1);
        const ocean::OceanCascadeDomain domain2 = ocean::ocean_cascade_domain(defaults, 2);
        const ocean::OceanCascadeDomain domain3 = ocean::ocean_cascade_domain(defaults, 3);
        const ocean::OceanCascadeDomain domain4 = ocean::ocean_cascade_domain(defaults, 4);
        require(!domain0.active && !domain1.active && domain2.active && domain3.active &&
                    domain4.active,
                "default slot wavelength domains should leave the C0/C1 core unfiltered");
        require(domain2.low_k < domain2.high_k && domain3.low_k < domain3.high_k &&
                    domain4.low_k < domain4.high_k,
                "default cascade domains should have increasing k bounds");
        require(domain2.low_wavelength < domain2.high_wavelength &&
                    domain3.low_wavelength < domain3.high_wavelength &&
                    domain4.low_wavelength < domain4.high_wavelength,
                "default cascade domains should expose low/high wavelength bounds");
        require(domain2.high_wavelength > domain3.high_wavelength &&
                    domain3.high_wavelength > domain4.high_wavelength,
                "active cascade diagnostic domains should run from large to small wavelengths");
        require_near(cascade0.domain_min_waves, 0.0F, 0.001F,
                     "cascade 0 should preserve the primary coherent whitecap carrier");
        require_near(cascade1.domain_min_waves, 0.0F, 0.001F,
                     "cascade 1 should support but not dominate the crest carrier");
        require_near(cascade2.domain_min_waves, 3.0F, 0.001F,
                     "cascade 2 should keep a conservative spectral low cutoff");
        require_near(cascade3.domain_min_waves, 2.0F, 0.001F,
                     "cascade 3 should keep more long chop wavelengths");
        require_near(cascade4.domain_min_waves, 3.0F, 0.001F,
                     "cascade 4 should stay detail-biased");
        require(!domain0.active && domain0.low_k == 0.0F && domain0.high_k == 0.0F,
                "cascade 0 should disable spectral filtering for coherent whitecaps");
        require(!domain1.active && domain1.low_k == 0.0F && domain1.high_k == 0.0F,
                "cascade 1 should disable spectral filtering for coherent whitecaps");
        require_near(domain2.high_wavelength, cascade2.tile_length / cascade2.domain_min_waves,
                     0.01F, "cascade 2 largest wavelength should follow its min waves per domain");
        require_near(domain2.low_wavelength,
                     cascade2.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 2 smallest wavelength should follow map sampling");
        require_near(domain3.high_wavelength, cascade3.tile_length / cascade3.domain_min_waves,
                     0.01F, "cascade 3 largest wavelength should follow its min waves per domain");
        require_near(domain3.low_wavelength,
                     cascade3.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 3 smallest wavelength should follow map sampling");
        require_near(domain4.high_wavelength, cascade4.tile_length / cascade4.domain_min_waves,
                     0.01F, "cascade 4 largest wavelength should follow its min waves per domain");
        require_near(domain4.low_wavelength,
                     cascade4.tile_length * ocean::kOceanCascadeSmallestWaveMultiplier /
                         static_cast<float>(defaults.map_size),
                     0.01F, "cascade 4 smallest wavelength should follow map sampling");
        require_near(cascade0.tile_length, 88.0F, 0.001F,
                     "cascade 0 should keep the reference primary crest scale");
        require_near(cascade0.displacement_scale, 1.42F, 0.001F,
                     "cascade 0 storm primary crest displacement should be tuned");
        require_near(cascade0.normal_scale, 1.14F, 0.001F,
                     "cascade 0 storm primary crest normal should be tuned");
        require_near(cascade0.wind_speed, 18.0F, 0.001F,
                     "cascade 0 storm primary crest wind should be tuned");
        require_near(cascade0.wind_direction_degrees, 20.0F, 0.001F,
                     "cascade 0 wind direction should match Godot ref primary crest");
        require_near(cascade0.fetch_length_km, 350.0F, 0.001F,
                     "cascade 0 storm primary crest fetch should be tuned");
        require_near(cascade0.spread, 0.12F, 0.001F,
                     "cascade 0 storm primary crest spread should be tuned");
        require_near(cascade0.whitecap, 0.50F, 0.001F,
                     "cascade 0 storm primary crest whitecap should match ref-style breaking");
        require_near(cascade0.foam_amount, 5.60F, 0.001F,
                     "cascade 0 storm primary crest foam should drive accumulated whitecaps");

        require_near(cascade1.tile_length, 57.0F, 0.001F,
                     "cascade 1 tile length should match Godot ref secondary wave");
        require_near(cascade1.displacement_scale, 1.10F, 0.001F,
                     "cascade 1 storm secondary wave displacement should be tuned");
        require_near(cascade1.normal_scale, 1.02F, 0.001F,
                     "cascade 1 storm secondary wave normal should be tuned");
        require_near(cascade1.wind_speed, 16.0F, 0.001F,
                     "cascade 1 storm secondary wave wind should be tuned");
        require_near(cascade1.wind_direction_degrees, 17.0F, 0.001F,
                     "cascade 1 storm secondary wave direction should be tuned");
        require_near(cascade1.fetch_length_km, 330.0F, 0.001F,
                     "cascade 1 storm secondary wave fetch should be tuned");
        require_near(cascade1.spread, 0.22F, 0.001F,
                     "cascade 1 storm secondary wave spread should be tuned");
        require_near(cascade1.whitecap, 0.48F, 0.001F,
                     "cascade 1 storm secondary wave whitecap should match ref-style breaking");
        require_near(cascade1.foam_amount, 4.50F, 0.001F,
                     "cascade 1 storm secondary wave foam should support accumulated whitecaps");

        require_near(cascade2.tile_length, 1531.0F, 0.001F,
                     "cascade 2 should be the largest decorrelated candidate slot");
        require_near(cascade3.tile_length, 421.0F, 0.001F,
                     "cascade 3 should be the mid-scale candidate slot");
        require_near(cascade2.displacement_scale, 0.55F, 0.001F,
                     "cascade 2 storm displacement should be tuned");
        require_near(cascade3.displacement_scale, 0.95F, 0.001F,
                     "cascade 3 storm displacement should be tuned");
        require_near(cascade2.wind_speed, 32.0F, 0.001F, "cascade 2 should carry storm swell wind");
        require_near(cascade3.wind_speed, 30.0F, 0.001F, "cascade 3 should carry storm chop wind");
        require(cascade2.tile_length > cascade3.tile_length &&
                    cascade3.tile_length > cascade0.tile_length &&
                    cascade0.tile_length > cascade1.tile_length,
                "candidate and core slots should retain large-to-small scale separation");
        require(cascade2.displacement_scale < cascade3.displacement_scale &&
                    cascade3.displacement_scale < cascade0.displacement_scale,
                "candidate long-wave slots should stay lower amplitude than the core");
        require(cascade2.normal_scale > 0.0F && cascade2.normal_scale < cascade3.normal_scale &&
                    cascade3.normal_scale < cascade0.normal_scale,
                "candidate long-wave slots should feed lower-amplitude normals than the core");
        require_near(cascade2.whitecap, 0.12F, 0.001F,
                     "cascade 2 candidate slot should keep its whitecap threshold");
        require_near(cascade2.foam_amount, 0.0F, 0.001F,
                     "cascade 2 candidate slot should not generate foam by default");
        require_near(cascade3.whitecap, 0.28F, 0.001F,
                     "cascade 3 candidate slot should feed restrained breaking source");
        require_near(cascade3.foam_amount, 0.90F, 0.001F,
                     "cascade 3 candidate slot should feed low accumulated foam by default");

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
        require_near(defaults.foam_density, 2.65F, 0.001F,
                     "windy foam density should keep intermittent visible coverage");
        require_near(defaults.foam_sharpness, 0.65F, 0.001F,
                     "windy foam sharpness should keep a whitecap-biased response");
        const ocean::OceanDiagnosticsConfig diagnostics{};
        require(diagnostics.size_reference_enabled,
                "ocean diagnostics should default the size reference pillar on");
        require_near(defaults.shape_anti_repeat_strength, 1.0F, 0.001F,
                     "ocean should default to shape anti-repeat sampling");
        require_near(defaults.detail_anti_repeat_strength, 1.0F, 0.001F,
                     "ocean should default to detail anti-repeat sampling");
        require_near(defaults.surface_shape_strength, 1.0F, 0.001F,
                     "ocean should default surface shape contribution on");
        require_near(defaults.surface_foam_strength, 0.92F, 0.001F,
                     "windy ocean should default to restrained surface foam");
        require_near(defaults.foam_history_strength, 0.80F, 0.001F,
                     "windy ocean should retain but not saturate foam history");
        require_near(defaults.self_shadow_strength, 0.30F, 0.001F,
                     "windy ocean should keep restrained wave self-shadowing on");
        require_near(defaults.self_shadow_distance, 44.0F, 0.001F,
                     "ocean should default wave self-shadow march reach");
        require_near(defaults.self_shadow_bias, 0.18F, 0.001F,
                     "ocean should default wave self-shadow height bias");
        require(defaults.self_shadow_steps == 8U,
                "ocean should default wave self-shadow sample count");
        require(defaults.self_shadow_far_steps == 4U,
                "ocean should default unresolved wave shadows to four samples");
        require_near(defaults.terrain_foam_strength, 1.0F, 0.001F,
                     "ocean should default terrain foam contribution on");
        require_near(defaults.shape_fade_distance_scale, 1.10F, 0.001F,
                     "ocean should default shape fade distance unchanged");
        require_near(defaults.normal_fade_distance_scale, 1.05F, 0.001F,
                     "ocean should default normal fade distance unchanged");
        require_near(defaults.foam_fade_distance_scale, 1.15F, 0.001F,
                     "ocean should default foam fade distance unchanged");
        require(defaults.far_field_enabled,
                "ocean should default statistical far-field material handoff on");
        require_near(defaults.far_field_start_m, 360.0F, 0.001F,
                     "ocean should default far-field start distance");
        require_near(defaults.far_field_end_m, 2600.0F, 0.001F,
                     "ocean should default far-field end distance");
        require_near(defaults.far_roughness_strength, 0.14F, 0.001F,
                     "windy ocean should use moderate far-field roughness");
        require_near(defaults.far_glint_strength, 0.32F, 0.001F,
                     "windy ocean should retain far-field sun glitter");
        require_near(defaults.far_detail_footprint_start_m, 0.9F, 0.001F,
                     "ocean should default far-detail footprint fade start");
        require_near(defaults.far_detail_footprint_end_m, 5.0F, 0.001F,
                     "ocean should default far-detail footprint fade end");
        require_near(defaults.far_reflection_variation_strength, 0.085F, 0.001F,
                     "windy ocean should keep broad far reflection variation restrained");
        require_near(defaults.sun_glitter_width, 0.095F, 0.001F,
                     "windy ocean should use a moderate reflected-sun corridor width");
        require_near(defaults.cloud_reflection_strength, 0.38F, 0.001F,
                     "ocean should keep default cloud reflections below close-range saturation");
        require(defaults.cloud_reflection_source == ocean::OceanCloudReflectionSource::Planar,
                "ocean should default to coherent planar cloud reflections");
        require(ocean::ocean_cloud_reflection_source_from_name("") ==
                    ocean::OceanCloudReflectionSource::Planar,
                "empty cloud reflection config should preserve the planar default");
        bool current_view_rejected = false;
        try {
            static_cast<void>(ocean::ocean_cloud_reflection_source_from_name("current-view"));
        } catch (const std::runtime_error&) {
            current_view_rejected = true;
        }
        require(current_view_rejected, "retired current-view cloud reflections should be rejected");
        bool hybrid_rejected = false;
        try {
            static_cast<void>(ocean::ocean_cloud_reflection_source_from_name("hybrid"));
        } catch (const std::runtime_error&) {
            hybrid_rejected = true;
        }
        require(hybrid_rejected, "retired hybrid cloud reflections should be rejected");
        require(defaults.cloud_environment_extent == 64U,
                "ocean should default the cached cloud environment to 64 pixels per face");
        require_near(defaults.cloud_environment_update_hz, 4.0F, 0.001F,
                     "ocean should default the cached cloud environment to four hertz");
        require_near(defaults.cloud_planar_resolution_scale, 0.5F, 0.001F,
                     "ocean should default planar cloud reflections to half resolution");
        require(defaults.cloud_planar_view_steps == 32U,
                "ocean should default planar cloud reflections to 32 view steps");
        require_near(defaults.cloud_planar_guard_band, 0.15F, 0.001F,
                     "ocean should reserve a reflected cloud guard band");
        require_near(defaults.cloud_shadow_strength, 0.40F, 0.001F,
                     "ocean should default shared cloud transmittance on conservatively");
        const ocean::OceanCascadeLodBand cascade0_lod = ocean::ocean_cascade_lod_band(defaults, 0);
        require_near(cascade0_lod.displacement_fade_start,
                     defaults.cascades[0].tile_length * ocean::kOceanCascadeDistanceFadeStartWaves *
                         defaults.shape_fade_distance_scale,
                     0.001F, "ocean should derive cascade displacement fade starts from tile size");
        require_near(cascade0_lod.displacement_fade_end,
                     defaults.cascades[0].tile_length * ocean::kOceanCascadeDistanceFadeEndWaves *
                         defaults.shape_fade_distance_scale,
                     0.001F, "ocean should derive cascade displacement fade ends from tile size");
        require_near(cascade0_lod.surface_fade_start,
                     defaults.cascades[0].tile_length * ocean::kOceanCascadeSurfaceFadeStartWaves *
                         defaults.shape_fade_distance_scale,
                     0.001F, "ocean should derive cascade surface fade starts from tile size");
        require_near(cascade0_lod.mesh_cell_full,
                     defaults.cascades[0].tile_length / ocean::kOceanCascadeMeshFullTileCellDivisor,
                     0.001F, "ocean should derive cascade mesh full-support cell size");
        require(cascade0_lod.mesh_cell_full < cascade0_lod.mesh_cell_zero,
                "ocean cascade LOD should fade as mesh cells become coarser");
        require_near(ocean::ocean_cascade_displacement_lod_weight(
                         defaults, 0, 0.0F, cascade0_lod.mesh_cell_full * 0.5F),
                     1.0F, 0.001F, "ocean should fully support near fine-mesh displacement");
        require_near(ocean::ocean_cascade_displacement_lod_weight(
                         defaults, 0, cascade0_lod.displacement_fade_end + 1.0F,
                         cascade0_lod.mesh_cell_full * 0.5F),
                     0.0F, 0.001F, "ocean should fade displacement after the distance band");
        require_near(
            ocean::ocean_cascade_mesh_lod_weight(defaults, 0, cascade0_lod.mesh_cell_zero + 1.0F),
            0.0F, 0.001F, "ocean should reject displacement on coarse mesh cells");
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
        require(ocean::ocean_render_view_from_name("curvature") ==
                    ocean::OceanRenderView::Curvature,
                "curvature debug view should parse");
        require(ocean::ocean_render_view_from_name("footprint") ==
                    ocean::OceanRenderView::Footprint,
                "footprint debug view should parse");
        require(ocean::ocean_render_view_from_name("energy-lod") ==
                    ocean::OceanRenderView::EnergyLod,
                "energy LOD debug view should parse");
        require(ocean::ocean_render_view_from_name("far-field") == ocean::OceanRenderView::FarField,
                "far-field debug view should parse");
        require(ocean::ocean_render_view_from_name("cloud-shadow") ==
                    ocean::OceanRenderView::CloudShadow,
                "cloud shadow debug view should parse");
        require(ocean::ocean_render_view_from_name("cloud-reflection") ==
                    ocean::OceanRenderView::CloudReflection,
                "cloud reflection debug view should parse");
        require(ocean::ocean_render_view_from_name("cloud-reflection-validity") ==
                    ocean::OceanRenderView::CloudReflectionValidity,
                "cloud reflection validity debug view should parse");
        require(ocean::ocean_render_view_from_name("water-body") ==
                    ocean::OceanRenderView::WaterBody,
                "water body debug view should parse");
        require(ocean::ocean_render_view_from_name("fresnel") == ocean::OceanRenderView::Fresnel,
                "Fresnel debug view should parse");
        require(ocean::ocean_render_view_from_name("specular") == ocean::OceanRenderView::Specular,
                "specular debug view should parse");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Foam) ==
                    ocean::OceanRenderView::FoamSource,
                "ocean debug view cycle should include the foam source view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamSource) ==
                    ocean::OceanRenderView::FoamHistory,
                "ocean debug view cycle should include the foam history view");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FoamHistory) ==
                    ocean::OceanRenderView::Lod,
                "ocean debug view cycle should include the LOD view after foam history");
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
                    ocean::OceanRenderView::Curvature,
                "ocean debug view cycle should include curvature after terrain slope");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Curvature) ==
                    ocean::OceanRenderView::Footprint,
                "ocean debug view cycle should include footprint after curvature");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Footprint) ==
                    ocean::OceanRenderView::EnergyLod,
                "ocean debug view cycle should include energy LOD after footprint");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::EnergyLod) ==
                    ocean::OceanRenderView::FarField,
                "ocean debug view cycle should include far-field after energy LOD");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::FarField) ==
                    ocean::OceanRenderView::CloudShadow,
                "ocean debug view cycle should include cloud shadow after far-field");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::CloudShadow) ==
                    ocean::OceanRenderView::CloudReflection,
                "ocean debug view cycle should include cloud reflection after cloud shadow");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::CloudReflection) ==
                    ocean::OceanRenderView::WaterBody,
                "ocean debug view cycle should include the water body after cloud reflection");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::WaterBody) ==
                    ocean::OceanRenderView::Fresnel,
                "ocean debug view cycle should include Fresnel after the water body");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Fresnel) ==
                    ocean::OceanRenderView::CloudReflectionValidity,
                "ocean debug view cycle should include planar validity after material diagnostics");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::CloudReflectionValidity) ==
                    ocean::OceanRenderView::Specular,
                "ocean debug view cycle should include specular after planar validity");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Specular) ==
                    ocean::OceanRenderView::Background,
                "ocean debug view cycle should include the background performance ablation");
        require(ocean::next_ocean_render_view(ocean::OceanRenderView::Background) ==
                    ocean::OceanRenderView::Final,
                "ocean debug view cycle should wrap after the background ablation");

        bool rejected = false;
        try {
            static_cast<void>(ocean::ocean_render_view_from_name("detail"));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should not expose the experimental detail debug view");

        cubey::RunConfig run_config;
        run_config.debug_view = "foam";
        run_config.ocean.sea_state = "stormy";
        run_config.ocean.map_size = 128;
        run_config.ocean.surface_mode = "flat";
        run_config.ocean.mesh_cells = 256U;
        run_config.ocean.mesh_lod_levels = 4U;
        run_config.ocean.horizon_target_near_cell_m = 3.5F;
        run_config.ocean.surface_shading_policy = "footprint";
        run_config.ocean.self_shadow_strength = 0.2F;
        run_config.ocean.self_shadow_steps = 4U;
        run_config.ocean.self_shadow_far_steps = 2U;
        run_config.ocean.shape_anti_repeat_strength = 0.25F;
        run_config.ocean.detail_anti_repeat_strength = 0.5F;
        run_config.ocean.detail_filter = "bilinear";
        run_config.ocean.planet_radius_scale = 0.25F;
        run_config.ocean.curvature_start_ratio = 0.20F;
        run_config.ocean.curvature_end_ratio = 0.80F;
        run_config.ocean.curvature_strength = 0.35F;
        run_config.ocean.cloud_reflection_source = "planar";
        run_config.ocean.cloud_environment_extent = 128U;
        run_config.ocean.cloud_environment_update_hz = 8.0F;
        run_config.ocean.cloud_planar_resolution_scale = 0.75F;
        run_config.ocean.cloud_planar_view_steps = 48U;
        run_config.ocean.cloud_planar_guard_band = 0.22F;
        run_config.ocean.cloud_reflection_strength = 0.82F;
        run_config.ocean.cloud_shadow_strength = 0.41F;
        run_config.ocean.spectral_domains = 0;
        run_config.ocean.terrain_fields = 1;
        run_config.pbr.exposure = 0.5F;
        const ocean::OceanConfig from_run_config = ocean::ocean_config_from_run_config(run_config);
        require(from_run_config.render_view == ocean::OceanRenderView::Foam,
                "run config should initialize ocean debug view");
        require(from_run_config.sea_state == ocean::OceanSeaState::Stormy,
                "run config should apply the selected sea state before independent overrides");
        require(from_run_config.map_size == 128U, "run config should initialize ocean map size");
        require(from_run_config.field_precision == ocean::OceanFieldPrecision::Half,
                "run config should inherit the default ocean field precision");
        require(from_run_config.surface_mode == ocean::OceanSurfaceMode::Flat,
                "run config should initialize ocean surface mode");
        require(from_run_config.mesh_cells == 256U &&
                    from_run_config.mesh_lod_levels == 4U,
                "run config should initialize ocean mesh ablation controls");
        require_near(from_run_config.horizon_target_near_cell_m, 3.5F, 0.001F,
                     "run config should initialize the ocean near-cell target");
        require(from_run_config.surface_shading_policy ==
                    ocean::OceanSurfaceShadingPolicy::FootprintAdaptive,
                "run config should initialize the ocean surface shading policy");
        require_near(from_run_config.self_shadow_strength, 0.2F, 0.001F,
                     "run config should initialize ocean self-shadow strength");
        require(from_run_config.self_shadow_steps == 4U,
                "run config should initialize ocean self-shadow steps");
        require(from_run_config.self_shadow_far_steps == 2U,
                "run config should initialize far ocean self-shadow steps");
        require_near(from_run_config.shape_anti_repeat_strength, 0.25F, 0.001F,
                     "run config should initialize shape anti-repeat strength");
        require_near(from_run_config.detail_anti_repeat_strength, 0.5F, 0.001F,
                     "run config should initialize detail anti-repeat strength");
        require(from_run_config.detail_filter == ocean::OceanDetailFilter::Bilinear,
                "run config should initialize the ocean detail filter");
        require_near(from_run_config.planet_radius_scale, 0.25F, 0.001F,
                     "run config should initialize ocean planet radius scale");
        require_near(from_run_config.curvature_start_ratio, 0.20F, 0.001F,
                     "run config should initialize ocean curvature start ratio");
        require_near(from_run_config.curvature_end_ratio, 0.80F, 0.001F,
                     "run config should initialize ocean curvature end ratio");
        require_near(from_run_config.curvature_strength, 0.35F, 0.001F,
                     "run config should initialize ocean curvature strength");
        require_near(from_run_config.cloud_reflection_strength, 0.82F, 0.001F,
                     "run config should initialize cloud reflection strength");
        require(from_run_config.cloud_reflection_source ==
                    ocean::OceanCloudReflectionSource::Planar,
                "run config should initialize cloud reflection source");
        require(from_run_config.cloud_environment_extent == 128U,
                "run config should initialize cloud environment extent");
        require_near(from_run_config.cloud_environment_update_hz, 8.0F, 0.001F,
                     "run config should initialize cloud environment update rate");
        require_near(from_run_config.cloud_planar_resolution_scale, 0.75F, 0.001F,
                     "run config should initialize planar cloud resolution");
        require(from_run_config.cloud_planar_view_steps == 48U,
                "run config should initialize planar cloud step count");
        require_near(from_run_config.cloud_planar_guard_band, 0.22F, 0.001F,
                     "run config should initialize planar cloud guard band");
        require_near(from_run_config.cloud_shadow_strength, 0.41F, 0.001F,
                     "run config should initialize cloud shadow strength");
        require(!from_run_config.spectral_domains_enabled,
                "run config should initialize ocean spectral domain override");
        require(from_run_config.terrain_fields_enabled,
                "run config should initialize ocean terrain field override");
        require_near(from_run_config.exposure, 0.5F, 0.001F,
                     "run config should initialize ocean exposure");
        const cubey::AtmosphereEnvironmentRunState default_atmosphere_state =
            cubey::atmosphere_environment_run_state_from_config(
                cubey::RunConfig{}.atmosphere,
                {
                    .sun_elevation_degrees = 20.0F,
                    .sun_azimuth_degrees = -20.0F,
                    .ground_mode =
                        cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
                    .reference_geometry_enabled = false,
                });
        require_near(default_atmosphere_state.environment.time_of_day.time_hours, 5.5F, 0.001F,
                     "default ocean atmosphere should start just before dawn");
        require_near(default_atmosphere_state.environment.time_of_day.azimuth_offset_degrees,
                     -10.0F, 0.001F,
                     "default ocean atmosphere should use the shared sunrise orientation offset");
        require_near(default_atmosphere_state.time_speed_hours_per_second, 0.5F, 0.001F,
                     "default ocean atmosphere should run at half an hour per second");
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
                              "--ocean-sea-state",
                              "calm",
                              "--ocean-map-size",
                              "256",
                              "--no-ocean-spectral-domains",
                              "--ocean-terrain-fields",
                              "--ocean-cascade",
                              "4",
                              "--debug-view",
                              "normal",
                              "--ocean-camera-preset",
                              "wide",
                              "--ocean-wire-overlay",
                              "--ocean-wire-opacity",
                              "0.8",
                              "--ocean-surface-mode",
                              "flat",
                              "--ocean-planet-radius-scale",
                              "0.25",
                              "--ocean-curvature-start-ratio",
                              "0.2",
                              "--ocean-curvature-end-ratio",
                              "0.8",
                              "--ocean-curvature-strength",
                              "0.35"};
        cubey::RunConfig parsed = cubey::parse_run_config(26, const_cast<char**>(argv));
        require(parsed.ocean.sea_state == "calm", "CLI parser should accept --ocean-sea-state");
        require(parsed.ocean.map_size == 256U, "CLI parser should accept --ocean-map-size");
        require(parsed.ocean.surface_mode == "flat",
                "CLI parser should accept --ocean-surface-mode");
        require_near(parsed.ocean.planet_radius_scale, 0.25F, 0.001F,
                     "CLI parser should accept --ocean-planet-radius-scale");
        require_near(parsed.ocean.curvature_start_ratio, 0.2F, 0.001F,
                     "CLI parser should accept --ocean-curvature-start-ratio");
        require_near(parsed.ocean.curvature_end_ratio, 0.8F, 0.001F,
                     "CLI parser should accept --ocean-curvature-end-ratio");
        require_near(parsed.ocean.curvature_strength, 0.35F, 0.001F,
                     "CLI parser should accept --ocean-curvature-strength");
        require(parsed.ocean.spectral_domains == 0,
                "CLI parser should accept --no-ocean-spectral-domains");
        require(parsed.ocean.terrain_fields == 1,
                "CLI parser should accept --ocean-terrain-fields");
        require(parsed.ocean.cascade == 4, "CLI parser should accept --ocean-cascade");
        require(parsed.ocean.camera_preset == "wide",
                "CLI parser should accept --ocean-camera-preset");
        require(parsed.debug_view == "normal", "CLI parser should preserve debug view");
        require(parsed.ocean.wire_overlay, "CLI parser should accept ocean wire overlay");
        require_near(parsed.ocean.wire_opacity, 0.8F, 0.001F,
                     "CLI parser should accept ocean wire opacity");

        const char* ablation_argv[] = {
            "ocean",
            "--ocean-mesh-cells",
            "192",
            "--ocean-mesh-lod-levels",
            "4",
            "--ocean-horizon-target-near-cell-m",
            "3.0",
            "--ocean-self-shadow-strength",
            "0.0",
            "--ocean-self-shadow-steps",
            "2",
            "--ocean-self-shadow-far-steps",
            "1",
            "--ocean-surface-shading-policy",
            "footprint",
            "--ocean-shape-anti-repeat-strength",
            "0.5",
            "--ocean-detail-anti-repeat-strength",
            "0.25",
            "--ocean-detail-filter",
            "bicubic",
            "--ocean-camera-orbit-spin-deg-per-sec",
            "3",
            "--no-ocean-size-reference",
        };
        parsed = cubey::parse_run_config(static_cast<int>(std::size(ablation_argv)),
                                         const_cast<char**>(ablation_argv));
        require(parsed.ocean.mesh_cells == 192U && parsed.ocean.mesh_lod_levels == 4U,
                "CLI parser should accept ocean mesh ablation controls");
        require_near(parsed.ocean.horizon_target_near_cell_m, 3.0F, 0.001F,
                     "CLI parser should accept the ocean near-cell target");
        require_near(parsed.ocean.self_shadow_strength, 0.0F, 0.001F,
                     "CLI parser should accept disabled ocean self-shadowing");
        require(parsed.ocean.self_shadow_steps == 2U,
                "CLI parser should accept ocean self-shadow steps");
        require(parsed.ocean.self_shadow_far_steps == 1U,
                "CLI parser should accept far ocean self-shadow steps");
        require(parsed.ocean.surface_shading_policy == "footprint",
                "CLI parser should accept ocean surface shading policy");
        require_near(parsed.ocean.shape_anti_repeat_strength, 0.5F, 0.001F,
                     "CLI parser should accept shape anti-repeat strength");
        require_near(parsed.ocean.detail_anti_repeat_strength, 0.25F, 0.001F,
                     "CLI parser should accept detail anti-repeat strength");
        require(parsed.ocean.detail_filter == "bicubic",
                "CLI parser should accept an ocean detail filter");
        require_near(parsed.ocean.camera_orbit_spin_degrees_per_second, 3.0F, 0.001F,
                     "CLI parser should accept ocean capture orbit spin");
        require(parsed.ocean.size_reference == 0,
                "CLI parser should accept a disabled ocean size reference");

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

        ocean::OceanConfig invalid_horizon_margin = defaults;
        invalid_horizon_margin.horizon_extent_margin = 0.0F;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_horizon_margin);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject invalid horizon margin");

        ocean::OceanConfig invalid_horizon_near_cell = defaults;
        invalid_horizon_near_cell.horizon_target_near_cell_m = 0.0F;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_horizon_near_cell);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject invalid horizon near-cell target");

        ocean::OceanConfig invalid_horizon_altitude_cell = defaults;
        invalid_horizon_altitude_cell.horizon_altitude_cell_ratio = -0.01F;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_horizon_altitude_cell);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject invalid horizon altitude-cell ratio");

        ocean::OceanConfig invalid_curvature_order = defaults;
        invalid_curvature_order.curvature_start_ratio = 0.75F;
        invalid_curvature_order.curvature_end_ratio = 0.25F;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_curvature_order);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject inverted curvature blend ratios");

        ocean::OceanConfig invalid_planet_scale = defaults;
        invalid_planet_scale.planet_radius_scale = 0.0F;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_planet_scale);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject invalid planet radius scale");

        ocean::OceanConfig invalid_curvature_strength = defaults;
        invalid_curvature_strength.curvature_strength = 1.5F;
        rejected = false;
        try {
            ocean::validate_ocean_config(invalid_curvature_strength);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "ocean should reject invalid curvature strength");

        const std::filesystem::path source_root(CUBEY_OCEAN_SOURCE_DIR);
        const std::string spectrum_shader =
            read_text_file(source_root / "shaders/ocean_spectrum_body.glsl");
        const std::string modulate_shader =
            read_text_file(source_root / "shaders/ocean_modulate_body.glsl");
        const std::string unpack_shader =
            read_text_file(source_root / "shaders/ocean_unpack_body.glsl");
        const std::string spectrum_entry =
            read_text_file(source_root / "shaders/ocean_spectrum.comp");
        const std::string spectrum_half_entry =
            read_text_file(source_root / "shaders/ocean_spectrum_half.comp");
        const std::string vertex_shader = read_text_file(source_root / "shaders/ocean.vert");
        const std::string ocean_fragment_entry = read_text_file(source_root / "shaders/ocean.frag");
        const std::string fragment_shader =
            ocean_fragment_entry + read_text_file(source_root / "shaders/ocean_shading.glsl") +
            read_text_file(source_root / "shaders/ocean_far_field.glsl") +
            read_text_file(source_root / "shaders/ocean_foam.glsl") +
            read_text_file(source_root / "shaders/ocean_debug.glsl");
        const std::string pillar_vertex_shader =
            read_text_file(source_root / "shaders/ocean_reference_pillar.vert");
        const std::string pillar_fragment_shader =
            read_text_file(source_root / "shaders/ocean_reference_pillar.frag");
        const std::string mesh_header = read_text_file(source_root / "ocean_mesh.h");
        const std::string app_source = read_text_file(source_root / "ocean_app.cpp");
        const std::string ui_source = read_text_file(source_root / "ocean_ui.cpp");
        const std::string gpu_resources_source =
            read_text_file(source_root / "ocean_gpu_resources.cpp");
        const std::string gpu_header_source = read_text_file(source_root / "ocean_gpu_resources.h");
        const std::string config_header = read_text_file(source_root / "ocean_config.h");
        const std::string cmake_source = read_text_file(source_root / "CMakeLists.txt");
        const std::string shader_cmake_source =
            read_text_file(source_root.parent_path().parent_path() / "cmake/CubeyShaders.cmake");

        require_contains(spectrum_shader, "xy = h0(k), zw = conj(h0(-k))",
                         "spectrum shader should document reference h0 packing");
        require_contains(spectrum_entry, "#define OCEAN_FIELD_FORMAT rgba32f",
                         "full ocean spectrum entry should use rgba32f storage images");
        require_contains(spectrum_half_entry, "#define OCEAN_FIELD_FORMAT rgba16f",
                         "half ocean spectrum entry should use rgba16f storage images");
        require_contains(spectrum_shader, "conj_complex(get_spectrum_amplitude(id1, dims))",
                         "spectrum shader should pack conjugated negative frequency");
        require_contains(spectrum_shader, "float spectral_domain_weight",
                         "spectrum shader should apply spectral source-domain filtering");
        require_contains(app_source, "ocean_cascade_domain(ocean_config_, cascade_index)",
                         "app should pass per-cascade spectral domain bounds");
        require_contains(config_header, "kOceanSpectrumFieldCount = 2U",
                         "ocean should pack the four logical FFT spectra into two storage fields");
        require_contains(modulate_shader, "const uint NUM_SPECTRA = 4U",
                         "modulate shader should preserve four logical reference spectra");
        require_contains(modulate_shader, "vec2 dhy_dx = h_inv * k_vec.y;",
                         "modulate shader should preserve swapped derivative axis");
        require_contains(modulate_shader, "vec2 dhx_dx = -h * k_vec.y * k_unit.y;",
                         "modulate shader should preserve reference horizontal derivative");
        require_contains(modulate_shader,
                         "hx.x - hy.y, hx.y + hy.x, hz.x - dhy_dx.y, hz.y + dhy_dx.x",
                         "modulate shader should preserve packed layer 0/1 values");
        require_contains(modulate_shader, "dhz_dz.x - dhz_dx.y, dhz_dz.y + dhz_dx.x",
                         "modulate shader should preserve packed layer 2/3 values");
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
        require_contains(unpack_shader, "float hz = field0.z * sign_shift;",
                         "unpack shader should split the packed first FFT field");
        require_contains(unpack_shader, "float dhz_dx = field1.w * sign_shift;",
                         "unpack shader should split the packed second FFT field");
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
        require_contains(vertex_shader, "float cascade_distance_lod_weight",
                         "vertex shader should centralize cascade distance LOD weights");
        require_contains(vertex_shader, "float cascade_mesh_lod_weight",
                         "vertex shader should fade displacement by clipmap mesh cell size");
        require_contains(vertex_shader, "OCEAN_CASCADE_DISTANCE_FADE_START_WAVES = 8.0",
                         "vertex shader should mirror the C++ displacement LOD start");
        require_contains(vertex_shader, "OCEAN_CASCADE_DISTANCE_FADE_END_WAVES = 24.0",
                         "vertex shader should mirror the C++ displacement LOD end");
        require_contains(vertex_shader, "OCEAN_CASCADE_MESH_FULL_TILE_CELL_DIVISOR = 10.0",
                         "vertex shader should mirror the C++ mesh full-support threshold");
        require_contains(vertex_shader, "OCEAN_CASCADE_MESH_ZERO_TILE_CELL_DIVISOR = 4.0",
                         "vertex shader should mirror the C++ mesh zero-support threshold");
        require_contains(vertex_shader, "frag_mesh_cell_size",
                         "vertex shader should pass mesh cell size to fragment diagnostics");
        require_contains(vertex_shader, "float horizon_displacement_weight",
                         "vertex shader should fade displacement only near the horizon");
        require_contains(vertex_shader, "float ocean_water_datum_y()",
                         "vertex shader should name the ocean surface datum");
        require_contains(vertex_shader, "ocean_features.surface_frame_options.x",
                         "vertex shader should read water datum from surface frame uniforms");
        require_contains(vertex_shader, "vec3 ocean_surface_up(vec2 local_xz)",
                         "vertex shader should name the local surface up contract");
        require_contains(vertex_shader, "vec2 ocean_surface_sample_position(vec2 local_xz)",
                         "vertex shader should keep FFT sampling behind a surface contract");
        require_contains(vertex_shader, "float ocean_surface_drop_y(vec2 local_xz)",
                         "vertex shader should isolate curved far-surface drop");
        require_contains(vertex_shader, "vec3 ocean_surface_world_position",
                         "vertex shader should isolate surface world mapping");
        require_contains(vertex_shader, "frag_surface_up",
                         "vertex shader should pass mapped surface up to fragment shading");
        require_contains(vertex_shader, "frag_surface_curve_drop",
                         "vertex shader should pass curvature diagnostics to fragment shading");
        require_contains(vertex_shader, "add_displacement(displacement, cascade, base_position",
                         "vertex shader should keep FFT displacement in local XZ space");
        require_contains(vertex_shader, "if (!ocean_cascade_enabled(cascade))",
                         "vertex shader should gate displacement by inspected cascade");
        require_contains(vertex_shader, "for (uint cascade = 0u; cascade < 5u; ++cascade)",
                         "vertex shader should include all regular displacement cascades");
        require_contains(vertex_shader, "bool ocean_shape_anti_repeat_enabled",
                         "vertex shader should expose shape anti-repeat for enabled slots");
        require_not_contains(vertex_shader, "cascade < 2u && ocean.inspection_options.y > 0.0",
                             "vertex shader should not gate shape anti-repeat to C0/C1");
        require_contains(vertex_shader, "sample_ocean_displacement(cascade, position, tile_length)",
                         "vertex shader should apply anti-repeat displacement sampling");
        require_contains(mesh_header, "cubey/render/clipmap_grid_2d.h",
                         "ocean clipmap should use the shared render helper");
        require_contains(mesh_header, "clipmap_grid_2d_patches",
                         "ocean clipmap should delegate patch generation to the shared helper");
        require_contains(mesh_header, "clipmap_grid_2d_total_triangle_count",
                         "ocean clipmap should delegate triangle totals to the shared helper");
        require_contains(mesh_header, "clipmap_grid_2d_total_vertex_count",
                         "ocean clipmap should delegate vertex totals to the shared helper");
        require_contains(app_source, "ocean_config_.shape_anti_repeat_strength",
                         "app should pass shape anti-repeat as ocean configuration");
        require_contains(app_source, "ocean_config_.detail_anti_repeat_strength",
                         "app should pass detail anti-repeat as ocean configuration");
        require_contains(
            app_source,
            "surface_feature_uniforms(draw_plan.surface_frame, cloud_shadow, frame_slot)",
            "app should isolate shader feature controls in a frame uniform");
        require_contains(app_source, "OceanMeshDrawPlan ocean_mesh_draw_plan",
                         "ocean app should centralize visible mesh patch planning");
        require_contains(app_source, "cubey::scene::intersects(frustum, bounds)",
                         "ocean app should frustum-cull clipmap patches before draw");
        require_contains(app_source, ".triangles = draw_plan.stats.submitted_triangles",
                         "ocean frame stats should report submitted post-cull mesh triangles");
        require_contains(app_source, "upload_surface_feature_uniforms",
                         "app should upload shader feature controls before ocean draw");
        require_contains(app_source, "ocean_config_.surface_shape_strength",
                         "app should pass surface shape isolation strength");
        require_contains(app_source, "ocean_config_.surface_foam_strength",
                         "app should pass surface foam isolation strength");
        require_contains(app_source, "ocean_config_.foam_history_strength",
                         "app should pass foam history isolation strength");
        require_contains(app_source, "ocean_enabled_cascade_mask",
                         "app should pack enabled cascades for shader-side isolation");
        require_contains(app_source, "bool ocean_should_update_cascade",
                         "app should centralize per-cascade compute scheduling");
        require_contains(app_source, "if (!ocean_cascade_enabled(config, cascade))",
                         "app should skip disabled cascade compute dispatches");
        require_contains(app_source, "ocean_cascade_update_interval(config, cascade)",
                         "app should honor per-cascade update intervals");
        require_contains(app_source, "lhs.field_precision != rhs.field_precision",
                         "app should recreate ocean GPU resources when field precision changes");
        require_contains(app_source, "lhs.cascade_map_sizes != rhs.cascade_map_sizes",
                         "app should recreate ocean GPU resources when cascade map sizes change");
        require_contains(gpu_resources_source, "VK_FORMAT_R16G16B16A16_SFLOAT",
                         "GPU resources should allocate half precision ocean field textures");
        require_contains(gpu_resources_source, "supports_image_format_features",
                         "GPU resources should validate half precision storage image support");
        require_contains(gpu_resources_source, "fallback_field_",
                         "GPU resources should keep a fallback field for inactive cascades");
        require_contains(gpu_resources_source, "if (!cascade_allocated(cascade))",
                         "GPU resources should skip compute descriptors for inactive cascades");
        require_contains(app_source, "ocean_config_.shape_fade_distance_scale",
                         "app should pass shape fade distance control");
        require_contains(app_source, "ocean_config_.foam_fade_distance_scale",
                         "app should pass foam fade distance control");
        require_contains(app_source, "ocean_config_.self_shadow_strength",
                         "app should pass wave self-shadow strength");
        require_contains(app_source, "ocean_config_.self_shadow_distance",
                         "app should pass wave self-shadow march distance");
        require_contains(app_source, "ocean_config_.self_shadow_bias",
                         "app should pass wave self-shadow bias");
        require_contains(app_source, "ocean_config_.self_shadow_steps",
                         "app should pass wave self-shadow step count");
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
        require_contains(cmake_source, "ocean_reference_pillar.vert",
                         "ocean build should compile the reference pillar vertex shader");
        require_contains(cmake_source, "ocean_reference_pillar.frag",
                         "ocean build should compile the reference pillar fragment shader");
        require_contains(cmake_source, "ocean_spectrum_half.comp",
                         "ocean build should compile half precision spectrum shader variants");
        require_contains(cmake_source, "ocean_unpack_body.glsl",
                         "ocean build should track shared compute shader bodies");
        require_contains(cmake_source, "ocean_shading.glsl",
                         "ocean build should track extracted fragment shading helpers");
        require_contains(cmake_source, "ocean_far_field.glsl",
                         "ocean build should track extracted far-field helpers");
        require_contains(cmake_source, "ocean_foam.glsl",
                         "ocean build should track extracted foam helpers");
        require_contains(cmake_source, "ocean_debug.glsl",
                         "ocean build should track extracted debug helpers");
        require_contains(cmake_source, "CUBEY_OCEAN_DETAIL_FILTER=1",
                         "ocean should compile a static bilinear filter variant");
        require_contains(cmake_source, "CUBEY_OCEAN_DETAIL_FILTER=2",
                         "ocean should compile a static bicubic filter variant");
        require_contains(fragment_shader, "#include \"ocean_shading.glsl\"",
                         "ocean fragment entry point should include shading helpers");
        require_contains(fragment_shader, "#include \"ocean_far_field.glsl\"",
                         "ocean fragment entry point should include far-field helpers");
        require_contains(fragment_shader, "#include \"ocean_foam.glsl\"",
                         "ocean fragment entry point should include foam helpers");
        require_contains(fragment_shader, "RETURN_DETAIL_TEXTURE",
                         "ocean filter controls should preserve direct sampler references");
        require_contains(fragment_shader, "filter_mode == 1",
                         "ocean detail sampling should expose a bilinear ablation");
        require_contains(fragment_shader, "filter_mode == 2",
                         "ocean detail sampling should expose a bicubic ablation");
        require_contains(gpu_resources_source, "surface_pipelines_[index]",
                         "ocean should select filter variants without a fragment uniform branch");
        require_contains(fragment_shader, "#include \"ocean_debug.glsl\"",
                         "ocean fragment entry point should include debug helpers");
        require_contains(cmake_source, "shaders/cubey/atmosphere/atmosphere.frag",
                         "ocean build should compile the shared atmosphere background shader");
        require_contains(cmake_source, "cubey_atmosphere_shader_depends",
                         "ocean build should use the shared atmosphere shader dependency package");
        require_contains(cmake_source, "CUBEY_OCEAN_ATMOSPHERE_SHADER_DEPENDS",
                         "ocean build should track shared atmosphere shader dependencies");
        require_contains(cmake_source, "atmosphere_reflection_prefilter.frag",
                         "ocean build should compile the atmosphere reflection prefilter shader");
        require_contains(cmake_source, "cubey_cloud_layer_shader_sources",
                         "ocean build should use the shared cloud shader source package");
        require_contains(cmake_source, "cubey_cloud_layer_shader_depends",
                         "ocean build should use the shared cloud shader dependency package");
        require_contains(shader_cmake_source, "cloud_composite_post.glsl",
                         "shared cloud shader package should track composite post include");
        require_contains(app_source, "ocean_config_.foam_density",
                         "app should pass foam density as diagnostics push data");
        require_contains(app_source, "ocean_config_.foam_sharpness",
                         "app should pass foam sharpness as diagnostics push data");
        require_contains(app_source, "ocean_config_.far_field_enabled",
                         "app should pass far-field material enable state");
        require_not_contains(app_source, "ocean_config_.far_normal_strength",
                             "app should not pass removed far-field normal carrier");
        require_not_contains(app_source, "ocean_config_.far_streak_scale_m",
                             "app should not pass removed far-field streak carrier");
        require_not_contains(app_source, "ocean_config_.far_whitecap_strength",
                             "app should not pass removed filtered far-whitecap carrier");
        require_contains(app_source, "ocean_config_.far_detail_footprint_start_m",
                         "app should pass far-detail footprint controls");
        require_contains(app_source, "ocean_config_.far_reflection_variation_strength",
                         "app should pass far reflection variation controls");
        require_contains(app_source, "ocean_config_.sun_glitter_width",
                         "app should pass sun glitter corridor controls");
        require_contains(app_source, "ocean_config_.spectral_domains_enabled",
                         "app should pass spectral domain bounds to spectrum generation");
        require_contains(app_source, "kCameraMaxDistance = 8000.0F",
                         "app should allow ocean camera to zoom out for curvature inspection");
        require_contains(app_source, "ocean_config_.planet_radius_scale",
                         "app should scale ocean surface radius for curvature inspection");
        require_contains(ui_source, "&ui.config.shape_anti_repeat_strength",
                         "UI should expose shape anti-repeat as ocean configuration");
        require_contains(ui_source, "&ui.config.detail_anti_repeat_strength",
                         "UI should expose detail anti-repeat as ocean configuration");
        require_contains(ui_source, "&ui.diagnostics.size_reference_enabled",
                         "UI should expose the size reference pillar toggle");
        require_contains(ui_source, "50 m sea-level-centered",
                         "UI should describe the size reference pillar height");
        require_not_contains(ui_source, "Feature Isolation",
                             "UI should not expose retired feature-isolation presets");
        require_contains(ui_source, "&ui.config.surface_shape_strength",
                         "UI should expose global wave presentation strength");
        require_contains(ui_source, "&ui.config.surface_foam_strength",
                         "UI should expose global foam presentation strength");
        require_contains(ui_source, "ui.config.detail_filter",
                         "UI should expose the detail filter ablation");
        require_contains(ui_source, "&ui.config.foam_history_strength",
                         "UI should expose persistent foam history strength");
        require_contains(ui_source, "\"Sea state\"",
                         "UI should expose first-class calm, windy, and stormy presets");
        require_contains(ui_source, "\"Camera\", camera_preset",
                         "UI should expose repeatable camera presets as a compact combo");
        require_contains(ui_source, "ocean_infer_sea_state",
                         "UI should report manual preset-owned edits as custom");
        require_contains(ui_source, "apply_ocean_sea_state",
                         "UI should apply coherent sea-state settings through the config helper");
        require_contains(ui_source, "steady_compute_dispatch_count",
                         "UI should expose a cascade-cost dispatch estimate");
        require_contains(ui_source, "&config.cascade_enabled[index]",
                         "UI should toggle cascade compute and surface contribution");
        require_contains(ui_source, "config.cascade_update_intervals[index]",
                         "UI should expose per-cascade update interval controls");
        require_contains(ui_source, "&config.cascade_map_sizes[index]",
                         "UI should expose per-cascade FFT map size controls");
        require_contains(ui_source, "draw_cascade_controls(ui.config, index)",
                         "UI should keep cascade workload and wave controls together");
        require_contains(ui_source, "std::snprintf(buffer, size, \"C%u\", index)",
                         "UI should use neutral cascade slot labels");
        require_not_contains(ui_source, "GodotOceanWaves port",
                             "UI should not retain port-era cascade labels");
        require_not_contains(ui_source, "core A",
                             "UI should not assign legacy semantic roles to cascade slots");
        require_not_contains(ui_source, "return \"candidate\"",
                             "UI should not assign legacy candidate roles to cascade slots");
        require_contains(ui_source, "OceanCameraPreset::Mid",
                         "UI should expose a mid-distance camera preset");
        require_contains(ui_source, "OceanCameraPreset::High",
                         "UI should expose a high-distance camera preset");
        require_contains(ui_source, "&ui.config.horizon_auto_extent",
                         "UI should expose auto horizon mesh control");
        require_contains(ui_source, "&ui.config.horizon_extent_margin",
                         "UI should expose horizon extent margin control");
        require_contains(ui_source, "&ui.config.horizon_target_near_cell_m",
                         "UI should expose horizon near-cell target control");
        require_contains(ui_source, "&ui.config.horizon_altitude_cell_ratio",
                         "UI should expose altitude-aware horizon mesh thinning");
        require_contains(ui_source, "ui.surface_frame.mesh_config.mesh_cells",
                         "UI should expose the effective auto-horizon mesh resolution");
        require_contains(ui_source, "\"Clip tris\"",
                         "UI performance counters should expose generated clipmap triangles");
        require_contains(ui_source, "\"Draw tris\"",
                         "UI performance counters should expose submitted post-cull triangles");
        require_contains(ui_source, "ui.config.surface_mode",
                         "UI should expose ocean surface mode");
        require_contains(ui_source, "&ui.config.planet_radius_scale",
                         "UI should expose ocean planet radius scale");
        require_contains(ui_source, "&ui.config.curvature_start_ratio",
                         "UI should expose ocean curvature start ratio");
        require_contains(ui_source, "&ui.config.curvature_end_ratio",
                         "UI should expose ocean curvature end ratio");
        require_contains(ui_source, "&ui.config.curvature_strength",
                         "UI should expose ocean curvature strength");
        require_contains(ui_source, "Horizon drop",
                         "UI should expose resolved curvature horizon drop");
        require_contains(ui_source, "&ui.config.self_shadow_strength",
                         "UI should expose wave self-shadow strength");
        require_before(ui_source, "const cubey::host::ScopedImGuiId section_id(\"Lighting\");",
                       "&ui.config.self_shadow_strength",
                       "UI should keep wave self-shadow controls in the lighting section");
        require_before(ui_source, "const cubey::host::ScopedImGuiId section_id(\"Surface\");",
                       "&ui.config.foam_density",
                       "UI should keep foam material controls in the surface section");
        require_contains(ui_source, "\"Scale & LOD\"",
                         "UI should group mesh and far-field controls by scale");
        require_contains(ui_source, "\"Environment\"",
                         "UI should group shared atmosphere and cloud controls");
        require_contains(ui_source, "\"Diagnostics\"",
                         "UI should group inspection and runtime counters");
        require_contains(ui_source, "&ui.config.far_field_enabled",
                         "UI should expose far-field material handoff");
        require_not_contains(ui_source, "&ui.config.far_normal_strength",
                             "UI should not expose removed far-field normal strength");
        require_contains(ui_source, "&ui.config.far_roughness_strength",
                         "UI should expose far-field roughness strength");
        require_contains(ui_source, "&ui.config.far_glint_strength",
                         "UI should expose far-field glint strength");
        require_not_contains(ui_source, "&ui.config.far_whitecap_strength",
                             "UI should not expose removed filtered far-whitecap strength");
        require_contains(ui_source, "&ui.config.far_detail_footprint_start_m",
                         "UI should expose far-detail footprint fade controls");
        require_contains(ui_source, "&ui.config.far_reflection_variation_strength",
                         "UI should expose far reflection variation controls");
        require_contains(ui_source, "&ui.config.sun_glitter_width",
                         "UI should expose sun glitter corridor width");
        require_contains(ui_source, "&ui.config.cloud_shadow_strength",
                         "UI should expose shared cloud shadow strength");
        require_contains(ui_source, "&ui.config.cloud_reflection_strength",
                         "UI should expose cloud reflection strength");
        require_contains(ui_source, "ui.config.cloud_reflection_source",
                         "UI should expose cloud reflection source selection");
        require_contains(ui_source, "&ui.config.cloud_environment_extent",
                         "UI should expose cached cloud environment resolution");
        require_contains(ui_source, "&ui.config.cloud_environment_update_hz",
                         "UI should expose cached cloud environment refresh rate");
        require_contains(ui_source, "&ui.config.cloud_planar_resolution_scale",
                         "UI should expose planar cloud resolution");
        require_contains(ui_source, "&planar_steps", "UI should expose planar cloud view steps");
        require_contains(ui_source, "&ui.config.cloud_planar_guard_band",
                         "UI should expose the planar reflected-view guard band");
        require_not_contains(ui_source, "cloud_shadow_scale_m",
                             "UI should not expose removed procedural cloud scale");
        require_not_contains(ui_source, "cloud_shadow_speed_mps",
                             "UI should not expose removed procedural cloud drift");
        require_contains(ui_source, "&ui.config.self_shadow_distance",
                         "UI should expose wave self-shadow reach");
        require_contains(ui_source, "&ui.config.self_shadow_bias",
                         "UI should expose wave self-shadow bias");
        require_contains(ui_source, "&self_shadow_steps",
                         "UI should expose wave self-shadow step count");
        require_contains(ui_source, "&ui.config.shape_fade_distance_scale",
                         "UI should expose shape fade tuning");
        require_contains(ui_source, "&ui.config.foam_fade_distance_scale",
                         "UI should expose foam fade tuning");
        require_contains(ui_source, "&ui.config.spectral_domains_enabled",
                         "UI should expose spectral domain filtering");
        require_contains(ui_source, "ui.config.field_precision",
                         "UI should expose ocean field precision");
        require_contains(ui_source, "&ui.config.terrain_fields_enabled",
                         "UI should expose optional terrain field influence");
        require_contains(ui_source, "Surface: %s",
                         "UI should expose the active ocean surface frame kind");
        require_contains(ui_source, "ui.surface_frame.local_frame.water_datum_m",
                         "UI should expose the ocean surface datum");
        require_contains(ui_source, "ui.surface_frame.local_frame.world_origin_m",
                         "UI should expose the local tangent frame origin");
        require_contains(ui_source, "ui.surface_frame.local_frame.up",
                         "UI should expose the local tangent frame up axis");
        require_contains(ui_source, "&ui.config.foam_density", "UI should expose foam density");
        require_contains(ui_source, "&ui.config.foam_sharpness", "UI should expose foam sharpness");
        require_contains(ui_source, "Cascade LOD bands",
                         "UI should expose cascade LOD transition diagnostics");
        require_contains(ui_source, "draw_lod_diagnostics(ui.surface_frame)",
                         "UI should use the effective ocean surface frame for LOD diagnostics");
        require_contains(ui_source, "ocean_cascade_lod_band(config, cascade)",
                         "UI should derive LOD diagnostics from shared config helpers");
        require_contains(ui_source, "ocean_cascade_displacement_lod_weight(",
                         "UI should show cascade shape contribution at the horizon");
        require_contains(ui_source, "Horizon wt",
                         "UI should show far-field shape and surface contribution weights");
        require_contains(ui_source, "ui.spectrum_diagnostics.cascades[index]",
                         "UI should consume shared spectrum support diagnostics");
        require_contains(ui_source, "peak %.1f m / safe %.2f-%.1f m",
                         "UI should show peak and safe wavelength support");
        require_contains(ui_source, "Domain min waves",
                         "UI should expose per-slot spectral-domain cutoffs");
        require_contains(fragment_shader, "struct OceanFoamData",
                         "fragment shader should keep foam role diagnostics grouped");
        require_contains(fragment_shader, "OceanFeatureParams",
                         "fragment shader should consume feature-isolation uniforms");
        require_not_contains(fragment_shader, "vec4 material_options",
                             "fragment shader should consume the shared environment directly");
        require_contains(fragment_shader, "vec4 cascade_options",
                         "fragment shader should consume enabled-cascade controls");
        require_contains(fragment_shader, "layout(set = 0, binding = 19)",
                         "fragment shader should bind feature-isolation uniforms after terrain");
        require_contains(vertex_shader, "float ocean_surface_shape_strength",
                         "vertex shader should isolate surface shape contribution");
        require_contains(vertex_shader, "float ocean_shape_fade_distance_scale",
                         "vertex shader should expose shape fade distance control");
        require_contains(fragment_shader, "float ocean_detail_anti_repeat_strength",
                         "fragment shader should isolate far detail anti-repeat contribution");
        require_contains(fragment_shader, "vec4 far_field_options",
                         "fragment shader should consume far-field material controls");
        require_contains(fragment_shader, "float active_far_field_lod_energy",
                         "fragment shader should derive far-field handoff from unresolved LOD");
        require_not_contains(fragment_shader, "ocean_apply_far_field_normal",
                             "fragment shader should not use removed far-field normal carrier");
        require_not_contains(
            fragment_shader, "float ocean_far_whitecap_coverage",
            "fragment shader should not use removed filtered far-whitecap carrier");
        require_not_contains(
            fragment_shader, "sample_filtered_foam_anti_repeat",
            "fragment shader should not keep rejected far-whitecap anti-repeat path");
        require_contains(fragment_shader, "float ocean_far_detail_filter",
                         "fragment shader should filter far normal detail by footprint");
        require_contains(fragment_shader, "float ocean_far_reflection_variation",
                         "fragment shader should add broad far reflection variation");
        require_contains(fragment_shader, "float far_material_energy",
                         "fragment shader should hand filtered detail into material response");
        require_contains(fragment_shader,
                         "color = vec3(far_field_energy, far_material_energy, far_detail_filter)",
                         "far-field debug view should expose active material handoff channels");
        require_contains(fragment_shader, "float ocean_far_sun_glitter",
                         "fragment shader should add a reflected-sun far glitter corridor");
        require_contains(fragment_shader, "float ocean_surface_foam_strength",
                         "fragment shader should isolate surface foam contribution");
        require_not_contains(fragment_shader, "float ocean_atmosphere_reflection_strength",
                             "fragment shader should not retain atmosphere isolation gates");
        require_contains(fragment_shader, "float ocean_foam_fade_distance_scale",
                         "fragment shader should expose foam fade distance control");
        require_contains(fragment_shader, "samplerCube atmosphere_sky_radiance_texture",
                         "fragment shader should sample the atmosphere sky radiance cube");
        require_contains(fragment_shader, "vec3 ocean_sky_radiance",
                         "fragment shader should centralize atmosphere sky radiance sampling");
        require_contains(fragment_shader, "float ocean_sun_light_scale",
                         "fragment shader should derive sun light from atmosphere intensity");
        require_contains(fragment_shader, "float ocean_moon_light_scale",
                         "fragment shader should derive moon light independently through twilight");
        require_contains(fragment_shader, "uniform sampler2D displacement_cascade0_texture",
                         "fragment shader should sample displacement for self-shadowing");
        require_contains(fragment_shader, "float ocean_self_shadow_strength",
                         "fragment shader should expose wave self-shadow strength");
        require_contains(fragment_shader, "float ocean_surface_height",
                         "fragment shader should reconstruct the FFT heightfield");
        require_contains(fragment_shader, "float ocean_wave_self_shadow",
                         "fragment shader should ray-march wave self-shadowing");
        require_contains(fragment_shader, "float elevation_gate = smoothstep(0.025, 0.14",
                         "wave self-shadowing should stay stable as the sun crosses the horizon");
        require_contains(
            fragment_shader, "stable_occlusion",
            "wave self-shadowing should accumulate blockers instead of popping on max hits");
        require_contains(fragment_shader, "sample_ocean_displacement_height",
                         "fragment shader should sample cascade displacement heights");
        require_contains(fragment_shader, "min(reference_shadow, wave_shadow)",
                         "fragment shader should combine pillar and wave direct shadows");
        require_contains(
            fragment_shader, "float ocean_ambient_light_scale",
            "fragment shader should derive ambient fill from atmosphere sky luminance");
        require_contains(fragment_shader, "data.gradient += normal_foam.xy * normal_scale",
                         "fragment shader should preserve normal map scale packing");
        require_contains(fragment_shader, "data.total += weighted_foam;",
                         "fragment shader should preserve accumulated foam sampling");
        require_contains(fragment_shader, "if (!ocean_cascade_enabled(cascade))",
                         "fragment shader should gate normal and foam by inspected cascade");
        require_contains(fragment_shader, "for (uint cascade = 0u; cascade < 5u; ++cascade)",
                         "fragment shader should include all regular normal/foam cascades");
        require_contains(fragment_shader, "return factor > 0.0;",
                         "fragment shader should allow far anti-repeat on any enabled slot");
        require_contains(fragment_shader, "cubey_proc_value_noise_pcg_2d(position * 0.0011",
                         "fragment shader should use stable shared world-space noise weights");
        require_contains(fragment_shader, "float foam_breakup_weight",
                         "fragment shader should use distance-gated world-space foam breakup");
        require_not_contains(fragment_shader, "cascade != 4u",
                             "fragment shader should not mechanically special-case C4 foam");
        require_contains(
            fragment_shader,
            "sample_normal_foam_domain(cascade, position, tile_length, pixels_per_meter",
            "fragment shader should sample secondary normal/foam domains");
        require_contains(
            fragment_shader, "vec2 foam = vec2(1.0)",
            "fragment shader should combine persistent and current foam with a soft union");
        require_contains(fragment_shader, "OCEAN_FAR_ANTI_REPEAT_START",
                         "fragment shader should distance-gate far anti-repeat");
        require_contains(fragment_shader, "float cascade_map_size(uint cascade)",
                         "fragment shader should resolve per-cascade ocean map sizes");
        require_contains(fragment_shader, "float pixels_per_meter = cascade_map_size(cascade)",
                         "fragment shader should use per-cascade ocean map sizes");
        require_contains(fragment_shader, "float cascade_surface_lod_weight",
                         "fragment shader should apply per-cascade normal and foam LOD weights");
        require_contains(fragment_shader, "float cascade_distance_lod_weight",
                         "fragment shader should share distance LOD logic with displacement");
        require_contains(fragment_shader, "float cascade_mesh_lod_weight",
                         "fragment shader should reconstruct mesh-aware displacement LOD");
        require_contains(fragment_shader, "OCEAN_CASCADE_SURFACE_FADE_START_WAVES = 10.0",
                         "fragment shader should mirror the C++ surface LOD start");
        require_contains(fragment_shader, "OCEAN_CASCADE_SURFACE_FADE_END_WAVES = 30.0",
                         "fragment shader should mirror the C++ surface LOD end");
        require_contains(fragment_shader, "active_displacement_lod_weight",
                         "fragment shader should expose active cascade support in LOD debug view");
        require_contains(fragment_shader, "ocean_normal_fade_distance_scale()",
                         "fragment shader should expose reference normal fade distance");
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
        require_contains(fragment_shader, "foam_data.total.x",
                         "fragment shader should derive final foam coverage from total foam");
        require_contains(fragment_shader, "float history_mask",
                         "fragment shader should keep persistent foam as the coverage carrier");
        require_contains(fragment_shader, "vec3 ocean_shaded_foam(",
                         "fragment shader should shade foam as a material");
        require_contains(fragment_shader, "ocean_sun_light_intensity()",
                         "fragment shader should consume atmosphere sun energy directly");
        require_contains(fragment_shader, "ocean_moon_light_intensity()",
                         "fragment shader should keep moon lighting continuous through twilight");
        require_contains(fragment_shader, "ocean_dielectric_fresnel(ndotv)",
                         "fragment shader should use dielectric Fresnel for the surface mix");
        require_contains(fragment_shader, "ocean_ggx_reflected_radiance",
                         "fragment shader should isolate directional specular from the water body");
        require_contains(fragment_shader, "smoothstep(0.15, 1.80, frag_wave.x)",
                         "fragment shader should limit crest transmission to positive crests");
        require_contains(
            fragment_shader, "sun_scatter_tint * ocean_sun_light_color()",
            "fragment shader should tint water transmission with the resolved sun color");
        require_contains(
            fragment_shader, "mix(water_body, reflection, fresnel) + specular",
            "fragment shader should compose body, reflection, and specular explicitly");
        require_contains(fragment_shader, "struct OceanAerialPerspective",
                         "fragment shader should name horizon aerial perspective data");
        require_contains(fragment_shader,
                         "float ocean_horizon_extinction_factor(vec3 view_dir, float dist)",
                         "fragment shader should use view-angle-aware horizon extinction");
        require_contains(fragment_shader, "float ocean_surface_horizon_distance_m()",
                         "fragment shader should expose surface-frame horizon distance");
        require_contains(fragment_shader,
                         "max(ocean_surface_horizon_distance_m(), ocean.mesh_options.z)",
                         "fragment shader should derive horizon extinction from frame metadata");
        require_contains(fragment_shader, "OceanAerialPerspective ocean_horizon_aerial_perspective",
                         "fragment shader should isolate horizon aerial perspective lookup");
        require_contains(fragment_shader, "water * perspective.transmittance",
                         "fragment shader should compose water through horizon transmittance");
        require_contains(fragment_shader, "color = vec3(foam_coverage);",
                         "fragment shader should keep debug foam view as presentation coverage");
        require_contains(fragment_shader, "color = vec3(foam_current);",
                         "fragment shader should expose current foam source debug view");
        require_contains(fragment_shader, "color = vec3(foam_persistent);",
                         "fragment shader should expose accumulated foam history debug view");
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
        require_contains(fragment_shader, "const uint OCEAN_VIEW_SPECULAR = 33u",
                         "fragment shader should expose specular diagnostics");
        require_contains(fragment_shader, "ocean_specular_aa_roughness(normal, roughness)",
                         "fragment shader should filter close-range specular normal variance");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_FOAM_RAW = 16u",
                         "fragment shader should expose raw foam diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_TERRAIN_DEPTH = 18u",
                         "fragment shader should expose terrain depth diagnostics");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_CURVATURE = 21u",
                         "fragment shader should expose curvature diagnostics");
        require_contains(fragment_shader, "debug_curvature_color",
                         "fragment shader should color curved far-surface diagnostics");
        require_contains(fragment_shader, "frag_surface_up + vec3",
                         "fragment shader should start normals from mapped surface up");
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
        require_contains(gpu_resources_source, "kOceanSurfaceFeatureUniformBinding",
                         "surface descriptors should expose feature-isolation uniforms");
        require_contains(gpu_header_source, "OceanSurfaceFeatureUniforms",
                         "GPU resource header should define packed feature-isolation uniforms");
        require_contains(gpu_header_source, "sun_light_direction_intensity",
                         "GPU resource header should carry independent sun lighting");
        require_contains(gpu_header_source, "moon_light_direction_intensity",
                         "GPU resource header should carry independent moon lighting");
        require_contains(gpu_header_source, "sizeof(float) * 92U",
                         "GPU resource header should size active feature and lighting uniforms");
        require_contains(gpu_header_source, "self_shadow_options",
                         "GPU resource header should pack wave self-shadow controls");
        require_contains(gpu_header_source, "surface_frame_options",
                         "GPU resource header should pack ocean surface frame metadata");
        require_contains(gpu_header_source, "surface_curve_options",
                         "GPU resource header should pack curved surface metadata");
        require_contains(gpu_header_source, "cloud_shadow_world_to_uv_x",
                         "GPU resource header should pack cloud shadow projection rows");
        require_contains(gpu_resources_source, "kOceanSurfaceCloudShadowBinding",
                         "surface descriptors should expose shared cloud transmittance");
        require_contains(gpu_resources_source, "update_cloud_shadow_descriptor",
                         "surface descriptors should support transient cloud shadow products");
        require_contains(gpu_resources_source,
                         "VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT",
                         "surface displacement descriptors should be visible to self-shadowing");
        require_contains(gpu_header_source, "FrameUniformBuffer<OceanSurfaceFeatureUniforms>",
                         "GPU resources should own feature-isolation frame uniforms");
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
        require_contains(
            app_source,
            "make_ocean_diagnostic_terrain_fields(const OceanConfig& config, float water_datum_m)",
            "ocean diagnostic terrain fields should accept the active surface datum");
        require_contains(app_source, ".sea_level_m = water_datum_m",
                         "ocean diagnostic terrain fields should publish the active surface datum");
        require_contains(app_source, "create_uploaded_terrain_ocean_field_texture",
                         "ocean app should upload the shared terrain-ocean field texture");
        require_contains(app_source, "FrameUniformBuffer<OceanTerrainFieldUniforms>",
                         "ocean app should upload terrain-ocean metadata per frame");
        require_contains(app_source, "AtmosphereBackgroundFrame atmosphere_background_",
                         "ocean app should own the shared atmosphere background frame");
        require_contains(app_source, "create_atmosphere_background_cached_textures",
                         "ocean app should reuse shared cached atmosphere atlas textures");
        require_contains(app_source, "record_atmosphere_background",
                         "ocean app should draw the shared atmosphere background");
        require_contains(app_source, "atmosphere_environment_for_surface_frame",
                         "ocean app should resolve background atmosphere from the surface frame");
        require_contains(app_source, "environment.camera_altitude_km",
                         "ocean app should publish surface-frame camera altitude to atmosphere");
        require_contains(
            app_source, "atmosphere_environment_frame_uniforms",
            "ocean app should build per-frame atmosphere uniforms without mutating UI config");
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
            app_source, "atmosphere_runtime_.advance",
            "ocean should advance coherent atmosphere and cloud environments together");
        require_not_contains(
            vertex_shader, "vec4 sun_direction;",
            "ocean surface push constants should not duplicate feature lighting uniforms");
        require_contains(
            fragment_shader, "ocean_features.sun_light_direction_intensity.w",
            "ocean surface shader should read continuous sun intensity from feature uniforms");
        require_contains(
            fragment_shader, "ocean_features.moon_light_direction_intensity.w",
            "ocean surface shader should read continuous moon intensity from feature uniforms");
        require_contains(
            fragment_shader, "samplerCube atmosphere_reflection_texture",
            "ocean surface shader should sample the previous atmosphere reflection probe");
        require_contains(
            fragment_shader, "samplerCube atmosphere_reflection_current_texture",
            "ocean surface shader should sample the current atmosphere reflection probe");
        require_contains(fragment_shader, "atmosphere_environment_options.x",
                         "ocean surface shader should crossfade coherent atmosphere probes");
        require_contains(fragment_shader, "ocean_environment_reflection",
                         "ocean surface shader should isolate atmosphere reflection lookup");
        require_contains(fragment_shader, "ocean_above_horizon_reflection_direction",
                         "ocean surface shader should keep reflected sky rays above the horizon");
        require_contains(fragment_shader, "sampler2D cloud_shadow_transmittance_texture",
                         "ocean surface shader should sample shared cloud transmittance");
        require_contains(fragment_shader, "ocean_cloud_shadow_transmittance",
                         "ocean surface shader should isolate cloud transmittance lookup");
        require_contains(fragment_shader, "samplerCube cloud_environment_previous_texture",
                         "ocean surface shader should sample the previous cached environment");
        require_contains(fragment_shader, "samplerCube cloud_environment_current_texture",
                         "ocean surface shader should sample the current cached environment");
        require_contains(fragment_shader, "sampler2D cloud_planar_reflection_texture",
                         "ocean surface shader should sample the reflected cloud product");
        require_contains(fragment_shader, "ocean_planar_cloud_reflection",
                         "ocean surface shader should isolate planar cloud projection");
        require_contains(fragment_shader, "const int OCEAN_CLOUD_REFLECTION_SOURCE_CACHED = 0",
                         "ocean shader should declare the cached source ID");
        require_contains(fragment_shader, "const int OCEAN_CLOUD_REFLECTION_SOURCE_PLANAR = 1",
                         "ocean shader should declare the planar source ID");
        require_contains(fragment_shader, "float(OCEAN_CLOUD_REFLECTION_SOURCE_PLANAR)",
                         "ocean shader should clamp source IDs through the planar contract");
        require_contains(fragment_shader, "roughness * roughness * max_lod + 0.25",
                         "planar cloud reflection should use roughness-filtered mips");
        require_contains(fragment_shader, "planar_reflection_sample.visibility",
                         "planar cloud reflection should expose a bounded validity weight");
        require_contains(fragment_shader, "cached_sky_reflection",
                         "cached fallback should preserve clear below-horizon facets");
        require_contains(fragment_shader, "smoothstep(0.0, 0.06, edge_distance)",
                         "planar cloud reflection should soften its guarded product edge");
        require_contains(
            fragment_shader, "ocean_cached_cloud_reflection",
            "ocean surface shader should select a roughness-filtered cached environment");
        require_contains(fragment_shader,
                         "filtered_roughness * filtered_roughness * max_lod + 0.25",
                         "cached cloud reflections should bias toward stable broad response");
        require_not_contains(fragment_shader, "current_view_detail",
                             "ocean shader should remove the retired hybrid handoff");
        require_not_contains(app_source, "OceanCloudReflectionSource::Hybrid",
                             "ocean runtime should remove retired hybrid scheduling");
        require_contains(fragment_shader, "const uint OCEAN_VIEW_CLOUD_REFLECTION = 27u",
                         "ocean surface shader should expose cloud reflection diagnostics");
        require_contains(fragment_shader, "sky_facet_visibility",
                         "ocean cloud reflections should reject below-horizon wave facets");
        require_contains(
            fragment_shader, "planar_reflection_sample.transmittance * clear_reflection",
            "ocean clouds should composite over the matching filtered environment reflection");
        require_contains(fragment_shader,
                         "planar_reflection_sample.transmittance * clear_sky_reflection",
                         "ocean cloud diagnostics should composite over the matching analytic sky");
        require_not_contains(
            fragment_shader, "cloud_reflection_sample.delta",
            "ocean cloud reflections should not apply signed deltas across different backgrounds");
        require_not_contains(fragment_shader, "cloud_shadow_scale_m",
                             "ocean surface shader should remove procedural cloud scale");
        require_not_contains(fragment_shader, "cloud_shadow_speed_mps",
                             "ocean surface shader should remove procedural cloud drift");
        require_contains(app_source, "clouds.declare_shadow_product",
                         "ocean should request the shared projected cloud shadow product");
        require_contains(app_source, "atmosphere_runtime_.clouds()",
                         "ocean should consume clouds owned by the atmosphere runtime");
        require_not_contains(app_source, "CloudLayerRuntime cloud_runtime_",
                             "ocean should not retain a duplicate surface cloud runtime");
        require_not_contains(app_source, "CloudEnvironmentRuntime cloud_environment_runtime_",
                             "ocean should not retain a duplicate cached cloud runtime");
        require_contains(app_source, "ocean_config_.cloud_shadow_strength > 0.0F",
                         "ocean should skip the cloud shadow pass when coupling is disabled");
        require_contains(app_source, "render_view_ == OceanRenderView::CloudShadow",
                         "ocean should preserve raw cloud shadow diagnostics at zero strength");
        require_contains(app_source, "ocean_cloud_shadow_half_extent_m",
                         "ocean cloud shadows should use a camera-scale local projection");
        require_before(app_source, "clouds.declare_shadow_product",
                       "graph.add_pass(\"ocean scene\"",
                       "ocean should generate cloud transmittance before drawing water");
        require_contains(app_source, "render_view_ != OceanRenderView::Background",
                         "ocean should expose an unperturbed background-only ablation");
        require_contains(app_source, "record_ocean_profile_metrics",
                         "ocean should export headless mesh metrics alongside GPU spans");
        require_contains(app_source, "\"submitted_triangles\"",
                         "ocean profile metrics should include post-cull triangle cost");
        require_contains(app_source, "ocean_config_.cloud_reflection_strength > 0.0F",
                         "ocean should skip cloud reflection work when coupling is disabled");
        require_contains(app_source, "cloud_planar_reflection_.record",
                         "ocean should record a dedicated reflected cloud view");
        require_contains(app_source, "ocean.cloud_planar_reflection",
                         "ocean should profile the reflected cloud view independently");
        require_contains(app_source, "diagnostics_.size_reference_enabled ? 1.0F : 0.0F",
                         "ocean app should pass reference shadow enable through feature uniforms");
        require_contains(fragment_shader, "ocean_reference_shadow_enabled",
                         "ocean surface shader should read reference shadow enable from features");
        require_contains(
            fragment_shader, "ocean_reference_shadow_strength",
            "ocean surface shader should read reference shadow strength from features");
        require_contains(app_source, "make_ocean_reference_pillar_mesh",
                         "ocean app should build a meter-banded scale reference mesh");
        require_contains(app_source, "kReferencePillarMinYMeters = -25.0F",
                         "ocean scale reference should extend 25 m below sea level");
        require_contains(app_source, "kReferencePillarMaxYMeters = 25.0F",
                         "ocean scale reference should extend 25 m above sea level");
        require_contains(app_source, "reference_pillar_marker_color",
                         "ocean scale reference should prioritize colored meter markers");
        require_contains(app_source, "reference_pillar_marker_half_height",
                         "ocean scale reference should keep 5 m and 10 m markers readable");
        require_contains(app_source, "kReferencePillarMarkerHalfWidthMeters",
                         "ocean scale reference markers should protrude from the white body");
        require_contains(
            app_source, "pillar_position",
            "ocean scale reference should rotate the pillar for readable shaded faces");
        require_contains(app_source, "pillar_u_normal",
                         "ocean scale reference should use flat face normals for basic shading");
        require_contains(app_source, "kReferencePillarHalfWidthMeters = 0.50F",
                         "ocean scale reference should use a 1 m square footprint");
        require_contains(fragment_shader, "ocean_reference_pillar_shadow",
                         "ocean surface shader should cast a simple analytic pillar shadow");
        require_contains(fragment_shader, "ocean_reference_shadow_axis",
                         "ocean surface shader should use slab tests for the pillar shadow");
        require_contains(fragment_shader, "shadowed_direct_light",
                         "ocean surface shader should apply pillar shadow to direct lighting");
        require_contains(app_source, "create_reference_pillar_resources",
                         "ocean app should create reference pillar mesh and pipeline resources");
        require_contains(app_source, "record_reference_pillar_draw",
                         "ocean app should draw the scale reference pillar in the scene pass");
        require_contains(app_source, "render_view_ != OceanRenderView::Final",
                         "ocean app should keep the pillar out of debug views");
        require_contains(app_source, "OceanSurfaceFrame ocean_surface_frame()",
                         "ocean app should centralize per-frame ocean surface state");
        require_contains(app_source, "surface_frame.projection_far_plane_m",
                         "ocean projection should derive far plane from surface frame");
        require_contains(app_source, "reference_pillar_mesh_",
                         "ocean app should retain the reference pillar mesh as reusable geometry");
        require_contains(pillar_vertex_shader, "pillar.view_projection",
                         "reference pillar vertex shader should use the ocean camera matrix");
        require_contains(pillar_fragment_shader, "pillar.light_direction",
                         "reference pillar fragment shader should use atmosphere light direction");
        require_contains(
            pillar_fragment_shader, "ambient + diffuse",
            "reference pillar fragment shader should use basic ambient plus diffuse shading");

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
