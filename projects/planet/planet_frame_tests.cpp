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
        .sea_level_m = -125.0F,
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
    require_near(frame.local_frame.water_datum_m, config.sea_level_m, 0.0001F,
                 "planet frame should carry sea level into the local tangent datum");
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

void test_planet_config_rejects_invalid_lod_hysteresis() {
    cubey::projects::planet::PlanetConfig config{};
    config.lod_hysteresis = 1.0F;
    try {
        cubey::projects::planet::validate_planet_config(config);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("planet config should reject invalid LOD hysteresis");
}

void test_planet_config_applies_run_config_surface_options() {
    cubey::RunConfig run_config{};
    run_config.planet.patches_per_face = 4U;
    run_config.planet.patch_resolution = 16U;
    run_config.planet.max_lod_level = 5U;
    run_config.planet.max_lod_level_set = true;
    run_config.planet.lod_target_edge_px = 9.5F;
    run_config.planet.lod_hysteresis = 0.25F;
    run_config.planet.wire_overlay = 1;
    run_config.planet.skirts_enabled = 0;
    run_config.planet.skirt_depth_scale = 0.45F;
    run_config.planet.terrain_enabled = 0;
    run_config.planet.terrain_height_scale_m = 9000.0F;
    run_config.planet.terrain_noise_scale = 4.25F;
    run_config.planet.terrain_mid_detail_strength = 0.75F;
    run_config.planet.terrain_fine_detail_strength = 0.2F;
    run_config.planet.terrain_fine_detail_scale = 18.0F;
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
    require_near(config.lod_hysteresis, 0.25F, 0.0001F,
                 "planet config should apply LOD hysteresis");
    require(config.wire_overlay, "planet config should apply wire overlay");
    require(!config.skirts_enabled, "planet config should apply skirts toggle");
    require_near(config.skirt_depth_scale, 0.45F, 0.0001F,
                 "planet config should apply skirt depth");
    require(!config.terrain_enabled, "planet config should apply terrain toggle");
    require_near(config.terrain_height_scale_m, 9000.0F, 0.0001F,
                 "planet config should apply terrain height");
    require_near(config.terrain_noise_scale, 4.25F, 0.0001F,
                 "planet config should apply terrain noise");
    require_near(config.terrain_mid_detail_strength, 0.75F, 0.0001F,
                 "planet config should apply terrain mid detail strength");
    require_near(config.terrain_fine_detail_strength, 0.2F, 0.0001F,
                 "planet config should apply terrain fine detail strength");
    require_near(config.terrain_fine_detail_scale, 18.0F, 0.0001F,
                 "planet config should apply terrain fine detail scale");
    require(config.terrain_seed == 42U, "planet config should apply terrain seed");
    require_near(config.sea_level_m, -250.0F, 0.0001F, "planet config should apply sea level");
    require_near(config.bathymetry_depth_scale_m, 3200.0F, 0.0001F,
                 "planet config should apply bathymetry depth scale");
    require_near(config.shoreline_width_m, 450.0F, 0.0001F,
                 "planet config should apply shoreline width");
}

void test_planet_config_change_kind_separates_dynamic_and_topology() {
    cubey::projects::planet::PlanetConfig current{};
    cubey::projects::planet::PlanetConfig dynamic = current;
    dynamic.radius_m += 1000.0F;
    dynamic.max_lod_level += 1U;
    dynamic.terrain_seed += 1U;
    require(cubey::projects::planet::planet_config_change_kind(current, dynamic) ==
                cubey::projects::planet::PlanetConfigChangeKind::Dynamic,
            "planet config should classify radius, LOD, and terrain edits as dynamic");

    cubey::projects::planet::PlanetConfig topology = current;
    topology.patch_resolution *= 2U;
    require(cubey::projects::planet::planet_config_change_kind(current, topology) ==
                cubey::projects::planet::PlanetConfigChangeKind::SurfaceTopology,
            "planet config should classify patch grid resolution edits as topology");

    topology = current;
    topology.skirts_enabled = !topology.skirts_enabled;
    require(cubey::projects::planet::planet_config_change_kind(current, topology) ==
                cubey::projects::planet::PlanetConfigChangeKind::SurfaceTopology,
            "planet config should classify skirt topology edits as topology");
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
    const cubey::projects::planet::PlanetCameraState state =
        cubey::projects::planet::planet_camera_home_state(config, 0.35F, 0.15F);

    const cubey::Transform3D transform =
        cubey::projects::planet::make_planet_camera_transform(config, state);
    const cubey::math::Vec3 up = glm::normalize(transform.translation);
    const cubey::math::Vec3 forward =
        glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});

    require(cubey::projects::planet::planet_surface_camera_blend(
                config, cubey::projects::planet::planet_camera_distance_m(state)) == 0.0F,
            "planet camera should stay in orbit mode at high altitude");
    require(glm::dot(forward, -up) > 0.95F,
            "high-altitude planet camera should keep looking toward planet center");
}

