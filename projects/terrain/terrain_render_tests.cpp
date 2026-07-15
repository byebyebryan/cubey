#include "terrain_backdrop_camera.h"
#include "terrain_clipmap.h"
#include "terrain_config.h"
#include "terrain_environment_gpu.h"
#include "terrain_material_tiles.h"
#include "terrain_surface_controller.h"

#include <cubey/core/run_config.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_near(float actual, float expected, float tolerance, std::string_view message) {
    require(std::abs(actual - expected) <= tolerance, message);
}

float independent_backdrop_foreground_margin(
    const cubey::projects::terrain::TerrainSourceParameters& source,
    const cubey::projects::terrain::TerrainBackdropCameraPlan& plan, float vertical_scale,
    float aspect_ratio) {
    constexpr std::array<float, 3> ndc_x_values{-1.0F, 0.0F, 1.0F};
    constexpr float vertical_fov = 40.0F * std::numbers::pi_v<float> / 180.0F;
    constexpr float conservative_pitch = -2.0F * std::numbers::pi_v<float> / 180.0F;
    const auto rotation = cubey::math::angle_axis_quat(plan.yaw_radians, {0.0F, 1.0F, 0.0F}) *
                          cubey::math::angle_axis_quat(conservative_pitch, {1.0F, 0.0F, 0.0F});
    const float tan_half_fov = std::tan(vertical_fov * 0.5F);
    float minimum_margin = std::numeric_limits<float>::infinity();
    for (const float ndc_x : ndc_x_values) {
        const cubey::math::Vec3 ray =
            rotation * cubey::math::Vec3{ndc_x * tan_half_fov * aspect_ratio, -tan_half_fov, -1.0F};
        const float horizontal_length = std::sqrt(ray.x * ray.x + ray.z * ray.z);
        const cubey::math::Vec2 direction{ray.x / horizontal_length, ray.z / horizontal_length};
        const float vertical_slope = ray.y / horizontal_length;
        for (float distance = 25.0F; distance <= 300.0F; distance += 25.0F) {
            const auto sample = cubey::projects::terrain::sample_terrain(
                source, {.world_xz = plan.anchor_xz + direction * distance});
            const float ray_height = plan.transform.translation.y + distance * vertical_slope;
            minimum_margin =
                std::min(minimum_margin, ray_height - sample.height_m * vertical_scale);
        }
    }
    return minimum_margin;
}

std::uint32_t independent_near_frame_occluded_ray_count(
    const cubey::projects::terrain::TerrainSourceParameters& source,
    const cubey::projects::terrain::TerrainBackdropCameraPlan& plan, float vertical_scale) {
    constexpr std::array<float, 5> ndc_x_values{-1.0F, -0.5F, 0.0F, 0.5F, 1.0F};
    constexpr std::array<float, 3> ndc_y_values{0.0F, 0.35F, 0.70F};
    constexpr float vertical_fov = 40.0F * std::numbers::pi_v<float> / 180.0F;
    const auto rotation = cubey::math::angle_axis_quat(plan.yaw_radians, {0.0F, 1.0F, 0.0F}) *
                          cubey::math::angle_axis_quat(plan.pitch_radians, {1.0F, 0.0F, 0.0F});
    const float tan_half_fov = std::tan(vertical_fov * 0.5F);
    std::uint32_t occluded_ray_count = 0U;
    for (const float ndc_y : ndc_y_values) {
        for (const float ndc_x : ndc_x_values) {
            const cubey::math::Vec3 ray =
                rotation * cubey::math::Vec3{ndc_x * tan_half_fov * plan.aspect_ratio,
                                             ndc_y * tan_half_fov, -1.0F};
            const float horizontal_length = std::sqrt(ray.x * ray.x + ray.z * ray.z);
            const cubey::math::Vec2 direction{ray.x / horizontal_length, ray.z / horizontal_length};
            const float vertical_slope = ray.y / horizontal_length;
            for (float distance = 100.0F; distance <= plan.target_distance_m * 0.75F;
                 distance += 50.0F) {
                const auto sample = cubey::projects::terrain::sample_terrain(
                    source, {.world_xz = plan.anchor_xz + direction * distance});
                const float ray_height = plan.transform.translation.y + distance * vertical_slope;
                if (sample.height_m * vertical_scale >= ray_height) {
                    ++occluded_ray_count;
                    break;
                }
            }
        }
    }
    return occluded_ray_count;
}

void require_near_frame_contract(const cubey::projects::terrain::TerrainSourceParameters& source,
                                 const cubey::projects::terrain::TerrainBackdropCameraPlan& plan,
                                 float vertical_scale) {
    constexpr float ray_count = 15.0F;
    require(plan.near_frame_occluded_ray_count <= 2U,
            "terrain camera should reject excessive near-frame occupancy");
    require_near(plan.near_frame_test_distance_m, plan.target_distance_m * 0.75F, 0.001F,
                 "terrain camera should test the near three quarters of its target distance");
    require_near(plan.near_frame_occupancy_ratio,
                 static_cast<float>(plan.near_frame_occluded_ray_count) / ray_count, 0.000001F,
                 "terrain camera should report its near-frame occupancy ratio");
    require(plan.near_frame_occluded_ray_count ==
                independent_near_frame_occluded_ray_count(source, plan, vertical_scale),
            "terrain camera occupancy should match an independent ray test");
    require(plan.near_frame_occluded_ray_count == 0U
                ? plan.near_frame_nearest_hit_distance_m == 0.0F
                : plan.near_frame_nearest_hit_distance_m >= 100.0F &&
                      plan.near_frame_nearest_hit_distance_m <= plan.near_frame_test_distance_m,
            "terrain camera should report a bounded nearest near-frame hit");
}

