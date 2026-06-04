#include <cubey/render/local_tangent_frame.h>

#include <cmath>
#include <stdexcept>

namespace {

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_local_tangent_frame_converts_between_world_and_local_space() {
    const cubey::render::LocalTangentFrame frame{
        .world_origin_m = {1000.0, 2000.0, 3000.0},
        .right = {1.0F, 0.0F, 0.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .forward = {0.0F, 0.0F, 1.0F},
        .planet_radius_m = 6371000.0F,
        .water_datum_m = 12.0F,
    };

    const cubey::math::Vec3 local =
        cubey::render::local_tangent_world_to_local_m(frame, {1010.0, 2024.0, 2975.0});
    require_near(local.x, 10.0F, 0.001F, "local tangent X should follow right axis");
    require_near(local.y, 24.0F, 0.001F, "local tangent Y should follow up axis");
    require_near(local.z, -25.0F, 0.001F, "local tangent Z should follow forward axis");

    const cubey::math::DVec3 world =
        cubey::render::local_tangent_local_to_world_m(frame, {4.0F, 5.0F, 6.0F});
    require_near(static_cast<float>(world.x), 1004.0F, 0.001F,
                 "local tangent world X should include origin");
    require_near(static_cast<float>(world.y), 2005.0F, 0.001F,
                 "local tangent world Y should include origin");
    require_near(static_cast<float>(world.z), 3006.0F, 0.001F,
                 "local tangent world Z should include origin");
}

void test_local_tangent_frame_preserves_camera_relative_precision() {
    const cubey::render::LocalTangentFrame frame{
        .world_origin_m = {1000000000.0, 0.0, -1000000000.0},
    };
    const cubey::math::DVec3 camera{1000000010.0, 5.0, -999999980.0};
    const cubey::math::DVec3 point{1000000014.0, 7.0, -999999986.0};

    const cubey::math::Vec3 relative =
        cubey::render::local_tangent_camera_relative_position_m(frame, point, camera);
    require_near(relative.x, 4.0F, 0.001F,
                 "camera-relative frame conversion should subtract before casting");
    require_near(relative.y, 2.0F, 0.001F,
                 "camera-relative frame conversion should preserve vertical offset");
    require_near(relative.z, -6.0F, 0.001F,
                 "camera-relative frame conversion should preserve forward offset");
}

void test_local_tangent_frame_reports_height_above_water_datum() {
    const cubey::render::LocalTangentFrame frame{
        .water_datum_m = 3.5F,
    };
    require_near(cubey::render::local_tangent_height_above_datum_m(frame, {0.0, 8.0, 0.0}),
                 4.5F, 0.001F,
                 "local tangent height should subtract the water datum");
}

void test_local_tangent_frame_rejects_invalid_basis() {
    cubey::render::LocalTangentFrame non_unit{};
    non_unit.right = {2.0F, 0.0F, 0.0F};
    require_throws([&] { cubey::render::validate_local_tangent_frame(non_unit); },
                   "local tangent frame should reject non-unit basis vectors");

    cubey::render::LocalTangentFrame non_orthogonal{};
    non_orthogonal.forward = {0.0F, 1.0F, 0.0F};
    require_throws([&] { cubey::render::validate_local_tangent_frame(non_orthogonal); },
                   "local tangent frame should reject non-orthogonal basis vectors");

    cubey::render::LocalTangentFrame invalid_radius{};
    invalid_radius.planet_radius_m = 0.0F;
    require_throws([&] { cubey::render::validate_local_tangent_frame(invalid_radius); },
                   "local tangent frame should reject invalid planet radius");
}
