#include <cubey/core/math.h>
#include <cubey/scene/transform_2d.h>
#include <cubey/scene/transform_3d.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

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

void require_matrix_close(const cubey::math::Mat3& actual, const cubey::math::Mat3& expected,
                          const char* message) {
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            require_close(actual[column][row], expected[column][row], message);
        }
    }
}

} // namespace

void test_transform_2d_builds_affine_matrix() {
    const cubey::Transform2D transform{
        .translation = {2.0F, 3.0F},
        .rotation_radians = std::numbers::pi_v<float> / 2.0F,
        .scale = {2.0F, 4.0F},
    };

    const cubey::math::Vec3 transformed_point =
        transform.affine_matrix() * cubey::math::Vec3{1.0F, 0.0F, 1.0F};
    cubey::math::Mat3 expected{1.0F};
    expected[0][0] = 0.0F;
    expected[0][1] = 2.0F;
    expected[1][0] = -4.0F;
    expected[1][1] = 0.0F;
    expected[2][0] = 2.0F;
    expected[2][1] = 3.0F;
    require_matrix_close(transform.affine_matrix(), expected,
                         "2D transform should expose its affine matrix");
    require_close(transformed_point.x, 2.0F,
                  "2D transform should apply scale, rotation, then translation");
    require_close(transformed_point.y, 5.0F,
                  "2D transform should apply scale, rotation, then translation");
    require_close(transformed_point.z, 1.0F, "2D transform should preserve homogeneous z");
}

void test_transform_3d_builds_affine_matrix() {
    const cubey::Transform3D transform{
        .translation = {2.0F, 3.0F, 4.0F},
        .rotation =
            cubey::math::angle_axis_quat(std::numbers::pi_v<float> / 2.0F, {1.0F, 0.0F, 0.0F}),
        .scale = {2.0F, 3.0F, 4.0F},
    };

    const cubey::math::Vec4 transformed_point =
        transform.affine_matrix() * cubey::math::Vec4{0.0F, 1.0F, 0.0F, 1.0F};
    require_close(transformed_point.x, 2.0F,
                  "3D transform should apply scale, rotation, then translation");
    require_close(transformed_point.y, 3.0F,
                  "3D transform should apply scale, rotation, then translation");
    require_close(transformed_point.z, 7.0F,
                  "3D transform should apply scale, rotation, then translation");
    require_close(transformed_point.w, 1.0F, "3D transform should preserve homogeneous w");
}

void test_transform_3d_matches_existing_cube_rotation_order() {
    const float pitch = 0.55F;
    const float yaw = 0.9F;
    const cubey::Transform3D transform{
        .rotation = cubey::math::euler_xyz_quat({pitch, yaw, 0.0F}),
    };

    const cubey::math::Mat4 expected =
        cubey::math::rotation_y(yaw) * cubey::math::rotation_x(pitch);
    require_matrix_close(transform.affine_matrix(), expected,
                         "3D transform should preserve existing cube rotation order");
}

void test_transform_3d_can_use_explicit_affine_matrix() {
    cubey::math::Mat4 explicit_matrix{1.0F};
    explicit_matrix[0][0] = 2.0F;
    explicit_matrix[1][1] = 3.0F;
    explicit_matrix[2][2] = 4.0F;
    explicit_matrix[3][0] = 5.0F;
    explicit_matrix[3][1] = 6.0F;
    explicit_matrix[3][2] = 7.0F;

    cubey::Transform3D transform = cubey::Transform3D::from_affine_matrix(explicit_matrix);
    transform.translation = {-1.0F, -2.0F, -3.0F};
    transform.scale = {9.0F, 9.0F, 9.0F};

    require(transform.has_affine_matrix(),
            "3D transform should report an explicit affine matrix override");
    require_matrix_close(transform.affine_matrix(), explicit_matrix,
                         "3D transform should return the explicit affine matrix");

    transform.clear_affine_matrix();
    require(!transform.has_affine_matrix(),
            "3D transform should clear an explicit affine matrix override");
    require_close(transform.affine_matrix()[3][0], -1.0F,
                  "cleared 3D transform should return to TRS evaluation");
}
