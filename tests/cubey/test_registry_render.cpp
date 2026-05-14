#include "test_registry_common.h"

void test_compute_generated_texture_config_validates_dispatch_shape();
void test_depth_only_rendering_info_describes_sampled_depth_target();
void test_depth_texture_config_maps_sampled_depth_usage();
void test_cube_examples_share_spinning_cube_motion();
void test_shadow_cube_ground_plane_sits_below_spinning_cube();
void test_shadow_cube_transforms_normals_with_rotating_model_matrix();
void test_material_cubes_show_real_material_variant_grid();
void test_example_lighting_uses_low_linear_ambient_terms();
void test_pbr_furnace_headless_path_transitions_depth_attachment();
void test_smoke_tests_fail_on_vulkan_validation_errors();
void test_hostless_cmake_defaults_disable_host_dependent_targets();
void test_frame_slot_defaults_to_single_frame_slot();
void test_frame_slot_rejects_invalid_slots();
void test_frame_slot_wraps_frame_indices();
void test_frame_uniform_buffer_config_describes_host_visible_uniform_storage();
void test_frame_uniform_buffer_contract_is_slot_based_and_move_only();
void test_generated_pbr_environment_config_rejects_zero_dimensions();
void test_generated_pbr_environment_data_is_deterministic_and_sized();
void test_generated_pbr_dfg_lut_stores_energy_compensation_term();
void test_generated_pbr_prefilter_uses_ggx_convolution_not_legacy_average_mix();
void test_pbr_environment_data_can_be_generated_from_equirectangular_hdr();
void test_pbr_equirectangular_sampling_maps_cardinal_directions();
void test_indexed_mesh_config_describes_u16_geometry();
void test_indexed_mesh_config_describes_u32_geometry();
void test_instance_buffer_helpers_describe_instance_vertex_data();
void test_material_descriptor_writer_preserves_set_and_write_order();
void test_material_info_defaults_to_depth_and_forward_passes();
void test_material_instance_config_builds_descriptor_set_info();
void test_material_pass_info_applies_graphics_pipeline_state();
void test_material_pass_info_builds_descriptor_set_info();
void test_material_pass_info_validates_descriptor_and_push_constant_shape();
void test_material_pass_masks_include_requested_passes();
void test_primitive_cube_normal_uv_mesh_preserves_normals_and_face_uvs();
void test_primitive_cube_position_color_mesh_uses_face_colors_and_indices();
void test_primitive_vertex_layouts_match_shader_contracts();
void test_primitive_uv_sphere_mesh_uses_smooth_normals_and_uv_grid();
void test_primitive_xz_plane_mesh_uses_center_half_extents_and_up_normal();
void test_color_space_converts_srgb_authored_values_to_linear();
void test_render_graph_allows_imported_texture_read_without_prior_write();
void test_render_graph_barrier_recording_rejects_unallocated_transient_resources();
void test_render_graph_creates_transient_texture_and_preserves_pass_order();
void test_render_graph_declares_compute_storage_buffer_flow();
void test_render_graph_declares_shadow_map_then_scene_sample_flow();
void test_render_graph_derives_compute_to_graphics_storage_buffer_barrier();
void test_render_graph_derives_depth_to_sampled_texture_barrier();
void test_render_graph_derives_imported_buffer_acquire_and_release_barriers();
void test_render_graph_derives_imported_texture_acquire_and_release_barriers();
void test_render_graph_derives_transient_texture_first_use_barrier();
void test_render_graph_execute_propagates_callback_exceptions();
void test_render_graph_execute_rejects_missing_callbacks_but_compile_allows_declarations();
void test_render_graph_execute_with_recorder_exposes_command_recorder();
void test_render_graph_executes_callbacks_in_pass_order_and_exposes_context();
void test_render_graph_execution_resolves_bound_transient_resources();
void test_render_graph_frame_executor_tracks_slots_and_rejects_invalid_record_info();
void test_render_graph_frame_record_info_separates_command_buffer_ownership();
void test_render_graph_frame_resources_manage_frame_slots();
void test_render_graph_frame_resources_reject_invalid_slots();
void test_render_graph_frame_resources_replace_one_slot_without_disturbing_another();
void test_render_graph_imports_color_and_depth_targets();
void test_render_graph_omits_read_after_read_barriers();
void test_render_graph_preserves_material_pass_metadata();
void test_render_graph_recorder_access_rejects_recorderless_execution();
void test_render_graph_rejects_attachment_usage_outside_graphics_pass();
void test_render_graph_rejects_incompatible_same_pass_resource_access();
void test_render_graph_rejects_invalid_resource_descriptors_and_handles();
void test_render_graph_rejects_transient_texture_read_before_write();
void test_render_graph_resolved_color_target_view_rejects_depth_texture();
void test_render_graph_resolves_color_target_view_from_bound_transient_texture();
void test_render_graph_resolves_sampled_color_texture_view();
void test_render_graph_resolves_sampled_depth_texture_view();
void test_render_graph_sampled_texture_view_rejects_unallocated_transient();
void test_render_graph_storage_read_write_initializes_transient_buffers();
void test_render_graph_transfer_pass_accepts_only_transfer_usages();
void test_render_item_resolves_draw_item_fields();
void test_render_item_validates_required_draw_identity();
void test_render_pass_helpers_describe_clear_values_and_fullscreen_triangle();
void test_pbr_vertex_layout_matches_shader_contract();
void test_pbr_forward_pass_declares_scene_and_material_sets();
void test_pbr_material_factors_are_uniforms_and_push_constants_are_model_only();
void test_pbr_post_pass_declares_uniforms_and_scene_color();
void test_pbr_reflectance_helpers_match_filament_convention();
void test_pbr_scene_uniforms_carry_display_transform();
void test_pbr_skybox_pass_declares_scene_set();
void test_pbr_skybox_uniforms_are_uniform_buffer_safe();
void test_pbr_shaders_use_filament_style_material_remap();
void test_render_pipeline_resource_allows_vertexless_fullscreen_pipeline_shape();
void test_render_pipeline_resource_builds_compute_pipeline_info();
void test_render_pipeline_resource_builds_layout_and_dynamic_pipeline_info();
void test_render_pipeline_resource_helpers_build_file_recipe_config();
void test_render_resource_handles_are_hashable_keys();
void test_render_resource_registry_round_trips_mesh_and_material_info();
void test_render_resource_registry_tracks_handle_lifetime_and_labels();
void test_render_resource_table_resolves_move_only_resources_by_handle();
void test_render_target_rendering_info_describes_dynamic_rendering();
void test_render_target_views_describe_color_only_targets();
void test_shadow_depth_pass_info_declares_depth_only_state();
void test_shadow_map_depth_texture_config_describes_sampled_depth_target();
void test_shadow_map_sampler_uses_depth_texture_defaults();
void test_texture_2d_config_maps_storage_sampled_usage();
void test_texture_2d_config_maps_transfer_sampled_usage();
void test_texture_cube_config_maps_transfer_sampled_cube_usage();

