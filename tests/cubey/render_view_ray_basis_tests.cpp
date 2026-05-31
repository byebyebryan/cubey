#include <cubey/render/view_ray_basis_3d.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

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

void test_view_ray_basis_packs_camera_axes_for_shader_contract() {
    const cubey::render::ViewRayBasis3D basis = cubey::render::view_ray_basis_3d(
        cubey::math::identity_quat(), 16.0F / 9.0F, std::numbers::pi_v<float> * 0.5F);

    require_near(basis.right_aspect.x, 1.0F, 0.0001F,
                 "view ray basis should pack camera right x");
    require_near(basis.right_aspect.w, 16.0F / 9.0F, 0.0001F,
                 "view ray basis should pack aspect in right.w");
    require_near(basis.up_tan_half_fovy.y, 1.0F, 0.0001F,
                 "view ray basis should pack camera up y");
    require_near(basis.up_tan_half_fovy.w, 1.0F, 0.0001F,
                 "view ray basis should pack tan half fovy in up.w");
    require_near(basis.forward.z, -1.0F, 0.0001F,
                 "view ray basis should pack camera forward z");
}

void test_view_ray_basis_reconstructs_fullscreen_ray_directions() {
    const cubey::render::ViewRayBasis3D basis = cubey::render::view_ray_basis_3d(
        cubey::math::identity_quat(), 1.0F, std::numbers::pi_v<float> * 0.5F);

    const cubey::math::Vec3 center = cubey::render::view_ray_direction(basis, {0.0F, 0.0F});
    require_near(center.x, 0.0F, 0.0001F, "center ray should not lean sideways");
    require_near(center.y, 0.0F, 0.0001F, "center ray should not lean vertically");
    require_near(center.z, -1.0F, 0.0001F, "center ray should point camera-forward");

    const cubey::math::Vec3 top = cubey::render::view_ray_direction(basis, {0.0F, 1.0F});
    require(top.y < 0.0F, "positive fullscreen y should match shader-space downward tilt");
    require(top.z < 0.0F, "top ray should keep pointing through the camera frustum");

    const cubey::math::Vec3 right = cubey::render::view_ray_direction(basis, {1.0F, 0.0F});
    require(right.x > 0.0F, "positive fullscreen x should tilt toward camera right");
    require(right.z < 0.0F, "right ray should keep pointing through the camera frustum");
}

void test_view_ray_basis_rejects_invalid_projection_inputs() {
    require_throws(
        [] {
            (void)cubey::render::view_ray_basis_3d(cubey::math::identity_quat(), 0.0F, 1.0F);
        },
        "view ray basis should reject non-positive aspect");
    require_throws(
        [] {
            (void)cubey::render::view_ray_basis_3d(cubey::math::identity_quat(), 1.0F, 0.0F);
        },
        "view ray basis should reject non-positive fovy");
}