void test_runtime_config_defaults_to_the_v1_scene() {
    const cubey::RunConfig run_config{};
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.source.preset == cubey::projects::terrain::TerrainPreset::Mountain,
            "terrain runtime should default to mountain preset");
    require(config.source.version == cubey::projects::terrain::TerrainSourceVersion::V1,
            "terrain runtime should default to source v1");
    require(config.source.weathering == cubey::projects::terrain::TerrainWeatheringMode::Local,
            "terrain runtime should default to local weathering");
    require(config.camera == cubey::projects::terrain::TerrainCameraPreset::Oblique,
            "terrain runtime should default to oblique camera");
    require(config.debug_view == cubey::projects::terrain::TerrainDebugView::Surface,
            "terrain runtime should default to surface view");
    require(config.presentation == cubey::projects::terrain::TerrainPresentationMode::Standard,
            "terrain runtime should default to standard presentation");
    require(config.render_path == cubey::projects::terrain::TerrainRenderPath::Control,
            "terrain runtime should default to control rendering");
    require(config.surface_detail == cubey::projects::terrain::TerrainSurfaceDetail::Tile,
            "terrain runtime should default to current tile detail");
    require_near(config.target_edge_px, 4.0F, 0.0F,
                 "terrain runtime should default to four-pixel quality edges");
    require_near(config.near_cell_size_m, 2.0F, 0.0F,
                 "terrain runtime should default to two-meter near cells");
    require(config.lod_levels == 8U && config.cells_per_axis == 128U,
            "terrain runtime should use the v1 clipmap dimensions");
}

void test_source_v2_extends_only_mountain_detail_band() {
    using namespace cubey::projects::terrain;
    const TerrainSourceParameters v1 = resolve_terrain_source_parameters({
        .seed = 9012U,
        .preset = TerrainPreset::Mountain,
    });
    const TerrainSourceParameters v2 = resolve_terrain_source_parameters({
        .seed = 9012U,
        .preset = TerrainPreset::Mountain,
        .version = TerrainSourceVersion::V2,
    });
    require(v1.macro.seed == v2.macro.seed && v1.macro.octaves == v2.macro.octaves &&
                v1.structure.seed == v2.structure.seed &&
                v1.structure.octaves == v2.structure.octaves &&
                v1.height_scale_m == v2.height_scale_m && v1.elevation_power == v2.elevation_power,
            "terrain source v2 should preserve v1 macro structure and elevation");
    require(v2.detail.octaves == 8U, "terrain source v2 should expose eight detail octaves");
    require_near(v2.detail.lacunarity, 2.03F, 0.0F,
                 "terrain source v2 should preserve detail lacunarity");
    require_near(v2.detail.gain, 0.52F, 0.0F, "terrain source v2 should preserve detail gain");
    require_near(v2.detail.ridge_mix, 0.24F, 0.0F,
                 "terrain source v2 should preserve detail ridge mix");
    require_near(v2.detail_weight, 0.16F, 0.0F,
                 "terrain source v2 should increase detail composition weight");

    bool rejected = false;
    try {
        (void)resolve_terrain_source_parameters({
            .preset = TerrainPreset::Upland,
            .version = TerrainSourceVersion::V2,
        });
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain source v2 should reject unsupported presets");
}

void test_runtime_config_parses_source_v2() {
    cubey::RunConfig run_config{};
    run_config.terrain.source_version = "v2";
    run_config.terrain.render_path = "quality";
    run_config.terrain.surface_detail = "layered";
    run_config.terrain.target_edge_px = 6.0F;
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.source.version == cubey::projects::terrain::TerrainSourceVersion::V2,
            "terrain runtime should parse source v2");
    require(config.render_path == cubey::projects::terrain::TerrainRenderPath::Quality,
            "terrain runtime should parse quality rendering");
    require(config.surface_detail == cubey::projects::terrain::TerrainSurfaceDetail::Layered,
            "terrain runtime should parse layered surface detail");
    require_near(config.target_edge_px, 6.0F, 0.0F,
                 "terrain runtime should parse the quality edge target");

    cubey::RunConfig invalid;
    invalid.terrain.surface_detail = "layered";
    bool rejected = false;
    try {
        (void)cubey::projects::terrain::terrain_runtime_config_from_run_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "layered surface detail should reject the control geometry path");
}

void test_runtime_config_parses_source_v2_1() {
    cubey::RunConfig run_config{};
    run_config.terrain.source_version = "v2.1";
    run_config.terrain.render_path = "quality";
    run_config.terrain.surface_detail = "layered";
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.source.version == cubey::projects::terrain::TerrainSourceVersion::V2_1,
            "terrain runtime should parse source v2.1");
}

void test_runtime_config_parses_source_v3() {
    cubey::RunConfig run_config{};
    run_config.terrain.source_version = "v3";
    run_config.terrain.render_path = "quality";
    run_config.terrain.surface_detail = "layered";
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.source.version == cubey::projects::terrain::TerrainSourceVersion::V3,
            "terrain runtime should parse source v3");
}

