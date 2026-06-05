#include "planet_camera.h"
#include "planet_config.h"
#include "planet_frame.h"

#include <cubey/scene/transform_3d.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void test_planet_frame_derives_horizon_and_planes() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 600000.0F,
        .atmosphere_height_m = 70000.0F,
        .camera_altitude_m = 240000.0F,
    };
    const cubey::Transform3D camera{
        .translation = {0.0F, 0.0F, config.radius_m + config.camera_altitude_m},
    };

    const cubey::projects::planet::PlanetFrame frame =
        cubey::projects::planet::make_planet_frame(config, camera);
    const float camera_radius = config.radius_m + config.camera_altitude_m;
    const float expected_horizon =
        std::sqrt((camera_radius * camera_radius) - (config.radius_m * config.radius_m));

    require_near(frame.camera_altitude_m, config.camera_altitude_m, 0.5F,
                 "planet frame should derive camera altitude from camera radius");
    require_near(frame.horizon_distance_m, expected_horizon, 1.0F,
                 "planet frame should compute horizon distance");
    require(frame.near_plane_m > 0.0F && frame.near_plane_m < frame.far_plane_m,
            "planet frame should derive valid clip planes");
    require_near(frame.far_plane_m, camera_radius + config.radius_m + config.atmosphere_height_m,
                 1.0F, "planet frame should include the atmosphere shell in far plane");
}