namespace cubey::tests {

std::span<const TestCase> render_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_frame_slot_defaults_to_single_frame_slot),
        CUBEY_TEST(test_cube_examples_share_spinning_cube_motion),
        CUBEY_TEST(test_shadow_cube_ground_plane_sits_below_spinning_cube),
        CUBEY_TEST(test_shadow_cube_transforms_normals_with_rotating_model_matrix),
        CUBEY_TEST(test_material_cubes_show_real_material_variant_grid),
        CUBEY_TEST(test_example_lighting_uses_low_linear_ambient_terms),
        CUBEY_TEST(test_pbr_furnace_headless_path_transitions_depth_attachment),
        CUBEY_TEST(test_smoke_tests_fail_on_vulkan_validation_errors),
        CUBEY_TEST(test_hostless_cmake_defaults_disable_host_dependent_targets),
        CUBEY_TEST(test_frame_slot_wraps_frame_indices),
        CUBEY_TEST(test_frame_slot_rejects_invalid_slots),
        CUBEY_TEST(test_frame_uniform_buffer_config_describes_host_visible_uniform_storage),
        CUBEY_TEST(test_frame_uniform_buffer_contract_is_slot_based_and_move_only),
        CUBEY_TEST(test_render_graph_imports_color_and_depth_targets),
        CUBEY_TEST(test_render_graph_creates_transient_texture_and_preserves_pass_order),
        CUBEY_TEST(test_render_graph_declares_shadow_map_then_scene_sample_flow),
        CUBEY_TEST(test_render_graph_derives_depth_to_sampled_texture_barrier),
        CUBEY_TEST(test_render_graph_derives_compute_to_graphics_storage_buffer_barrier),
        CUBEY_TEST(test_render_graph_derives_imported_texture_acquire_and_release_barriers),
        CUBEY_TEST(test_render_graph_derives_transient_texture_first_use_barrier),
        CUBEY_TEST(test_render_graph_derives_imported_buffer_acquire_and_release_barriers),
        CUBEY_TEST(test_render_graph_omits_read_after_read_barriers),
        CUBEY_TEST(test_render_graph_storage_read_write_initializes_transient_buffers),
        CUBEY_TEST(test_render_graph_preserves_material_pass_metadata),
        CUBEY_TEST(test_render_graph_barrier_recording_rejects_unallocated_transient_resources),
        CUBEY_TEST(test_render_graph_executes_callbacks_in_pass_order_and_exposes_context),
        CUBEY_TEST(test_render_graph_execution_resolves_bound_transient_resources),
        CUBEY_TEST(test_render_graph_resolves_color_target_view_from_bound_transient_texture),
        CUBEY_TEST(test_render_graph_resolved_color_target_view_rejects_depth_texture),
        CUBEY_TEST(test_render_graph_frame_resources_manage_frame_slots),
        CUBEY_TEST(test_render_graph_frame_resources_reject_invalid_slots),
        CUBEY_TEST(test_render_graph_frame_resources_replace_one_slot_without_disturbing_another),
        CUBEY_TEST(test_render_graph_resolves_sampled_color_texture_view),
        CUBEY_TEST(test_render_graph_resolves_sampled_depth_texture_view),
        CUBEY_TEST(test_render_graph_sampled_texture_view_rejects_unallocated_transient),
        CUBEY_TEST(
            test_render_graph_execute_rejects_missing_callbacks_but_compile_allows_declarations),
        CUBEY_TEST(test_render_graph_execute_propagates_callback_exceptions),
        CUBEY_TEST(test_render_graph_execute_with_recorder_exposes_command_recorder),
        CUBEY_TEST(test_render_graph_recorder_access_rejects_recorderless_execution),
        CUBEY_TEST(test_render_graph_rejects_transient_texture_read_before_write),
        CUBEY_TEST(test_render_graph_allows_imported_texture_read_without_prior_write),
        CUBEY_TEST(test_render_graph_rejects_invalid_resource_descriptors_and_handles),
        CUBEY_TEST(test_render_graph_rejects_attachment_usage_outside_graphics_pass),
        CUBEY_TEST(test_render_graph_rejects_incompatible_same_pass_resource_access),
        CUBEY_TEST(test_render_graph_declares_compute_storage_buffer_flow),
        CUBEY_TEST(test_render_graph_transfer_pass_accepts_only_transfer_usages),
        CUBEY_TEST(test_render_graph_frame_executor_tracks_slots_and_rejects_invalid_record_info),
        CUBEY_TEST(test_render_graph_frame_record_info_separates_command_buffer_ownership),
        CUBEY_TEST(test_render_pipeline_resource_builds_layout_and_dynamic_pipeline_info),
        CUBEY_TEST(test_render_pipeline_resource_allows_vertexless_fullscreen_pipeline_shape),
        CUBEY_TEST(test_render_pipeline_resource_builds_compute_pipeline_info),
        CUBEY_TEST(test_render_pipeline_resource_helpers_build_file_recipe_config),
        CUBEY_TEST(test_indexed_mesh_config_describes_u16_geometry),
        CUBEY_TEST(test_indexed_mesh_config_describes_u32_geometry),
        CUBEY_TEST(test_primitive_vertex_layouts_match_shader_contracts),
        CUBEY_TEST(test_instance_buffer_helpers_describe_instance_vertex_data),
        CUBEY_TEST(test_color_space_converts_srgb_authored_values_to_linear),
        CUBEY_TEST(test_primitive_cube_position_color_mesh_uses_face_colors_and_indices),
        CUBEY_TEST(test_primitive_cube_normal_uv_mesh_preserves_normals_and_face_uvs),
        CUBEY_TEST(test_primitive_xz_plane_mesh_uses_center_half_extents_and_up_normal),
        CUBEY_TEST(test_primitive_uv_sphere_mesh_uses_smooth_normals_and_uv_grid),
        CUBEY_TEST(test_render_item_validates_required_draw_identity),
        CUBEY_TEST(test_render_item_resolves_draw_item_fields),
        CUBEY_TEST(test_render_pass_helpers_describe_clear_values_and_fullscreen_triangle),
        CUBEY_TEST(test_pbr_vertex_layout_matches_shader_contract),
        CUBEY_TEST(test_pbr_forward_pass_declares_scene_and_material_sets),
        CUBEY_TEST(test_pbr_material_factors_are_uniforms_and_push_constants_are_model_only),
        CUBEY_TEST(test_pbr_scene_uniforms_carry_display_transform),
        CUBEY_TEST(test_pbr_post_pass_declares_uniforms_and_scene_color),
        CUBEY_TEST(test_pbr_skybox_uniforms_are_uniform_buffer_safe),
        CUBEY_TEST(test_pbr_skybox_pass_declares_scene_set),
        CUBEY_TEST(test_pbr_reflectance_helpers_match_filament_convention),
        CUBEY_TEST(test_pbr_shaders_use_filament_style_material_remap),
        CUBEY_TEST(test_generated_pbr_environment_data_is_deterministic_and_sized),
        CUBEY_TEST(test_generated_pbr_dfg_lut_stores_energy_compensation_term),
        CUBEY_TEST(test_generated_pbr_prefilter_uses_ggx_convolution_not_legacy_average_mix),
        CUBEY_TEST(test_pbr_equirectangular_sampling_maps_cardinal_directions),
        CUBEY_TEST(test_pbr_environment_data_can_be_generated_from_equirectangular_hdr),
        CUBEY_TEST(test_generated_pbr_environment_config_rejects_zero_dimensions),
        CUBEY_TEST(test_render_resource_registry_tracks_handle_lifetime_and_labels),
        CUBEY_TEST(test_render_resource_handles_are_hashable_keys),
        CUBEY_TEST(test_render_resource_registry_round_trips_mesh_and_material_info),
        CUBEY_TEST(test_render_resource_table_resolves_move_only_resources_by_handle),
        CUBEY_TEST(test_shadow_map_sampler_uses_depth_texture_defaults),
        CUBEY_TEST(test_shadow_map_depth_texture_config_describes_sampled_depth_target),
        CUBEY_TEST(test_shadow_depth_pass_info_declares_depth_only_state),
        CUBEY_TEST(test_render_target_views_describe_color_only_targets),
        CUBEY_TEST(test_render_target_rendering_info_describes_dynamic_rendering),
        CUBEY_TEST(test_depth_only_rendering_info_describes_sampled_depth_target),
        CUBEY_TEST(test_texture_2d_config_maps_storage_sampled_usage),
        CUBEY_TEST(test_texture_2d_config_maps_transfer_sampled_usage),
        CUBEY_TEST(test_texture_cube_config_maps_transfer_sampled_cube_usage),
        CUBEY_TEST(test_depth_texture_config_maps_sampled_depth_usage),
        CUBEY_TEST(test_compute_generated_texture_config_validates_dispatch_shape),
        CUBEY_TEST(test_material_info_defaults_to_depth_and_forward_passes),
        CUBEY_TEST(test_material_instance_config_builds_descriptor_set_info),
        CUBEY_TEST(test_material_descriptor_writer_preserves_set_and_write_order),
        CUBEY_TEST(test_material_pass_masks_include_requested_passes),
        CUBEY_TEST(test_material_pass_info_validates_descriptor_and_push_constant_shape),
        CUBEY_TEST(test_material_pass_info_applies_graphics_pipeline_state),
        CUBEY_TEST(test_material_pass_info_builds_descriptor_set_info),
    };
    return tests;
}

} // namespace cubey::tests