void test_source_v3_component_views_require_source_v3() {
    using namespace cubey::projects::terrain;
    require(terrain_debug_view_from_name("range-support") == TerrainDebugView::SourceRange &&
                terrain_debug_view_from_name("massif") == TerrainDebugView::SourceMassif &&
                terrain_debug_view_from_name("valley-delta") == TerrainDebugView::SourceValley &&
                terrain_debug_view_from_name("ridge-delta") == TerrainDebugView::SourceRidge &&
                terrain_debug_view_from_name("meso-delta") == TerrainDebugView::SourceMeso,
            "terrain runtime should parse source v3 component views");

    cubey::RunConfig valid{};
    valid.terrain.source_version = "v3";
    valid.debug_view = "source-ridge";
    const TerrainRuntimeConfig config = terrain_runtime_config_from_run_config(valid);
    require(config.debug_view == TerrainDebugView::SourceRidge,
            "terrain source v3 should accept component views");

    cubey::RunConfig invalid{};
    invalid.debug_view = "source-ridge";
    bool rejected = false;
    try {
        (void)terrain_runtime_config_from_run_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain source component views should reject legacy sources");
}

void test_ground_camera_and_shape_diagnostics_parse() {
    cubey::RunConfig run_config{};
    run_config.terrain.camera_preset = "ground";
    run_config.debug_view = "clay";
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.camera == cubey::projects::terrain::TerrainCameraPreset::Ground,
            "terrain runtime should parse the ground camera");
    require(config.debug_view == cubey::projects::terrain::TerrainDebugView::Clay,
            "terrain runtime should parse the clay diagnostic");
    require(cubey::projects::terrain::terrain_camera_is_surface(config.camera),
            "terrain ground camera should use surface traversal");
    require_near(cubey::projects::terrain::terrain_camera_clearance_m(config.camera), 2.0F, 0.0F,
                 "terrain ground camera should use eye-level clearance");
    require_near(cubey::projects::terrain::terrain_camera_traversal_speed_mps(config.camera), 12.0F,
                 0.0F, "terrain ground camera should use walking-scale traversal");
    require(cubey::projects::terrain::terrain_debug_view_from_name("shadow") ==
                cubey::projects::terrain::TerrainDebugView::Shadow,
            "terrain runtime should parse the shadow diagnostic");
    require(cubey::projects::terrain::terrain_debug_view_from_name("aerial") ==
                cubey::projects::terrain::TerrainDebugView::AerialTransmittance,
            "terrain runtime should parse the aerial diagnostic alias");
    require(cubey::projects::terrain::terrain_debug_view_from_name("normals") ==
                cubey::projects::terrain::TerrainDebugView::Normal,
            "terrain runtime should parse the final-normal diagnostic alias");
    require(cubey::projects::terrain::terrain_debug_view_from_name("macro-normal") ==
                cubey::projects::terrain::TerrainDebugView::ClassificationNormal,
            "terrain runtime should parse the classification-normal diagnostic alias");
    require(cubey::projects::terrain::terrain_debug_view_from_name("materials") ==
                cubey::projects::terrain::TerrainDebugView::MaterialWeights,
            "terrain runtime should parse the material-weight diagnostic alias");
    require(cubey::projects::terrain::terrain_debug_view_from_name("ambient") ==
                cubey::projects::terrain::TerrainDebugView::AmbientVisibility,
            "terrain runtime should parse the ambient-visibility diagnostic alias");
    require(cubey::projects::terrain::terrain_debug_view_from_name("roughness") ==
                    cubey::projects::terrain::TerrainDebugView::MaterialRoughness &&
                cubey::projects::terrain::terrain_debug_view_from_name("blend-height") ==
                    cubey::projects::terrain::TerrainDebugView::MaterialHeight &&
                cubey::projects::terrain::terrain_debug_view_from_name("cavity") ==
                    cubey::projects::terrain::TerrainDebugView::MaterialCavity,
            "terrain runtime should parse layered material diagnostics");
}

void test_backdrop_camera_configuration() {
    cubey::RunConfig run_config;
    run_config.terrain.camera_preset = "backdrop";
    run_config.terrain.backdrop_mode = "grounded";
    run_config.terrain.backdrop_azimuth_degrees = -90.0F;
    run_config.terrain.backdrop_orbit_radius_m = 125.0F;
    run_config.terrain.backdrop_elevation_degrees = 24.0F;
    run_config.terrain.backdrop_minimum_visible_distance_m = 1'750.0F;
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.camera == cubey::projects::terrain::TerrainCameraPreset::Backdrop,
            "terrain runtime should parse the backdrop camera");
    require(config.backdrop_mode == cubey::projects::terrain::TerrainBackdropStageMode::Grounded &&
                config.backdrop_azimuth_radians.has_value() &&
                config.backdrop_orbit_radius_m == 125.0F &&
                config.backdrop_elevation_radians.has_value() &&
                config.backdrop_minimum_visible_distance_m == 1'750.0F,
            "terrain runtime should parse optional backdrop stage controls");
    require_near(config.backdrop_azimuth_radians.value(), -0.5F * std::numbers::pi_v<float>,
                 0.000001F, "terrain runtime should convert backdrop azimuth to radians");
    require_near(config.backdrop_elevation_radians.value(),
                 24.0F * std::numbers::pi_v<float> / 180.0F, 0.000001F,
                 "terrain runtime should convert backdrop elevation to radians");
    require(!cubey::projects::terrain::terrain_camera_is_surface(config.camera),
            "terrain backdrop camera should use unrestricted orbit control");
    require(!cubey::projects::terrain::terrain_camera_advances_headless(config.camera),
            "terrain backdrop camera should remain static in headless captures");
    require_near(cubey::projects::terrain::terrain_camera_clearance_m(config.camera), 150.0F, 0.0F,
                 "terrain backdrop clearance should remain available to legacy diagnostics");
    require_near(cubey::projects::terrain::terrain_camera_fovy_radians(config.camera),
                 40.0F * std::numbers::pi_v<float> / 180.0F, 0.000001F,
                 "terrain backdrop camera should use a restrained field of view");

    run_config.terrain.camera_preset = "backdrop-stage";
    const auto stage = cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(stage.camera == cubey::projects::terrain::TerrainCameraPreset::BackdropStage &&
                cubey::projects::terrain::terrain_camera_is_backdrop(stage.camera) &&
                !cubey::projects::terrain::terrain_camera_is_surface(stage.camera),
            "terrain runtime should expose a dedicated orbiting backdrop stage view");

    run_config.terrain.camera_preset = "midground";
    const auto midground =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(midground.camera == cubey::projects::terrain::TerrainCameraPreset::Midground,
            "terrain runtime should parse the midground camera");
    require(cubey::projects::terrain::terrain_camera_is_surface(midground.camera) &&
                cubey::projects::terrain::terrain_camera_advances_headless(midground.camera),
            "terrain midground camera should advance during headless video captures");
    require_near(cubey::projects::terrain::terrain_camera_clearance_m(midground.camera), 150.0F,
                 0.0F, "terrain midground camera should retain backdrop clearance");
}

