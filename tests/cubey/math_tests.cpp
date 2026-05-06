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
