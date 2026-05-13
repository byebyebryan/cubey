#include "test_registry_common.h"

#include <array>

void test_gltf_asset_loads_static_pbr_triangle();
void test_gltf_asset_rejects_unsupported_animation();

namespace cubey::tests {

std::span<const TestCase> asset_test_cases() {
    static constexpr std::array<TestCase, 2> tests{
        CUBEY_TEST(test_gltf_asset_loads_static_pbr_triangle),
        CUBEY_TEST(test_gltf_asset_rejects_unsupported_animation),
    };
    return tests;
}

} // namespace cubey::tests