void test_backdrop_presentation_and_coverage_debug_parse() {
    cubey::RunConfig run_config;
    run_config.terrain.presentation = "backdrop";
    run_config.debug_view = "vegetation-coverage";
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.presentation == cubey::projects::terrain::TerrainPresentationMode::Backdrop,
            "terrain runtime should parse backdrop presentation");
    require(config.debug_view == cubey::projects::terrain::TerrainDebugView::VegetationCoverage,
            "terrain runtime should parse vegetation coverage diagnostics");
    require(cubey::projects::terrain::terrain_presentation_mode_name(config.presentation) ==
                "backdrop",
            "terrain presentation should retain its canonical name");
}

void test_backdrop_planner_is_deterministic_and_clear() {
    constexpr std::array presets{
        cubey::projects::terrain::TerrainPreset::Mountain,
        cubey::projects::terrain::TerrainPreset::Upland,
        cubey::projects::terrain::TerrainPreset::Plains,
    };
    constexpr std::array<std::uint64_t, 6> seeds{
        0U, 1U, 42U, 9012U, 12345U, std::numeric_limits<std::uint64_t>::max()};
    for (const auto preset : presets) {
        for (const std::uint64_t seed : seeds) {
            const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
                .seed = seed,
                .preset = preset,
                .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
            });
            const auto first = cubey::projects::terrain::plan_terrain_backdrop_camera(source);
            const auto second = cubey::projects::terrain::plan_terrain_backdrop_camera(source);
            require_near(first.anchor_xz.x, second.anchor_xz.x, 0.0F,
                         "terrain backdrop anchor x should be deterministic");
            require_near(first.anchor_xz.y, second.anchor_xz.y, 0.0F,
                         "terrain backdrop anchor z should be deterministic");
            require_near(first.yaw_radians, second.yaw_radians, 0.0F,
                         "terrain backdrop heading should be deterministic");
            require_near(first.score, second.score, 0.0F,
                         "terrain backdrop score should be deterministic");
            require(first.near_frame_occluded_ray_count == second.near_frame_occluded_ray_count &&
                        first.near_frame_occupancy_ratio == second.near_frame_occupancy_ratio,
                    "terrain backdrop occupancy should be deterministic");
            require(std::isfinite(first.transform.translation.x) &&
                        std::isfinite(first.transform.translation.y) &&
                        std::isfinite(first.transform.translation.z) &&
                        std::isfinite(first.score) && first.score >= 0.0F,
                    "terrain backdrop plan should remain finite");
            require(first.target_distance_m >= 3200.0F && first.target_distance_m <= 6400.0F,
                    "terrain backdrop target should stay in the far presentation tier");
            const auto anchor_sample =
                cubey::projects::terrain::sample_terrain(source, {.world_xz = first.anchor_xz});
            require(first.camera_clearance_m >= 149.999F,
                    "terrain backdrop camera should preserve its minimum clearance");
            require_near(first.transform.translation.y,
                         anchor_sample.height_m + first.camera_clearance_m, 0.001F,
                         "terrain backdrop camera should preserve final-source clearance");
            require(first.foreground_clear_distance_m == 300.0F &&
                        first.foreground_min_margin_m >= 9.999F,
                    "terrain backdrop camera should preserve the foreground contract");
            require(independent_backdrop_foreground_margin(source, first, 1.0F,
                                                           first.aspect_ratio) >= 9.998F,
                    "terrain backdrop camera should independently clear the lower frustum");
            require(first.pitch_radians >= -2.0F * std::numbers::pi_v<float> / 180.0F &&
                        first.pitch_radians <= 12.0F * std::numbers::pi_v<float> / 180.0F,
                    "terrain backdrop pitch should remain in the presentation range");
            require_near_frame_contract(source, first, 1.0F);

            const auto midground = cubey::projects::terrain::plan_terrain_backdrop_camera(
                source, 1.0F, 16.0F / 9.0F,
                cubey::projects::terrain::TerrainBackdropCameraProfile::Midground);
            require(midground.target_distance_m == 1600.0F,
                    "terrain midground target should retain the fixed stress distance");
            require(midground.camera_clearance_m >= 149.999F &&
                        midground.foreground_min_margin_m >= 9.999F,
                    "terrain midground camera should preserve backdrop clearance contracts");
            require(independent_backdrop_foreground_margin(source, midground, 1.0F,
                                                           midground.aspect_ratio) >= 9.998F,
                    "terrain midground camera should independently clear the lower frustum");
            require_near_frame_contract(source, midground, 1.0F);
        }
    }
}

