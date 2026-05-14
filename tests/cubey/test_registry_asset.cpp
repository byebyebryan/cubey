#include "test_registry_common.h"

#include <array>

void test_gltf_asset_loads_static_pbr_triangle();
void test_gltf_asset_generates_flat_normals_when_missing();
void test_gltf_asset_can_reject_missing_normals_when_generation_is_disabled();
void test_gltf_asset_loads_empty_animation();
void test_gltf_asset_loads_rigid_animation_channels();
void test_gltf_asset_loads_skinning_and_morph_data();
void test_gltf_asset_ignores_unknown_optional_extensions();
void test_gltf_asset_rejects_unknown_required_extensions();
void test_gltf_asset_accepts_supported_required_extensions();
void test_gltf_asset_loads_sparse_mesh_accessors();
void test_gltf_asset_loads_sparse_animation_output();
void test_hdr_image_loads_radiance_rgba32f_pixels();
void test_hdr_image_rejects_non_hdr_input();

namespace cubey::tests {

std::span<const TestCase> asset_test_cases() {
    static constexpr std::array<TestCase, 13> tests{
        CUBEY_TEST(test_gltf_asset_loads_static_pbr_triangle),
        CUBEY_TEST(test_gltf_asset_generates_flat_normals_when_missing),
        CUBEY_TEST(test_gltf_asset_can_reject_missing_normals_when_generation_is_disabled),
        CUBEY_TEST(test_gltf_asset_loads_empty_animation),
        CUBEY_TEST(test_gltf_asset_loads_rigid_animation_channels),
        CUBEY_TEST(test_gltf_asset_loads_skinning_and_morph_data),
        CUBEY_TEST(test_gltf_asset_ignores_unknown_optional_extensions),
        CUBEY_TEST(test_gltf_asset_rejects_unknown_required_extensions),
        CUBEY_TEST(test_gltf_asset_accepts_supported_required_extensions),
        CUBEY_TEST(test_gltf_asset_loads_sparse_mesh_accessors),
        CUBEY_TEST(test_gltf_asset_loads_sparse_animation_output),
        CUBEY_TEST(test_hdr_image_loads_radiance_rgba32f_pixels),
        CUBEY_TEST(test_hdr_image_rejects_non_hdr_input),
    };
    return tests;
}

} // namespace cubey::tests