void test_planet_camera_transitions_to_surface_view_near_ground() {
    const cubey::projects::planet::PlanetConfig config{};
    const float distance =
        config.radius_m + cubey::projects::planet::planet_camera_min_altitude_m(config);
    cubey::projects::planet::PlanetCameraState state =
        cubey::projects::planet::planet_camera_home_state(config, 0.35F, 0.15F);
    cubey::projects::planet::planet_camera_set_distance(state, config, distance);
    cubey::projects::planet::planet_camera_reset_surface_view(state, config);

    const cubey::Transform3D transform =
        cubey::projects::planet::make_planet_camera_transform(config, state);
    const cubey::math::Vec3 up = glm::normalize(transform.translation);
    const cubey::math::Vec3 forward =
        glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});

    require(cubey::projects::planet::planet_surface_camera_blend(
                config, cubey::projects::planet::planet_camera_distance_m(state)) == 1.0F,
            "planet camera should fully transition near the minimum altitude");
    require(glm::dot(forward, -up) < 0.45F,
            "surface planet camera should stop looking straight down at planet center");
    require(glm::dot(forward, up) < -0.05F,
            "surface planet camera should keep a slight downward pitch toward terrain");
}

void test_planet_camera_initial_state_applies_run_config_mode() {
    cubey::projects::planet::PlanetConfig config{};
    cubey::RunConfig run_config{};
    run_config.planet.camera_mode = "surface";
    run_config.planet.camera_altitude_m = 240000.0F;

    const cubey::projects::planet::PlanetCameraState surface_state =
        cubey::projects::planet::planet_camera_initial_state_from_run_config(config, run_config,
                                                                             0.35F, 0.15F);

    require(cubey::projects::planet::planet_surface_camera_blend(
                config, cubey::projects::planet::planet_camera_distance_m(surface_state)) == 1.0F,
            "surface camera mode should force an initial surface-range camera");
    require(surface_state.surface_rotation_active,
            "surface camera mode should activate surface rotation immediately");

    run_config.planet.camera_mode = "orbit";
    const cubey::projects::planet::PlanetCameraState orbit_state =
        cubey::projects::planet::planet_camera_initial_state_from_run_config(config, run_config,
                                                                             0.35F, 0.15F);
    require(cubey::projects::planet::planet_surface_camera_blend(
                config, cubey::projects::planet::planet_camera_distance_m(orbit_state)) == 0.0F,
            "orbit camera mode should preserve the high-altitude home camera");
}