void test_backdrop_planner_handles_review_aspect_ratios() {
    constexpr std::array<float, 3> aspects{4.0F / 3.0F, 16.0F / 9.0F, 21.0F / 9.0F};
    constexpr std::array presets{
        cubey::projects::terrain::TerrainPreset::Mountain,
        cubey::projects::terrain::TerrainPreset::Upland,
        cubey::projects::terrain::TerrainPreset::Plains,
    };
    for (const float aspect : aspects) {
        for (const auto preset : presets) {
            const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
                .seed = 9012U,
                .preset = preset,
                .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
            });
            const auto plan =
                cubey::projects::terrain::plan_terrain_backdrop_camera(source, 1.0F, aspect);
            require_near(plan.aspect_ratio, aspect, 0.0F,
                         "terrain backdrop plan should preserve its requested aspect ratio");
            require(independent_backdrop_foreground_margin(source, plan, 1.0F, aspect) >= 9.998F,
                    "terrain backdrop plan should clear representative review aspects");
        }
    }
}

void test_far_field_v1_camera_contract() {
    constexpr std::array<std::uint64_t, 3> seeds{0U, 9012U, 12345U};
    constexpr float expected_yaw_half_angle = 30.0F * std::numbers::pi_v<float> / 180.0F;
    for (const std::uint64_t seed : seeds) {
        const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
            .seed = seed,
            .preset = cubey::projects::terrain::TerrainPreset::Mountain,
            .version = cubey::projects::terrain::TerrainSourceVersion::V2_1,
            .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
        });
        const auto plan = cubey::projects::terrain::plan_terrain_backdrop_camera(source);
        require(plan.far_field_contract_satisfied,
                "terrain far-field v1 should find a valid natural staging zone");
        require_near(plan.safe_zone_radius_m, 200.0F, 0.0F,
                     "terrain far-field v1 should publish its movement radius");
        require_near(plan.yaw_half_angle_radians, expected_yaw_half_angle, 0.000001F,
                     "terrain far-field v1 should publish its directional cone");
        require(plan.target_distance_m >= 3400.0F && plan.minimum_target_distance_m >= 3200.0F,
                "terrain far-field v1 should preserve target distance throughout the zone");
        require(plan.safe_zone_foreground_min_margin_m >= 9.99F,
                "terrain far-field v1 should clear the lower foreground throughout the zone");
        require(plan.safe_zone_near_frame_test_distance_m == 2400.0F &&
                    plan.safe_zone_near_frame_max_occluded_ray_count == 0U,
                "terrain far-field v1 should keep center and upper rays clear through 2.4 km");
        require(plan.safe_zone_lower_frame_test_distance_m == 1200.0F &&
                    plan.safe_zone_lower_frame_max_occluded_ray_count <= 2U,
                "terrain far-field v1 should reject a nearby lower-frame terrain wall");

        const cubey::math::Vec2 target_xz{plan.target_position.x, plan.target_position.z};
        for (std::uint32_t position_index = 0U; position_index < 8U; ++position_index) {
            const float angle =
                static_cast<float>(position_index) * std::numbers::pi_v<float> / 4.0F;
            const cubey::math::Vec2 position =
                plan.anchor_xz +
                cubey::math::Vec2{std::cos(angle), std::sin(angle)} * plan.safe_zone_radius_m;
            const cubey::math::Vec2 offset = target_xz - position;
            require(std::sqrt(offset.x * offset.x + offset.y * offset.y) >= 3199.99F,
                    "terrain far-field v1 perimeter should independently preserve distance");
        }

        if (seed != 9012U) {
            continue;
        }
        cubey::projects::terrain::TerrainSurfaceController controller(80.0F);
        controller.set_home_pose(plan.anchor_xz, plan.yaw_radians, plan.pitch_radians);
        controller.set_home_constraints(plan.safe_zone_radius_m, plan.yaw_half_angle_radians);
        const cubey::Transform3D home_camera =
            controller.camera_transform(source, 1.0F, plan.camera_clearance_m);
        const cubey::math::Vec3 home_forward_3d =
            home_camera.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F};
        const float home_horizontal_length = std::sqrt(home_forward_3d.x * home_forward_3d.x +
                                                       home_forward_3d.z * home_forward_3d.z);
        const cubey::math::Vec2 home_forward{home_forward_3d.x / home_horizontal_length,
                                             home_forward_3d.z / home_horizontal_length};
        controller.advance_forward(100.0);
        auto camera = controller.camera_transform(source, 1.0F, plan.camera_clearance_m);
        const float movement_x = camera.translation.x - plan.anchor_xz.x;
        const float movement_z = camera.translation.z - plan.anchor_xz.y;
        require_near(std::sqrt(movement_x * movement_x + movement_z * movement_z), 200.0F, 0.01F,
                     "terrain far-field controller should clamp movement to the zone");

        controller.apply_look_delta(2.0F, 0.0F);
        camera = controller.camera_transform(source, 1.0F, plan.camera_clearance_m);
        const cubey::math::Vec3 camera_forward_3d =
            camera.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F};
        const float horizontal_length = std::sqrt(camera_forward_3d.x * camera_forward_3d.x +
                                                  camera_forward_3d.z * camera_forward_3d.z);
        const cubey::math::Vec2 camera_forward{camera_forward_3d.x / horizontal_length,
                                               camera_forward_3d.z / horizontal_length};
        require_near(home_forward.x * camera_forward.x + home_forward.y * camera_forward.y,
                     std::cos(expected_yaw_half_angle), 0.00001F,
                     "terrain far-field controller should clamp yaw to the directional cone");
    }
}

