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
        test_planet_config_rejects_lod_above_live_cap();
        test_planet_frame_converts_camera_to_render_origin();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_frame_tests: %s\n", error.what());
        return 1;
    }
}
