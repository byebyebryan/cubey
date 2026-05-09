#include <cubey/math.h>

#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_math_helpers_match_vulkan_projection_conventions() {
    static_assert(sizeof(cubey::math::Mat4) == sizeof(float) * 16U);

    const cubey::math::Mat4 identity = cubey::math::identity();
    require_near(identity[0][0], 1.0F, 0.0001F, "identity matrix should set diagonal entries");
    require_near(identity[1][1], 1.0F, 0.0001F, "identity matrix should set diagonal entries");

    const cubey::math::Mat4 translation = cubey::math::translation(2.0F, 3.0F, 4.0F);
    require_near(translation[3][0], 2.0F, 0.0001F,
                 "translation helper should use column-major GLM indexing");
    require_near(translation[3][1], 3.0F, 0.0001F,
                 "translation helper should use column-major GLM indexing");
    require_near(translation[3][2], 4.0F, 0.0001F,
                 "translation helper should use column-major GLM indexing");

    constexpr float kNearZ = 0.1F;
    constexpr float kFarZ = 100.0F;
    const cubey::math::Mat4 projection =
        cubey::math::perspective(std::numbers::pi_v<float> / 3.0F, 16.0F / 9.0F, kNearZ, kFarZ);
    require(projection[1][1] < 0.0F, "Vulkan projection helper should flip framebuffer-space Y");

    const cubey::math::Vec4 near_clip = projection * cubey::math::Vec4{0.0F, 0.0F, -kNearZ, 1.0F};
    const cubey::math::Vec4 far_clip = projection * cubey::math::Vec4{0.0F, 0.0F, -kFarZ, 1.0F};
    require_near(near_clip.z / near_clip.w, 0.0F, 0.0001F,
                 "Vulkan projection helper should map the near plane to NDC z 0");
    require_near(far_clip.z / far_clip.w, 1.0F, 0.0001F,
                 "Vulkan projection helper should map the far plane to NDC z 1");
}

void test_math_quaternion_helpers_match_rotation_matrices() {
    static_assert(sizeof(cubey::math::Quat) == sizeof(float) * 4U);

    const cubey::math::Mat4 identity_rotation = cubey::math::rotation(cubey::math::identity_quat());
    require_near(identity_rotation[0][0], 1.0F, 0.0001F,
                 "identity quaternion should produce identity rotation");
    require_near(identity_rotation[1][1], 1.0F, 0.0001F,
                 "identity quaternion should produce identity rotation");
    require_near(identity_rotation[2][2], 1.0F, 0.0001F,
                 "identity quaternion should produce identity rotation");

    const cubey::math::Quat x_rotation = cubey::math::angle_axis_quat(
        std::numbers::pi_v<float> / 2.0F, cubey::math::Vec3{1.0F, 0.0F, 0.0F});
    const cubey::math::Vec4 rotated =
        cubey::math::rotation(x_rotation) * cubey::math::Vec4{0.0F, 1.0F, 0.0F, 1.0F};
    require_near(rotated.y, 0.0F, 0.0001F,
                 "axis-angle quaternion should rotate around the requested axis");
    require_near(rotated.z, 1.0F, 0.0001F,
                 "axis-angle quaternion should rotate around the requested axis");

    const float pitch = 0.55F;
    const float yaw = 0.9F;
    const cubey::math::Mat4 quaternion_rotation =
        cubey::math::rotation(cubey::math::euler_xyz_quat({pitch, yaw, 0.0F}));
    const cubey::math::Mat4 matrix_rotation =
        cubey::math::rotation_y(yaw) * cubey::math::rotation_x(pitch);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            require_near(quaternion_rotation[column][row], matrix_rotation[column][row], 0.0001F,
                         "euler quaternion helper should preserve existing cube rotation order");
        }
    }
}