void test_backdrop_planner_frames_hierarchical_source_peaks() {
    constexpr std::array<std::uint64_t, 3> seeds{0U, 9012U, 12345U};
    constexpr float peak_offset_radians = 5.0F * std::numbers::pi_v<float> / 180.0F;
    constexpr float maximum_pitch_radians = 18.0F * std::numbers::pi_v<float> / 180.0F;
    for (const std::uint64_t seed : seeds) {
        const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
            .seed = seed,
            .preset = cubey::projects::terrain::TerrainPreset::Mountain,
            .version = cubey::projects::terrain::TerrainSourceVersion::V3,
        });
        const auto plan = cubey::projects::terrain::plan_terrain_backdrop_camera(source);
        const cubey::math::Vec3 camera_forward =
            plan.transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F};
        const cubey::math::Vec2 target_direction{
            plan.target_position.x - plan.transform.translation.x,
            plan.target_position.z - plan.transform.translation.z,
        };
        const float target_length = std::sqrt(target_direction.x * target_direction.x +
                                              target_direction.y * target_direction.y);
        const float camera_horizontal_length =
            std::sqrt(camera_forward.x * camera_forward.x + camera_forward.z * camera_forward.z);
        const float target_alignment =
            (camera_forward.x * target_direction.x + camera_forward.z * target_direction.y) /
            (camera_horizontal_length * target_length);
        require(target_alignment >= 0.98F,
                "terrain hierarchical backdrop should face its selected target");
        require(plan.pitch_radians <= maximum_pitch_radians + 0.000001F,
                "terrain hierarchical backdrop pitch should remain bounded");
        const float desired_pitch = plan.target_elevation_radians - peak_offset_radians;
        constexpr float minimum_pitch_radians = -2.0F * std::numbers::pi_v<float> / 180.0F;
        require_near(plan.pitch_radians,
                     std::clamp(desired_pitch, minimum_pitch_radians, maximum_pitch_radians),
                     0.000001F, "terrain hierarchical backdrop should frame or bound its peak");
        require_near_frame_contract(source, plan, 1.0F);
    }
}

void test_backdrop_traversal_preserves_planned_clearance() {
    const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = 9012U,
        .preset = cubey::projects::terrain::TerrainPreset::Mountain,
        .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
    });
    const auto plan = cubey::projects::terrain::plan_terrain_backdrop_camera(source);
    cubey::projects::terrain::TerrainSurfaceController controller(80.0F);
    controller.set_home_pose(plan.anchor_xz, plan.yaw_radians, plan.pitch_radians);
    for (std::uint32_t frame = 0U; frame < 600U; ++frame) {
        controller.advance_forward(1.0 / 60.0);
    }
    const auto camera = controller.camera_transform(source, 1.0F, plan.camera_clearance_m);
    const auto sample = cubey::projects::terrain::sample_terrain(
        source, {.world_xz = {camera.translation.x, camera.translation.z}});
    require_near(camera.translation.y, sample.height_m + plan.camera_clearance_m, 0.001F,
                 "terrain backdrop traversal should preserve selected planned clearance");
}

void test_environment_gpu_parameters_preserve_atmosphere_lighting() {
    cubey::render::AtmosphereEnvironmentConfig environment{};
    environment.sun_elevation_degrees = 12.0F;
    environment.sun_azimuth_degrees = -35.0F;
    const auto frame = cubey::render::atmosphere_environment_frame_uniforms(environment, {});
    const auto lighting = cubey::render::atmosphere_environment_lighting(environment);
    const auto gpu = cubey::projects::terrain::terrain_environment_gpu_parameters(frame, lighting);
    require_near(gpu.primary_light_direction_intensity.x, lighting.primary_light_direction.x,
                 0.0001F, "terrain environment should preserve primary light direction");
    require_near(gpu.primary_light_direction_intensity.w, lighting.primary_light_intensity, 0.0001F,
                 "terrain environment should preserve primary light intensity");
    require_near(gpu.primary_light_color_angular_radius.w, frame.sun_direction_radius.w, 0.0001F,
                 "terrain daylight should preserve the sun angular radius");
    require_near(gpu.diffuse_irradiance_sh[0].x, lighting.diffuse_irradiance_sh[0].x, 0.0001F,
                 "terrain environment should preserve diffuse irradiance SH");
}