void test_planet_surface_camera_drag_rotates_view_without_moving_anchor() {
    const cubey::projects::planet::PlanetConfig config{};
    const float distance =
        config.radius_m + cubey::projects::planet::planet_camera_min_altitude_m(config);
    cubey::projects::planet::PlanetCameraState anchored_state =
        cubey::projects::planet::planet_camera_home_state(config, 0.35F, 0.15F);
    cubey::projects::planet::planet_camera_set_distance(anchored_state, config, distance);
    cubey::projects::planet::planet_camera_reset_surface_view(anchored_state, config);
    cubey::projects::planet::PlanetCameraState dragged_state = anchored_state;
    cubey::projects::planet::planet_camera_surface_look_drag(dragged_state, config, 120.0, -90.0);

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
    cubey::projects::planet::PlanetCameraState anchored_state =
        cubey::projects::planet::planet_camera_home_state(config, 0.35F, 0.15F);
    cubey::projects::planet::planet_camera_set_distance(anchored_state, config, distance);
    cubey::projects::planet::planet_camera_reset_surface_view(anchored_state, config);
    cubey::projects::planet::PlanetCameraState drag_up_state = anchored_state;
    cubey::projects::planet::planet_camera_surface_look_drag(drag_up_state, config, 0.0, -45.0);
    cubey::projects::planet::PlanetCameraState drag_down_state = anchored_state;
    cubey::projects::planet::planet_camera_surface_look_drag(drag_down_state, config, 0.0, 45.0);

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

void test_planet_surface_camera_move_stays_on_surface_shell() {
    const cubey::projects::planet::PlanetConfig config{};
    const float distance =
        config.radius_m + cubey::projects::planet::planet_camera_min_altitude_m(config);
    cubey::projects::planet::PlanetCameraState state =
        cubey::projects::planet::planet_camera_home_state(config, 0.35F, 0.15F);
    cubey::projects::planet::planet_camera_set_distance(state, config, distance);
    cubey::projects::planet::planet_camera_reset_surface_view(state, config);
    const cubey::math::DVec3 before_position = state.position_m;

    const bool moved =
        cubey::projects::planet::planet_camera_surface_move(state, config, 1.0F, 0.0F, 1.0);

    require(moved, "surface camera movement should move while near the surface");
    require_near(cubey::projects::planet::planet_camera_distance_m(state), distance, 1.0F,
                 "surface camera movement should preserve altitude");
    require(glm::dot(glm::normalize(before_position), glm::normalize(state.position_m)) < 0.99999F,
            "surface camera movement should advance along the planet tangent");
}

void test_planet_camera_keeps_double_precision_position_state() {
    const cubey::projects::planet::PlanetConfig config{
        .radius_m = 6000000.0F,
        .camera_altitude_m = 2400000.0F,
    };
    cubey::projects::planet::PlanetCameraState state =
        cubey::projects::planet::planet_camera_home_state(config, 0.35F, 0.15F);
    const cubey::math::DVec3 before = state.position_m;

    cubey::projects::planet::planet_camera_set_distance(
        state, config, cubey::projects::planet::planet_camera_distance_m(state));

    require(std::isfinite(state.position_m.x) && std::isfinite(state.position_m.y) &&
                std::isfinite(state.position_m.z),
            "planet camera double position should stay finite");
    require(glm::length(state.position_m) > static_cast<double>(config.radius_m),
            "planet camera double position should remain above the planet radius");
    require(glm::length(before) > 0.0, "planet camera double position should initialize");
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
        test_planet_config_rejects_invalid_lod_hysteresis();
        test_planet_config_applies_run_config_surface_options();
        test_planet_config_change_kind_separates_dynamic_and_topology();
        test_planet_camera_min_altitude_tracks_terrain_clearance();
        test_planet_camera_keeps_orbit_view_at_high_altitude();
        test_planet_camera_transitions_to_surface_view_near_ground();
        test_planet_camera_initial_state_applies_run_config_mode();
        test_planet_surface_camera_drag_rotates_view_without_moving_anchor();
        test_planet_surface_camera_vertical_drag_direction();
        test_planet_surface_camera_move_stays_on_surface_shell();
        test_planet_camera_keeps_double_precision_position_state();
        test_planet_frame_converts_camera_to_render_origin();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_frame_tests: %s\n", error.what());
        return 1;
    }
}