void test_planet_config_rejects_invalid_radius() {
    cubey::projects::planet::PlanetConfig config{};
    config.radius_m = 0.0F;
    try {
        cubey::projects::planet::validate_planet_config(config);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("planet config should reject nonpositive radius");
}

void test_planet_config_rejects_invalid_skirt_depth() {
    cubey::projects::planet::PlanetConfig config{};
    config.skirt_depth_scale = 0.0F;
    try {
        cubey::projects::planet::validate_planet_config(config);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("planet config should reject nonpositive skirt depth");
}

void test_planet_config_accepts_max_live_lod() {
    cubey::projects::planet::PlanetConfig config{};
    config.max_lod_level = cubey::projects::planet::kPlanetMaxLiveLodLevel;
    cubey::projects::planet::validate_planet_config(config);
}

void test_planet_config_accepts_max_patch_resolution() {
    cubey::projects::planet::PlanetConfig config{};
    config.patch_resolution = cubey::projects::planet::kPlanetMaxPatchResolution;
    cubey::projects::planet::validate_planet_config(config);
}

void test_planet_config_rejects_lod_above_live_cap() {
    cubey::projects::planet::PlanetConfig config{};
    config.max_lod_level = cubey::projects::planet::kPlanetMaxLiveLodLevel + 1U;
    try {
        cubey::projects::planet::validate_planet_config(config);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("planet config should reject LOD above the live cap");
}

void test_planet_config_rejects_patch_resolution_above_cap() {
    cubey::projects::planet::PlanetConfig config{};
    config.patch_resolution = cubey::projects::planet::kPlanetMaxPatchResolution + 1U;
    try {
        cubey::projects::planet::validate_planet_config(config);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("planet config should reject patch resolution above the live cap");
}

void test_planet_config_applies_run_config_surface_options() {
    cubey::RunConfig run_config{};
    run_config.planet.patches_per_face = 4U;
    run_config.planet.patch_resolution = 16U;
    run_config.planet.max_lod_level = 5U;
    run_config.planet.max_lod_level_set = true;
    run_config.planet.lod_target_edge_px = 9.5F;
    run_config.planet.wire_overlay = 1;
    run_config.planet.skirts_enabled = 0;
    run_config.planet.skirt_depth_scale = 0.45F;
    run_config.planet.terrain_enabled = 0;
    run_config.planet.terrain_height_scale_m = 9000.0F;
    run_config.planet.terrain_noise_scale = 4.25F;
    run_config.planet.terrain_seed = 42U;
    run_config.planet.terrain_seed_set = true;
    run_config.planet.sea_level_m = -250.0F;
    run_config.planet.bathymetry_depth_scale_m = 3200.0F;
    run_config.planet.shoreline_width_m = 450.0F;

    const cubey::projects::planet::PlanetConfig config =
        cubey::projects::planet::planet_config_from_run_config(run_config);
    require(config.patches_per_face == 4U, "planet config should apply patches per face");
    require(config.patch_resolution == 16U, "planet config should apply patch resolution");
    require(config.max_lod_level == 5U, "planet config should apply max LOD level");
    require_near(config.lod_target_edge_px, 9.5F, 0.0001F, "planet config should apply LOD target");
    require(config.wire_overlay, "planet config should apply wire overlay");
    require(!config.skirts_enabled, "planet config should apply skirts toggle");
    require_near(config.skirt_depth_scale, 0.45F, 0.0001F,
                 "planet config should apply skirt depth");
    require(!config.terrain_enabled, "planet config should apply terrain toggle");
    require_near(config.terrain_height_scale_m, 9000.0F, 0.0001F,
                 "planet config should apply terrain height");
    require_near(config.terrain_noise_scale, 4.25F, 0.0001F,
                 "planet config should apply terrain noise");
    require(config.terrain_seed == 42U, "planet config should apply terrain seed");
    require_near(config.sea_level_m, -250.0F, 0.0001F,
                 "planet config should apply sea level");
    require_near(config.bathymetry_depth_scale_m, 3200.0F, 0.0001F,
                 "planet config should apply bathymetry depth scale");
    require_near(config.shoreline_width_m, 450.0F, 0.0001F,
                 "planet config should apply shoreline width");
}

void test_planet_camera_min_altitude_tracks_terrain_clearance() {
    cubey::projects::planet::PlanetConfig config{
        .radius_m = 600000.0F,
        .terrain_enabled = true,
        .terrain_height_scale_m = 12000.0F,
    };

    const float min_altitude = cubey::projects::planet::planet_camera_min_altitude_m(config);
    require(min_altitude >= config.terrain_height_scale_m,
            "planet camera minimum altitude should clear placeholder terrain");
}

void test_planet_camera_keeps_orbit_view_at_high_altitude() {
    const cubey::projects::planet::PlanetConfig config{};
    const cubey::projects::planet::PlanetCameraState state{
        .distance_m = config.radius_m + config.camera_altitude_m,
        .yaw_radians = 0.35F,
        .pitch_radians = 0.15F,
    };

    const cubey::Transform3D transform =
        cubey::projects::planet::make_planet_camera_transform(config, state);
    const cubey::math::Vec3 up = glm::normalize(transform.translation);
    const cubey::math::Vec3 forward =
        glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});

    require(cubey::projects::planet::planet_surface_camera_blend(config, state.distance_m) == 0.0F,
            "planet camera should stay in orbit mode at high altitude");
    require(glm::dot(forward, -up) > 0.95F,
            "high-altitude planet camera should keep looking toward planet center");
}

void test_planet_camera_transitions_to_surface_view_near_ground() {
    const cubey::projects::planet::PlanetConfig config{};
    const float distance =
        config.radius_m + cubey::projects::planet::planet_camera_min_altitude_m(config);
    const cubey::projects::planet::PlanetCameraState state{
        .distance_m = distance,
        .yaw_radians = 0.35F,
        .pitch_radians = 0.15F,
    };

    const cubey::Transform3D transform =
        cubey::projects::planet::make_planet_camera_transform(config, state);
    const cubey::math::Vec3 up = glm::normalize(transform.translation);
    const cubey::math::Vec3 forward =
        glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});

    require(cubey::projects::planet::planet_surface_camera_blend(config, state.distance_m) == 1.0F,
            "planet camera should fully transition near the minimum altitude");
    require(glm::dot(forward, -up) < 0.45F,
            "surface planet camera should stop looking straight down at planet center");
    require(glm::dot(forward, up) < -0.05F,
            "surface planet camera should keep a slight downward pitch toward terrain");
}

void test_planet_surface_camera_drag_rotates_view_without_moving_anchor() {
    const cubey::projects::planet::PlanetConfig config{};
    const float distance =
        config.radius_m + cubey::projects::planet::planet_camera_min_altitude_m(config);
    const cubey::projects::planet::PlanetCameraState anchored_state{
        .distance_m = distance,
        .yaw_radians = 0.35F,
        .pitch_radians = 0.15F,
        .surface_anchor_yaw_radians = 0.35F,
        .surface_anchor_pitch_radians = 0.15F,
        .surface_anchor_active = true,
    };
    const cubey::projects::planet::PlanetCameraState dragged_state{
        .distance_m = distance,
        .yaw_radians = 0.75F,
        .pitch_radians = 0.42F,
        .surface_anchor_yaw_radians = 0.35F,
        .surface_anchor_pitch_radians = 0.15F,
        .surface_anchor_active = true,
    };

    const cubey::Transform3D anchored =
        cubey::projects::planet::make_planet_camera_transform(config, anchored_state);
    const cubey::Transform3D dragged =
        cubey::projects::planet::make_planet_camera_transform(config, dragged_state);
    const cubey::math::Vec3 anchored_forward =
        glm::normalize(anchored.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
    const cubey::math::Vec3 dragged_forward =
        glm::normalize(dragged.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});

    require(glm::length(dragged.translation - anchored.translation) < 1.0F,
            "surface camera drag should keep the same anchored surface position");
    require(glm::dot(anchored_forward, dragged_forward) < 0.95F,
            "surface camera drag should rotate the local view direction");
}