void test_clipmap_has_expected_extent_and_transition_data() {
    const cubey::projects::terrain::TerrainRuntimeConfig config{};
    const auto clipmap = cubey::projects::terrain::terrain_clipmap_config(config);
    require_near(cubey::render::clipmap_grid_2d_near_cell_size(clipmap), 2.0F, 0.0001F,
                 "terrain clipmap should preserve the configured near cell size");
    require_near(clipmap.outer_half_extent, 16'384.0F, 0.01F,
                 "terrain clipmap should cover the v1 outer radius");

    const auto mesh = cubey::projects::terrain::make_terrain_clipmap_mesh(config);
    require(mesh.diagnostics.lod_levels == 8U && mesh.diagnostics.patch_count == 29U,
            "terrain clipmap should contain one center and seven four-piece rings");
    require(mesh.diagnostics.total_triangles ==
                cubey::projects::terrain::terrain_clipmap_triangle_count(mesh),
            "terrain clipmap diagnostics should match mesh triangles");
    require(!mesh.vertices.empty() && mesh.vertices.front().color[2] == 0.0F,
            "terrain clipmap should draw the finest level first");

    bool has_transition = false;
    bool has_fully_morphed_guard = false;
    bool has_child_coverage = false;
    bool has_skirts = false;
    for (const auto& vertex : mesh.vertices) {
        require(vertex.color[0] >= 2.0F && vertex.color[1] >= 0.0F && vertex.color[1] <= 1.0F &&
                    vertex.color[2] >= 0.0F && vertex.color[2] <= 1.0F,
                "terrain clipmap vertex metadata should stay in range");
        has_transition = has_transition || vertex.color[1] > 0.0F;
        has_fully_morphed_guard = has_fully_morphed_guard || vertex.color[1] == 1.0F;
        require(vertex.normal[1] >= 0.0F, "terrain skirt depth should not be negative");
        require_near(vertex.normal[2], 2.0F, 0.0F,
                     "terrain levels should share the finest snapped origin");
        has_skirts = has_skirts || vertex.normal[1] > 0.0F;
        if (vertex.color[2] == 0.0F) {
            require_near(vertex.normal[0], 0.0F, 0.0F,
                         "terrain finest level should not mask child coverage");
        } else {
            require_near(vertex.normal[0], vertex.color[0] * 32.0F, 0.001F,
                         "terrain coarse levels should publish their child half extent");
            has_child_coverage = true;
        }
    }
    require(has_transition, "terrain clipmap should publish transition morph weights");
    require(has_fully_morphed_guard,
            "terrain clipmap should fully morph its ownership guard cells");
    require(has_child_coverage, "terrain clipmap should publish finer-level coverage metadata");
    require(has_skirts, "terrain clipmap should add boundary skirts below transitioning levels");
}

void test_clipmap_patch_spans_preserve_level_cell_spacing() {
    const cubey::projects::terrain::TerrainRuntimeConfig config{};
    const auto clipmap = cubey::projects::terrain::terrain_clipmap_config(config);
    const auto patches = cubey::render::clipmap_grid_2d_patches<64U>(clipmap);
    for (const auto& patch : patches) {
        const float expected_cell_size =
            cubey::render::clipmap_grid_2d_level_cell_size(clipmap, patch.level);
        const float actual_cell_size_x =
            (patch.bounds.max_x - patch.bounds.min_x) / static_cast<float>(patch.cells_x);
        const float actual_cell_size_z =
            (patch.bounds.max_z - patch.bounds.min_z) / static_cast<float>(patch.cells_z);
        require_near(actual_cell_size_x, expected_cell_size, 0.0001F,
                     "terrain clipmap patch width should preserve advertised cell spacing");
        require_near(actual_cell_size_z, expected_cell_size, 0.0001F,
                     "terrain clipmap patch height should preserve advertised cell spacing");
    }
}

void test_quality_tile_field_uses_world_aligned_quad_patches() {
    cubey::projects::terrain::TerrainRuntimeConfig config{};
    config.render_path = cubey::projects::terrain::TerrainRenderPath::Quality;
    const auto mesh = cubey::projects::terrain::make_terrain_quality_tile_mesh(config);
    require(!mesh.vertices.empty() && mesh.indices.size() == mesh.vertices.size(),
            "quality tile field should index every patch control point");
    require(mesh.diagnostics.patches_per_axis == 128U &&
                mesh.diagnostics.patch_count == 16'384U &&
                mesh.patch_count() == mesh.diagnostics.patch_count,
            "quality tile field should cover the default extent with a 128 by 128 grid");
    require_near(mesh.diagnostics.patch_span_m, 256.0F, 0.0F,
                 "quality tile patches should preserve the supported maximum span");
    require_near(mesh.diagnostics.half_extent_m, 16'384.0F, 0.0F,
                 "quality tile field should preserve clipmap coverage");
    require((mesh.indices.size() % 4U) == 0U && mesh.vertices.size() == 65'536U,
            "quality tile field should use four-control-point patches");
    for (std::size_t index = 0; index < mesh.vertices.size(); index += 4U) {
        require_near(mesh.vertices[index].color[0], mesh.diagnostics.patch_span_m, 0.0F,
                     "quality tiles should publish their physical span");
        require_near(mesh.vertices[index].normal[0], 0.0F, 0.0F,
                     "quality tiles should not publish child LOD ownership");
        require_near(mesh.vertices[index].normal[1], mesh.diagnostics.patch_span_m, 0.0F,
                     "quality tiles should recenter by whole patch increments");
        require(mesh.vertices[index].position[0] < mesh.vertices[index + 1U].position[0] &&
                    mesh.vertices[index].position[2] < mesh.vertices[index + 3U].position[2],
                "quality tile corners should preserve quad orientation");
    }

    const std::uint32_t patches_per_axis = mesh.diagnostics.patches_per_axis;
    for (std::uint32_t z = 0; z < patches_per_axis; ++z) {
        for (std::uint32_t x = 0; x < patches_per_axis; ++x) {
            const std::size_t patch =
                (static_cast<std::size_t>(z) * patches_per_axis + x) * 4U;
            if (x + 1U < patches_per_axis) {
                const std::size_t right = patch + 4U;
                require(mesh.vertices[patch + 1U].position == mesh.vertices[right].position &&
                            mesh.vertices[patch + 2U].position ==
                                mesh.vertices[right + 3U].position,
                        "quality horizontal neighbors should share exact edge controls");
            }
            if (z + 1U < patches_per_axis) {
                const std::size_t top =
                    patch + static_cast<std::size_t>(patches_per_axis) * 4U;
                require(mesh.vertices[patch + 3U].position == mesh.vertices[top].position &&
                            mesh.vertices[patch + 2U].position ==
                                mesh.vertices[top + 1U].position,
                        "quality vertical neighbors should share exact edge controls");
            }
        }
    }
    require_near(mesh.vertices.front().position[0], -mesh.diagnostics.half_extent_m, 0.0F,
                 "quality tile field should begin at the negative extent");
    require_near(mesh.vertices[mesh.vertices.size() - 2U].position[0],
                 mesh.diagnostics.half_extent_m, 0.0F,
                 "quality tile field should end at the positive extent");
}

