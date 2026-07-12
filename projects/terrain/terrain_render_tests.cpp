#include "terrain_backdrop_camera.h"
#include "terrain_clipmap.h"
#include "terrain_config.h"
#include "terrain_environment_gpu.h"
#include "terrain_surface_controller.h"

#include <cubey/core/run_config.h>

#include <cmath>
#include <iostream>
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

void test_runtime_config_defaults_to_the_v1_scene() {
    const cubey::RunConfig run_config{};
    const auto config =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.source.preset == cubey::projects::terrain::TerrainPreset::Mountain,
            "terrain runtime should default to mountain preset");
    require(config.source.weathering == cubey::projects::terrain::TerrainWeatheringMode::Local,
            "terrain runtime should default to local weathering");
    require(config.camera == cubey::projects::terrain::TerrainCameraPreset::Oblique,
            "terrain runtime should default to oblique camera");
    require(config.debug_view == cubey::projects::terrain::TerrainDebugView::Surface,
            "terrain runtime should default to surface view");
    require(config.presentation == cubey::projects::terrain::TerrainPresentationMode::Standard,
            "terrain runtime should default to standard presentation");
    require_near(config.near_cell_size_m, 2.0F, 0.0F,
                 "terrain runtime should default to two-meter near cells");
    require(config.lod_levels == 8U && config.cells_per_axis == 128U,
            "terrain runtime should use the v1 clipmap dimensions");
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
    require_near(cubey::projects::terrain::terrain_camera_traversal_speed_mps(config.camera),
                 12.0F, 0.0F, "terrain ground camera should use walking-scale traversal");
    require(cubey::projects::terrain::terrain_debug_view_from_name("shadow") ==
                cubey::projects::terrain::TerrainDebugView::Shadow,
            "terrain runtime should parse the shadow diagnostic");
    require(cubey::projects::terrain::terrain_debug_view_from_name("aerial") ==
                cubey::projects::terrain::TerrainDebugView::AerialTransmittance,
            "terrain runtime should parse the aerial diagnostic alias");
}

void test_backdrop_camera_configuration() {
    cubey::RunConfig run_config;
    run_config.terrain.camera_preset = "backdrop";
    const auto config = cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
    require(config.camera == cubey::projects::terrain::TerrainCameraPreset::Backdrop,
            "terrain runtime should parse the backdrop camera");
    require(cubey::projects::terrain::terrain_camera_is_surface(config.camera),
            "terrain backdrop camera should remain interactively traversable");
    require(!cubey::projects::terrain::terrain_camera_advances_headless(config.camera),
            "terrain backdrop camera should remain static in headless captures");
    require_near(cubey::projects::terrain::terrain_camera_clearance_m(config.camera), 150.0F,
                 0.0F, "terrain backdrop camera should clear the local surface");
    require_near(cubey::projects::terrain::terrain_camera_fovy_radians(config.camera),
                 40.0F * std::numbers::pi_v<float> / 180.0F, 0.000001F,
                 "terrain backdrop camera should use a restrained field of view");
}

void test_backdrop_presentation_and_coverage_debug_parse() {
    cubey::RunConfig run_config;
    run_config.terrain.presentation = "backdrop";
    run_config.debug_view = "vegetation-coverage";
    const auto config = cubey::projects::terrain::terrain_runtime_config_from_run_config(run_config);
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
    constexpr std::array<std::uint64_t, 3> seeds{0U, 9012U, 12345U};
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
            require(std::isfinite(first.transform.translation.x) &&
                        std::isfinite(first.transform.translation.y) &&
                        std::isfinite(first.transform.translation.z) &&
                        std::isfinite(first.score) && first.score >= 0.0F,
                    "terrain backdrop plan should remain finite");
            require(first.target_distance_m >= 400.0F && first.target_distance_m <= 6400.0F,
                    "terrain backdrop target should use a supported sample distance");
            const auto anchor_sample = cubey::projects::terrain::sample_terrain(
                source, {.world_xz = first.anchor_xz});
            require(first.camera_clearance_m >= 150.0F,
                    "terrain backdrop camera should preserve its minimum clearance");
            require_near(first.transform.translation.y,
                         anchor_sample.height_m + first.camera_clearance_m, 0.001F,
                         "terrain backdrop camera should preserve final-source clearance");
            require(first.foreground_clear_distance_m == 300.0F &&
                        first.foreground_min_margin_m >= 9.999F,
                    "terrain backdrop camera should preserve the foreground contract");
            require(first.pitch_radians >= -2.0F * std::numbers::pi_v<float> / 180.0F &&
                        first.pitch_radians <= 12.0F * std::numbers::pi_v<float> / 180.0F,
                    "terrain backdrop pitch should remain in the presentation range");
        }
    }
}

void test_environment_gpu_parameters_preserve_atmosphere_lighting() {
    cubey::render::AtmosphereEnvironmentConfig environment{};
    environment.sun_elevation_degrees = 12.0F;
    environment.sun_azimuth_degrees = -35.0F;
    const auto frame = cubey::render::atmosphere_environment_frame_uniforms(environment, {});
    const auto lighting = cubey::render::atmosphere_environment_lighting(environment);
    const auto gpu = cubey::projects::terrain::terrain_environment_gpu_parameters(frame, lighting);
    require_near(gpu.primary_light_direction_intensity.x,
                 lighting.primary_light_direction.x, 0.0001F,
                 "terrain environment should preserve primary light direction");
    require_near(gpu.primary_light_direction_intensity.w, lighting.primary_light_intensity,
                 0.0001F, "terrain environment should preserve primary light intensity");
    require_near(gpu.primary_light_color_angular_radius.w, frame.sun_direction_radius.w,
                 0.0001F, "terrain daylight should preserve the sun angular radius");
    require_near(gpu.diffuse_irradiance_sh[0].x, lighting.diffuse_irradiance_sh[0].x,
                 0.0001F, "terrain environment should preserve diffuse irradiance SH");
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
        require_near(camera.translation.y, sample.height_m * vertical_scale + clearance_m,
                     0.001F, "terrain traversal should preserve requested surface clearance");
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
        test_ground_camera_and_shape_diagnostics_parse();
        test_backdrop_camera_configuration();
        test_backdrop_presentation_and_coverage_debug_parse();
        test_backdrop_planner_is_deterministic_and_clear();
        test_environment_gpu_parameters_preserve_atmosphere_lighting();
        test_clipmap_has_expected_extent_and_transition_data();
        test_clipmap_patch_spans_preserve_level_cell_spacing();
        test_surface_controller_traversal_preserves_clearance();
        test_ground_controller_uses_walking_scale_speed();
        std::cout << "terrain_render_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_render_tests: " << error.what() << '\n';
        return 1;
    }
}