void test_planet_surface_camera_vertical_drag_direction() {
    const cubey::projects::planet::PlanetConfig config{};
    const float distance =
        config.radius_m + cubey::projects::planet::planet_camera_min_altitude_m(config);
    const cubey::projects::planet::PlanetCameraState anchored_state{
        .distance_m = distance,
        .yaw_radians = 0.35F,
        .pitch_radians = 0.15F,
        .surface_anchor_yaw_radians = 0.35F,
        .surface_anchor_pitch_radians = 0.15F,
        .surface_anchor_active = true,
    };
    const cubey::projects::planet::PlanetCameraState drag_up_state{
        .distance_m = distance,
        .yaw_radians = anchored_state.yaw_radians,
        .pitch_radians = 0.42F,
        .surface_anchor_yaw_radians = anchored_state.surface_anchor_yaw_radians,
        .surface_anchor_pitch_radians = anchored_state.surface_anchor_pitch_radians,
        .surface_anchor_active = true,
    };
    const cubey::projects::planet::PlanetCameraState drag_down_state{
        .distance_m = distance,
        .yaw_radians = anchored_state.yaw_radians,
        .pitch_radians = -0.12F,
        .surface_anchor_yaw_radians = anchored_state.surface_anchor_yaw_radians,
        .surface_anchor_pitch_radians = anchored_state.surface_anchor_pitch_radians,
        .surface_anchor_active = true,
    };

    const cubey::Transform3D anchored =
        cubey::projects::planet::make_planet_camera_transform(config, anchored_state);
    const cubey::Transform3D drag_up =
        cubey::projects::planet::make_planet_camera_transform(config, drag_up_state);
    const cubey::Transform3D drag_down =
        cubey::projects::planet::make_planet_camera_transform(config, drag_down_state);
    const cubey::math::Vec3 up = glm::normalize(anchored.translation);
    const cubey::math::Vec3 anchored_forward =
        glm::normalize(anchored.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
    const cubey::math::Vec3 drag_up_forward =
        glm::normalize(drag_up.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
    const cubey::math::Vec3 drag_down_forward =
        glm::normalize(drag_down.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});

    require(glm::dot(drag_up_forward, up) > glm::dot(anchored_forward, up),
            "surface camera mouse-up drag should raise the local view");
    require(glm::dot(drag_down_forward, up) < glm::dot(anchored_forward, up),
            "surface camera mouse-down drag should lower the local view");
}

void test_planet_frame_converts_camera_to_render_origin() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 600000.0F,
        .camera_altitude_m = 240000.0F,
    };
    const cubey::Transform3D camera{
        .translation = {0.0F, 0.0F, config.radius_m + config.camera_altitude_m},
    };
    const cubey::projects::planet::PlanetFrame frame =
        cubey::projects::planet::make_planet_frame(config, camera);

    const cubey::math::Vec3 render_camera = cubey::projects::planet::planet_frame_world_to_render_m(
        frame, frame.camera_world_position_m);
    require_near(render_camera.x, 0.0F, 0.0001F, "camera should convert to render-origin x");
    require_near(render_camera.y, 0.0F, 0.0001F, "camera should convert to render-origin y");
    require_near(render_camera.z, 0.0F, 0.0001F, "camera should convert to render-origin z");
}

} // namespace

int main() {
    try {
        test_planet_frame_derives_horizon_and_planes();
        test_planet_config_rejects_invalid_radius();
        test_planet_config_rejects_invalid_skirt_depth();
        test_planet_config_accepts_max_live_lod();
        test_planet_config_accepts_max_patch_resolution();
        test_planet_config_rejects_lod_above_live_cap();
        test_planet_config_rejects_patch_resolution_above_cap();
        test_planet_config_applies_run_config_surface_options();
        test_planet_camera_min_altitude_tracks_terrain_clearance();
        test_planet_camera_keeps_orbit_view_at_high_altitude();
        test_planet_camera_transitions_to_surface_view_near_ground();
        test_planet_surface_camera_drag_rotates_view_without_moving_anchor();
        test_planet_surface_camera_vertical_drag_direction();
        test_planet_frame_converts_camera_to_render_origin();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_frame_tests: %s\n", error.what());
        return 1;
    }
}