void test_quality_material_tile_contract() {
    require(cubey::projects::terrain::kTerrainMaterialTileExtent == 1024U,
            "quality material tiles should preserve review resolution");
    require_near(cubey::projects::terrain::kTerrainMaterialTilePeriodM, 256.0F, 0.0F,
                 "quality material tiles should preserve their world period");
    require(cubey::projects::terrain::kTerrainMaterialTileCount == 4U,
            "quality materials should expose ground scree rock and snow tiles");
    require(cubey::projects::terrain::kTerrainLayeredMaterialTextureCount == 8U,
            "layered materials should expose two products for four terrain layers");
}

void test_surface_controller_traversal_preserves_clearance() {
    const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = 9012U,
        .preset = cubey::projects::terrain::TerrainPreset::Mountain,
        .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
    });

    cubey::projects::terrain::TerrainSurfaceController controller;
    constexpr float vertical_scale = 1.0F;
    constexpr float clearance_m = 70.0F;
    constexpr double fixed_step_seconds = 1.0 / 60.0;
    const cubey::Transform3D start =
        controller.camera_transform(source, vertical_scale, clearance_m);

    for (std::uint32_t frame = 0U; frame < 600U; ++frame) {
        controller.advance_forward(fixed_step_seconds);
        const cubey::Transform3D camera =
            controller.camera_transform(source, vertical_scale, clearance_m);
        const auto sample = cubey::projects::terrain::sample_terrain(
            source, {.world_xz = {camera.translation.x, camera.translation.z}});
        require(std::isfinite(camera.translation.x) && std::isfinite(camera.translation.y) &&
                    std::isfinite(camera.translation.z),
                "terrain traversal camera transform should stay finite");
        require_near(camera.translation.y, sample.height_m * vertical_scale + clearance_m, 0.001F,
                     "terrain traversal should preserve requested surface clearance");
    }

    const cubey::Transform3D finish =
        controller.camera_transform(source, vertical_scale, clearance_m);
    const float dx = finish.translation.x - start.translation.x;
    const float dz = finish.translation.z - start.translation.z;
    require_near(std::sqrt(dx * dx + dz * dz), 2'200.0F, 0.1F,
                 "terrain traversal should cover speed times fixed duration");
}

void test_ground_controller_uses_walking_scale_speed() {
    const auto source = cubey::projects::terrain::resolve_terrain_source_parameters({
        .seed = 9012U,
        .preset = cubey::projects::terrain::TerrainPreset::Mountain,
        .weathering = cubey::projects::terrain::TerrainWeatheringMode::Local,
    });
    cubey::projects::terrain::TerrainSurfaceController controller(12.0F);
    constexpr double fixed_step_seconds = 1.0 / 60.0;
    for (std::uint32_t frame = 0U; frame < 600U; ++frame) {
        controller.advance_forward(fixed_step_seconds);
    }
    const cubey::Transform3D camera = controller.camera_transform(source, 1.0F, 2.0F);
    const auto sample = cubey::projects::terrain::sample_terrain(
        source, {.world_xz = {camera.translation.x, camera.translation.z}});
    require_near(camera.translation.y, sample.height_m + 2.0F, 0.001F,
                 "terrain ground traversal should preserve eye-level clearance");
    require_near(std::sqrt(camera.translation.x * camera.translation.x +
                           camera.translation.z * camera.translation.z),
                 120.0F, 0.02F,
                 "terrain ground traversal should cover walking-scale fixed-step distance");
}

} // namespace

int main() {
    try {
        test_runtime_config_defaults_to_the_v1_scene();
        test_source_v2_extends_only_mountain_detail_band();
        test_runtime_config_parses_source_v2();
        test_runtime_config_parses_source_v2_1();
        test_runtime_config_parses_source_v3();
        test_source_v3_component_views_require_source_v3();
        test_ground_camera_and_shape_diagnostics_parse();
        test_backdrop_camera_configuration();
        test_backdrop_presentation_and_coverage_debug_parse();
        test_backdrop_planner_is_deterministic_and_clear();
        test_backdrop_planner_handles_review_aspect_ratios();
        test_far_field_v1_camera_contract();
        test_backdrop_planner_frames_hierarchical_source_peaks();
        test_backdrop_traversal_preserves_planned_clearance();
        test_environment_gpu_parameters_preserve_atmosphere_lighting();
        test_clipmap_has_expected_extent_and_transition_data();
        test_clipmap_patch_spans_preserve_level_cell_spacing();
        test_quality_tile_field_uses_world_aligned_quad_patches();
        test_quality_material_tile_contract();
        test_surface_controller_traversal_preserves_clearance();
        test_ground_controller_uses_walking_scale_speed();
        std::cout << "terrain_render_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_render_tests: " << error.what() << '\n';
        return 1;
    }
}
