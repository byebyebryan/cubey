#include "test_registry_common.h"

#include <array>

void test_gltf_asset_loads_static_pbr_triangle();
void test_gltf_asset_rejects_unsupported_animation();
void test_hdr_image_loads_radiance_rgba32f_pixels();
void test_hdr_image_rejects_non_hdr_input();

namespace cubey::tests {

std::span<const TestCase> asset_test_cases() {
    static constexpr std::array<TestCase, 4> tests{
        CUBEY_TEST(test_gltf_asset_loads_static_pbr_triangle),
        CUBEY_TEST(test_gltf_asset_rejects_unsupported_animation),
        CUBEY_TEST(test_hdr_image_loads_radiance_rgba32f_pixels),
        CUBEY_TEST(test_hdr_image_rejects_non_hdr_input),
    };
    return tests;
}

} // namespace cubey::tests
