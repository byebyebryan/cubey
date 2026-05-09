#include <cubey/camera_2d.h>
#include <cubey/math.h>
#include <cubey/orbit_camera_3d.h>
#include <cubey/orbit_controller.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_matrix_close(const cubey::math::Mat4& actual, const cubey::math::Mat4& expected,
                          const char* message) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            require_close(actual[column][row], expected[column][row], message);
        }
    }
}

} // namespace

void test_camera_2d_pans_zooms_and_reports_view() {
    cubey::Camera2D camera({
        .center = {-0.5F, 0.0F},
        .scale = 0.675F,
    });

    camera.pan_by_screen_delta({64.0F, -36.0F}, 640.0F, 360.0F);
    cubey::Camera2DView view = camera.view(640.0F, 360.0F);
    require_close(view.center.x, -0.74F, "2D camera should pan horizontally in view units");
    require_close(view.center.y, 0.135F, "2D camera should pan vertically in view units");
    require_close(view.scale, 0.675F, "2D camera panning should keep scale unchanged");
    require_close(view.aspect, 640.0F / 360.0F, "2D camera should report viewport aspect");

    camera.zoom_at(0.86F, {320.0F, 180.0F}, 640.0F, 360.0F);
    view = camera.view(640.0F, 360.0F);
    require_close(view.center.x, -0.74F, "center zoom should keep camera center x unchanged");
    require_close(view.center.y, 0.135F, "center zoom should keep camera center y unchanged");
    require_close(view.scale, 0.5805F, "2D camera should zoom by the requested factor");

    camera.reset();
    view = camera.view(640.0F, 360.0F);
    require_close(view.center.x, -0.5F, "2D camera reset should restore configured center x");
    require_close(view.center.y, 0.0F, "2D camera reset should restore configured center y");
    require_close(view.scale, 0.675F, "2D camera reset should restore configured scale");
}

void test_camera_2d_clamps_scale() {
    cubey::Camera2D camera({
        .scale = 1.0F,
        .min_scale = 0.5F,
        .max_scale = 2.0F,
    });

    camera.zoom_at(0.01F, {320.0F, 180.0F}, 640.0F, 360.0F);
    require_close(camera.scale(), 0.5F, "2D camera should clamp minimum scale");

    camera.zoom_at(100.0F, {320.0F, 180.0F}, 640.0F, 360.0F);
    require_close(camera.scale(), 2.0F, "2D camera should clamp maximum scale");
}

void test_orbit_camera_3d_matches_current_cube_view_projection() {
    cubey::OrbitCamera3D camera({
        .distance = 4.2F,
        .fovy_radians = std::numbers::pi_v<float> / 3.0F,
        .near_z = 0.1F,
        .far_z = 100.0F,
    });

    const cubey::math::Mat4 expected_view = cubey::math::translation(0.0F, 0.0F, -4.2F);
    require_matrix_close(camera.view_matrix(), expected_view,
                         "orbit camera default view should match existing cube view");

    const float aspect = 16.0F / 9.0F;
    const cubey::math::Mat4 expected_projection =
        cubey::math::perspective(std::numbers::pi_v<float> / 3.0F, aspect, 0.1F, 100.0F);
    require_matrix_close(camera.projection_matrix(aspect), expected_projection,
                         "orbit camera projection should use shared Vulkan perspective helper");
    require_matrix_close(camera.view_projection_matrix(aspect), expected_projection * expected_view,
                         "orbit camera should compose projection and view matrices");
}

void test_orbit_camera_3d_accepts_orbit_state() {
    cubey::OrbitCamera3D camera({.distance = 4.2F});
    cubey::OrbitController controller;
    controller.begin_drag(0.0, 0.0);
    controller.drag_to(25.0, -50.0);
    controller.end_drag();
    camera.set_orbit(controller);

    const cubey::math::Mat4 expected_view = cubey::math::translation(0.0F, 0.0F, -4.2F) *
                                            cubey::math::rotation_x(0.5F) *
                                            cubey::math::rotation_y(-0.25F);
    require_matrix_close(camera.view_matrix(), expected_view,
                         "orbit camera should derive view orientation from orbit state");
}
