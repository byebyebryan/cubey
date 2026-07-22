#include "test_registry_common.h"

#include <array>

void test_gltf_asset_loads_static_pbr_triangle();
void test_gltf_asset_marks_nodes_authored_with_matrix();
void test_gltf_asset_generates_tangent_handedness_from_mirrored_uvs();
void test_gltf_asset_loads_uv1_vertex_color_and_texture_transform();
void test_gltf_asset_generates_tangents_from_normal_texture_uv_set();
void test_gltf_asset_rejects_unsupported_texture_coordinate_set();
void test_gltf_asset_generates_flat_normals_when_missing();
void test_gltf_asset_can_reject_missing_normals_when_generation_is_disabled();
void test_gltf_asset_loads_empty_animation();
void test_gltf_asset_loads_rigid_animation_channels();
void test_gltf_asset_loads_normalized_rotation_animation_output();
void test_gltf_asset_loads_skinning_and_morph_data();
void test_gltf_asset_ignores_unknown_optional_extensions();
void test_gltf_asset_rejects_unknown_required_extensions();
void test_gltf_asset_accepts_supported_required_extensions();
void test_gltf_asset_loads_required_basisu_texture_source();
void test_gltf_asset_rejects_layered_basisu_material_images();
void test_gltf_asset_rejects_formatted_ktx2_basisu_sources();
void test_gltf_asset_loads_sampler_mip_filter();
void test_gltf_asset_loads_sparse_mesh_accessors();
void test_gltf_asset_loads_sparse_animation_output();
void test_hdr_image_loads_radiance_rgba32f_pixels();
void test_hdr_image_rejects_non_hdr_input();
void test_terrain_raster_height_source_loads_and_samples_calibrated_field();
void test_terrain_raster_height_source_footprint_selects_filtered_mip();
void test_terrain_raster_height_source_odd_mips_preserve_world_endpoints();
void test_terrain_raster_height_source_rejects_invalid_contracts();

namespace cubey::tests {

std::span<const TestCase> asset_test_cases() {
    static constexpr std::array<TestCase, 27> tests{
        CUBEY_TEST(test_gltf_asset_loads_static_pbr_triangle),
        CUBEY_TEST(test_gltf_asset_marks_nodes_authored_with_matrix),
        CUBEY_TEST(test_gltf_asset_generates_tangent_handedness_from_mirrored_uvs),
        CUBEY_TEST(test_gltf_asset_loads_uv1_vertex_color_and_texture_transform),
        CUBEY_TEST(test_gltf_asset_generates_tangents_from_normal_texture_uv_set),
        CUBEY_TEST(test_gltf_asset_rejects_unsupported_texture_coordinate_set),
        CUBEY_TEST(test_gltf_asset_generates_flat_normals_when_missing),
        CUBEY_TEST(test_gltf_asset_can_reject_missing_normals_when_generation_is_disabled),
        CUBEY_TEST(test_gltf_asset_loads_empty_animation),
        CUBEY_TEST(test_gltf_asset_loads_rigid_animation_channels),
        CUBEY_TEST(test_gltf_asset_loads_normalized_rotation_animation_output),
        CUBEY_TEST(test_gltf_asset_loads_skinning_and_morph_data),
        CUBEY_TEST(test_gltf_asset_ignores_unknown_optional_extensions),
        CUBEY_TEST(test_gltf_asset_rejects_unknown_required_extensions),
        CUBEY_TEST(test_gltf_asset_accepts_supported_required_extensions),
        CUBEY_TEST(test_gltf_asset_loads_required_basisu_texture_source),
        CUBEY_TEST(test_gltf_asset_rejects_layered_basisu_material_images),
        CUBEY_TEST(test_gltf_asset_rejects_formatted_ktx2_basisu_sources),
        CUBEY_TEST(test_gltf_asset_loads_sampler_mip_filter),
        CUBEY_TEST(test_gltf_asset_loads_sparse_mesh_accessors),
        CUBEY_TEST(test_gltf_asset_loads_sparse_animation_output),
        CUBEY_TEST(test_hdr_image_loads_radiance_rgba32f_pixels),
        CUBEY_TEST(test_hdr_image_rejects_non_hdr_input),
        CUBEY_TEST(test_terrain_raster_height_source_loads_and_samples_calibrated_field),
        CUBEY_TEST(test_terrain_raster_height_source_footprint_selects_filtered_mip),
        CUBEY_TEST(test_terrain_raster_height_source_odd_mips_preserve_world_endpoints),
        CUBEY_TEST(test_terrain_raster_height_source_rejects_invalid_contracts),
    };
    return tests;
}

} // namespace cubey::tests
