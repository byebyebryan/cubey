#include "test_registry_common.h"

void test_compute_generated_texture_config_validates_dispatch_shape();
void test_depth_only_rendering_info_describes_sampled_depth_target();
void test_depth_texture_config_maps_sampled_depth_usage();
void test_cube_examples_share_spinning_cube_motion();
void test_cube_examples_split_app_lifecycle_from_resources_scene_and_rendering();
void test_shadow_cube_ground_plane_sits_below_spinning_cube();
void test_shadow_cube_transforms_normals_with_rotating_model_matrix();
void test_material_cubes_show_real_material_variant_grid();
void test_example_lighting_uses_low_linear_ambient_terms();
void test_pbr_furnace_headless_path_transitions_depth_attachment();
void test_smoke_tests_fail_on_vulkan_validation_errors();
void test_hostless_cmake_defaults_disable_host_dependent_targets();
void test_render_app_dynamic_rendering_scan_covers_built_projects();
void test_frame_slot_defaults_to_single_frame_slot();
void test_frame_slot_rejects_invalid_slots();
void test_frame_slot_wraps_frame_indices();
void test_frame_uniform_buffer_config_describes_host_visible_uniform_storage();
void test_frame_uniform_buffer_contract_is_slot_based_and_move_only();
void test_generated_pbr_environment_config_rejects_zero_dimensions();
void test_generated_pbr_environment_data_is_deterministic_and_sized();
void test_generated_pbr_dfg_lut_stores_energy_compensation_term();
void test_generated_pbr_prefilter_uses_ggx_convolution_not_legacy_average_mix();
void test_pbr_environment_texture_bindings_validate_required_views();
void test_pbr_environment_data_can_be_generated_from_equirectangular_hdr();
void test_pbr_equirectangular_sampling_maps_cardinal_directions();
void test_indexed_mesh_config_describes_u16_geometry();
void test_indexed_mesh_config_describes_u32_geometry();
void test_indexed_mesh_config_allows_storage_capable_vertex_buffers();
void test_instance_buffer_helpers_describe_instance_vertex_data();
void test_material_descriptor_writer_preserves_set_and_write_order();
void test_material_alpha_modes_map_to_blend_and_pass_policy();
void test_material_info_defaults_to_depth_and_forward_passes();
void test_material_instance_config_builds_descriptor_set_info();
void test_material_pass_info_applies_graphics_pipeline_state();
void test_material_pass_info_builds_descriptor_set_info();
void test_material_pass_info_validates_descriptor_and_push_constant_shape();
void test_push_constant_range_validation_enforces_device_limit_and_alignment();
void test_material_pass_masks_include_requested_passes();
void test_primitive_cube_normal_uv_mesh_preserves_normals_and_face_uvs();
void test_primitive_cube_position_color_mesh_uses_face_colors_and_indices();
void test_primitive_vertex_layouts_match_shader_contracts();
void test_primitive_uv_sphere_mesh_uses_smooth_normals_and_uv_grid();
void test_primitive_xz_plane_mesh_uses_center_half_extents_and_up_normal();
void test_color_space_converts_srgb_authored_values_to_linear();
void test_color_space_converts_hsv_and_hsl_authored_values();
void test_clipmap_grid_2d_emits_far_to_near_annular_patches();
void test_clipmap_grid_2d_rejects_invalid_config();
void test_adaptive_patch_lod_selects_quadtree_children();
void test_adaptive_patch_lod_hysteresis_delays_split_and_merge();
void test_adaptive_patch_lod_falls_back_at_patch_budget();
void test_adaptive_patch_lod_repairs_neighbor_deltas();
void test_adaptive_patch_lod_marks_edges_against_coarser_neighbors();
void test_adaptive_patch_lod_rejects_invalid_config_and_callbacks();
void test_local_tangent_frame_converts_between_world_and_local_space();
void test_local_tangent_frame_preserves_camera_relative_precision();
void test_local_tangent_frame_reports_height_above_water_datum();
void test_local_tangent_frame_rejects_invalid_basis();
void test_terrain_ocean_fields_pack_channel_layout_and_ranges();
void test_terrain_ocean_fields_reject_invalid_contract_data();
void test_render_graph_allows_imported_texture_read_without_prior_write();
void test_render_graph_barrier_recording_rejects_unallocated_transient_resources();
void test_render_graph_creates_transient_texture_and_preserves_pass_order();
void test_render_graph_declares_compute_storage_buffer_flow();
void test_render_graph_declares_shadow_map_then_scene_sample_flow();
void test_render_graph_derives_compute_to_graphics_storage_buffer_barrier();
void test_render_graph_derives_compute_to_graphics_storage_texture_barrier();
void test_render_graph_derives_compute_to_vertex_buffer_barrier();
void test_render_graph_derives_depth_to_sampled_texture_barrier();
void test_render_graph_honors_explicit_graphics_shader_stage_masks();
void test_render_graph_derives_imported_buffer_acquire_and_release_barriers();
void test_render_graph_derives_imported_texture_acquire_and_release_barriers();
void test_render_graph_derives_transient_texture_first_use_barrier();
void test_render_graph_execute_propagates_callback_exceptions();
void test_render_graph_execute_rejects_missing_callbacks_but_compile_allows_declarations();
void test_render_graph_execute_rejects_incompatible_resource_set();
void test_render_graph_execute_with_recorder_exposes_command_recorder();
void test_render_graph_executes_callbacks_in_pass_order_and_exposes_context();
void test_render_graph_execution_resolves_bound_transient_resources();
void test_render_graph_frame_executor_tracks_slots_and_rejects_invalid_record_info();
void test_render_graph_frame_record_info_separates_command_buffer_ownership();
void test_render_graph_frame_resources_manage_frame_slots();
void test_render_graph_frame_resources_reuse_compatible_slots();
void test_render_graph_resource_set_rejects_incompatible_shapes();
void test_render_graph_resource_set_rejects_undersized_bound_buffers();
void test_render_graph_frame_resources_reject_invalid_slots();
void test_render_graph_frame_resources_replace_one_slot_without_disturbing_another();
void test_render_graph_imports_color_and_depth_targets();
void test_render_graph_omits_read_after_read_barriers();
void test_render_graph_preserves_material_pass_metadata();
void test_render_graph_recorder_access_rejects_recorderless_execution();
void test_render_graph_rejects_attachment_usage_outside_graphics_pass();
void test_render_graph_rejects_incompatible_same_pass_resource_access();
void test_render_graph_rejects_invalid_resource_descriptors_and_handles();
void test_render_graph_rejects_shader_stage_masks_outside_pass_domain();
void test_render_graph_rejects_transient_texture_read_before_write();
void test_render_graph_texture_state_helpers_describe_common_frame_states();
void test_render_graph_resolved_color_target_view_rejects_depth_texture();
void test_render_graph_resolved_depth_target_view_rejects_color_texture();
void test_render_graph_resolves_color_target_view_from_bound_transient_texture();
void test_render_graph_resolves_depth_target_view_from_bound_transient_texture();
void test_render_graph_resolves_sampled_color_texture_view();
void test_render_graph_resolves_sampled_depth_texture_view();
void test_render_graph_sampled_texture_view_rejects_unallocated_transient();
void test_render_graph_storage_read_write_initializes_transient_buffers();
void test_render_graph_storage_read_write_initializes_transient_textures();
void test_render_graph_transfer_pass_accepts_only_transfer_usages();
void test_render_item_resolves_draw_item_fields();
void test_render_item_resolves_draw_item_from_frame_mesh_override();
void test_render_item_validates_required_draw_identity();
void test_frame_mesh_table_resolves_per_frame_override();
void test_gpu_deformation_descriptor_set_declares_storage_buffers();
void test_gpu_deformation_descriptor_set_scales_for_frame_slots();
void test_gpu_deformation_dispatch_groups_round_up_vertices();
void test_gpu_deformation_pipeline_config_accepts_descriptor_layouts();
void test_gpu_deformation_pipeline_config_uses_compute_stage_and_push_constants();
void test_gpu_deformation_shader_morphs_before_skinning();
void test_render_pass_helpers_describe_clear_values_and_fullscreen_triangle();
void test_atmosphere_environment_packs_frame_uniforms();
void test_atmosphere_environment_packs_celestial_frame_uniforms();
void test_atmosphere_environment_resolves_celestial_time_math();
void test_atmosphere_environment_lighting_projects_diffuse_sh();
void test_atmosphere_background_pass_declares_frame_and_atlas_bindings();
void test_atmosphere_background_texture_bindings_reject_null_atlases();
void test_atmosphere_reflection_probe_declares_prefilter_pass();
void test_atmosphere_reflection_probe_uses_per_subresource_uniform_materials();
void test_pbr_vertex_layout_matches_shader_contract();
void test_pbr_debug_view_names_parse_and_cycle();
void test_pbr_forward_pass_declares_scene_and_material_sets();
void test_pbr_material_factors_are_uniforms_and_push_constants_are_model_only();
void test_pbr_default_texture_specs_cover_all_sampled_material_bindings();
void test_pbr_material_table_groups_factors_and_supports_lifetime_operations();
void test_pbr_material_table_tracks_descriptor_layout_explicitly();
void test_hdr_post_frame_helpers_pack_scene_color_and_display_transform();
void test_pbr_post_pass_declares_uniforms_and_scene_color();
void test_pbr_reflectance_helpers_match_filament_convention();
void test_pbr_scene_uniforms_carry_display_transform();
void test_pbr_skybox_pass_declares_scene_set();
void test_pbr_skybox_uniforms_are_uniform_buffer_safe();
void test_pbr_shaders_use_filament_style_material_remap();
void test_forward_pbr_shader_package_uses_renderer_names();
void test_gltf_material_fallback_textures_preserve_pbr_factor_channels();
void test_pbr_examples_and_gltf_importer_share_material_resources();
void test_pbr_consumers_use_atmosphere_lighting_foundation();
void test_pbr_diagnostics_are_exposed_in_gltf_viewer_and_material_cubes();
void test_gltf_basisu_transcoder_policy_uses_bc7_and_rgba_fallback();
void test_gltf_basisu_transcoder_uses_bundled_zstd();
void test_vulkan_and_gltf_sample_asset_cmake_paths_are_portable_and_pinned();
void test_gltf_viewer_sample_asset_smoke_tests_cover_material_and_tangent_cases();
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
void test_view_ray_basis_packs_camera_axes_for_shader_contract();
void test_view_ray_basis_reconstructs_fullscreen_ray_directions();
void test_view_ray_basis_rejects_invalid_projection_inputs();
void test_shadow_map_depth_texture_config_describes_sampled_depth_target();
void test_shadow_map_sampler_uses_depth_texture_defaults();
void test_texture_2d_config_maps_storage_sampled_usage();
void test_texture_3d_config_maps_storage_sampled_volume_usage();
void test_texture_2d_config_maps_transfer_sampled_usage();
void test_texture_2d_config_preserves_mip_count();
void test_texture_2d_byte_size_uses_compressed_blocks();
void test_texture_cube_config_maps_transfer_sampled_cube_usage();
void test_texture_cube_config_maps_color_attachment_sampled_usage();

namespace cubey::tests {

std::span<const TestCase> render_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_frame_slot_defaults_to_single_frame_slot),
        CUBEY_TEST(test_cube_examples_share_spinning_cube_motion),
        CUBEY_TEST(test_cube_examples_split_app_lifecycle_from_resources_scene_and_rendering),
        CUBEY_TEST(test_shadow_cube_ground_plane_sits_below_spinning_cube),
        CUBEY_TEST(test_shadow_cube_transforms_normals_with_rotating_model_matrix),
        CUBEY_TEST(test_material_cubes_show_real_material_variant_grid),
        CUBEY_TEST(test_example_lighting_uses_low_linear_ambient_terms),
        CUBEY_TEST(test_pbr_furnace_headless_path_transitions_depth_attachment),
        CUBEY_TEST(test_smoke_tests_fail_on_vulkan_validation_errors),
        CUBEY_TEST(test_hostless_cmake_defaults_disable_host_dependent_targets),
        CUBEY_TEST(test_render_app_dynamic_rendering_scan_covers_built_projects),
        CUBEY_TEST(test_frame_slot_wraps_frame_indices),
        CUBEY_TEST(test_frame_slot_rejects_invalid_slots),
        CUBEY_TEST(test_frame_uniform_buffer_config_describes_host_visible_uniform_storage),
        CUBEY_TEST(test_frame_uniform_buffer_contract_is_slot_based_and_move_only),
        CUBEY_TEST(test_render_graph_texture_state_helpers_describe_common_frame_states),
        CUBEY_TEST(test_render_graph_imports_color_and_depth_targets),
        CUBEY_TEST(test_render_graph_creates_transient_texture_and_preserves_pass_order),
        CUBEY_TEST(test_render_graph_declares_shadow_map_then_scene_sample_flow),
        CUBEY_TEST(test_render_graph_derives_depth_to_sampled_texture_barrier),
        CUBEY_TEST(test_render_graph_derives_compute_to_graphics_storage_buffer_barrier),
        CUBEY_TEST(test_render_graph_derives_compute_to_graphics_storage_texture_barrier),
        CUBEY_TEST(test_render_graph_honors_explicit_graphics_shader_stage_masks),
        CUBEY_TEST(test_render_graph_derives_compute_to_vertex_buffer_barrier),
        CUBEY_TEST(test_render_graph_derives_imported_texture_acquire_and_release_barriers),
        CUBEY_TEST(test_render_graph_derives_transient_texture_first_use_barrier),
        CUBEY_TEST(test_render_graph_derives_imported_buffer_acquire_and_release_barriers),
        CUBEY_TEST(test_render_graph_omits_read_after_read_barriers),
        CUBEY_TEST(test_render_graph_storage_read_write_initializes_transient_buffers),
        CUBEY_TEST(test_render_graph_storage_read_write_initializes_transient_textures),
        CUBEY_TEST(test_render_graph_preserves_material_pass_metadata),
        CUBEY_TEST(test_render_graph_barrier_recording_rejects_unallocated_transient_resources),
        CUBEY_TEST(test_render_graph_executes_callbacks_in_pass_order_and_exposes_context),
        CUBEY_TEST(test_render_graph_execution_resolves_bound_transient_resources),
        CUBEY_TEST(test_render_graph_resolves_color_target_view_from_bound_transient_texture),
        CUBEY_TEST(test_render_graph_resolved_color_target_view_rejects_depth_texture),
        CUBEY_TEST(test_render_graph_resolves_depth_target_view_from_bound_transient_texture),
        CUBEY_TEST(test_render_graph_resolved_depth_target_view_rejects_color_texture),
        CUBEY_TEST(test_render_graph_frame_resources_manage_frame_slots),
        CUBEY_TEST(test_render_graph_frame_resources_reuse_compatible_slots),
        CUBEY_TEST(test_render_graph_resource_set_rejects_incompatible_shapes),
        CUBEY_TEST(test_render_graph_resource_set_rejects_undersized_bound_buffers),
        CUBEY_TEST(test_render_graph_frame_resources_reject_invalid_slots),
        CUBEY_TEST(test_render_graph_frame_resources_replace_one_slot_without_disturbing_another),
        CUBEY_TEST(test_render_graph_resolves_sampled_color_texture_view),
        CUBEY_TEST(test_render_graph_resolves_sampled_depth_texture_view),
        CUBEY_TEST(test_render_graph_sampled_texture_view_rejects_unallocated_transient),
        CUBEY_TEST(
            test_render_graph_execute_rejects_missing_callbacks_but_compile_allows_declarations),
        CUBEY_TEST(test_render_graph_execute_propagates_callback_exceptions),
        CUBEY_TEST(test_render_graph_execute_rejects_incompatible_resource_set),
        CUBEY_TEST(test_render_graph_execute_with_recorder_exposes_command_recorder),
        CUBEY_TEST(test_render_graph_recorder_access_rejects_recorderless_execution),
        CUBEY_TEST(test_render_graph_rejects_transient_texture_read_before_write),
        CUBEY_TEST(test_render_graph_allows_imported_texture_read_without_prior_write),
        CUBEY_TEST(test_render_graph_rejects_invalid_resource_descriptors_and_handles),
        CUBEY_TEST(test_render_graph_rejects_attachment_usage_outside_graphics_pass),
        CUBEY_TEST(test_render_graph_rejects_shader_stage_masks_outside_pass_domain),
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
        CUBEY_TEST(test_indexed_mesh_config_allows_storage_capable_vertex_buffers),
        CUBEY_TEST(test_primitive_vertex_layouts_match_shader_contracts),
        CUBEY_TEST(test_instance_buffer_helpers_describe_instance_vertex_data),
        CUBEY_TEST(test_color_space_converts_srgb_authored_values_to_linear),
        CUBEY_TEST(test_color_space_converts_hsv_and_hsl_authored_values),
        CUBEY_TEST(test_clipmap_grid_2d_emits_far_to_near_annular_patches),
        CUBEY_TEST(test_clipmap_grid_2d_rejects_invalid_config),
        CUBEY_TEST(test_adaptive_patch_lod_selects_quadtree_children),
        CUBEY_TEST(test_adaptive_patch_lod_hysteresis_delays_split_and_merge),
        CUBEY_TEST(test_adaptive_patch_lod_falls_back_at_patch_budget),
        CUBEY_TEST(test_adaptive_patch_lod_repairs_neighbor_deltas),
        CUBEY_TEST(test_adaptive_patch_lod_marks_edges_against_coarser_neighbors),
        CUBEY_TEST(test_adaptive_patch_lod_rejects_invalid_config_and_callbacks),
        CUBEY_TEST(test_local_tangent_frame_converts_between_world_and_local_space),
        CUBEY_TEST(test_local_tangent_frame_preserves_camera_relative_precision),
        CUBEY_TEST(test_local_tangent_frame_reports_height_above_water_datum),
        CUBEY_TEST(test_local_tangent_frame_rejects_invalid_basis),
        CUBEY_TEST(test_terrain_ocean_fields_pack_channel_layout_and_ranges),
        CUBEY_TEST(test_terrain_ocean_fields_reject_invalid_contract_data),
        CUBEY_TEST(test_primitive_cube_position_color_mesh_uses_face_colors_and_indices),
        CUBEY_TEST(test_primitive_cube_normal_uv_mesh_preserves_normals_and_face_uvs),
        CUBEY_TEST(test_primitive_xz_plane_mesh_uses_center_half_extents_and_up_normal),
        CUBEY_TEST(test_primitive_uv_sphere_mesh_uses_smooth_normals_and_uv_grid),
        CUBEY_TEST(test_render_item_validates_required_draw_identity),
        CUBEY_TEST(test_render_item_resolves_draw_item_fields),
        CUBEY_TEST(test_frame_mesh_table_resolves_per_frame_override),
        CUBEY_TEST(test_render_item_resolves_draw_item_from_frame_mesh_override),
        CUBEY_TEST(test_gpu_deformation_descriptor_set_declares_storage_buffers),
        CUBEY_TEST(test_gpu_deformation_descriptor_set_scales_for_frame_slots),
        CUBEY_TEST(test_gpu_deformation_dispatch_groups_round_up_vertices),
        CUBEY_TEST(test_gpu_deformation_pipeline_config_uses_compute_stage_and_push_constants),
        CUBEY_TEST(test_gpu_deformation_pipeline_config_accepts_descriptor_layouts),
        CUBEY_TEST(test_gpu_deformation_shader_morphs_before_skinning),
        CUBEY_TEST(test_render_pass_helpers_describe_clear_values_and_fullscreen_triangle),
        CUBEY_TEST(test_atmosphere_environment_packs_frame_uniforms),
        CUBEY_TEST(test_atmosphere_environment_packs_celestial_frame_uniforms),
        CUBEY_TEST(test_atmosphere_environment_resolves_celestial_time_math),
        CUBEY_TEST(test_atmosphere_environment_lighting_projects_diffuse_sh),
        CUBEY_TEST(test_atmosphere_background_pass_declares_frame_and_atlas_bindings),
        CUBEY_TEST(test_atmosphere_background_texture_bindings_reject_null_atlases),
        CUBEY_TEST(test_atmosphere_reflection_probe_declares_prefilter_pass),
        CUBEY_TEST(test_atmosphere_reflection_probe_uses_per_subresource_uniform_materials),
        CUBEY_TEST(test_pbr_vertex_layout_matches_shader_contract),
        CUBEY_TEST(test_pbr_debug_view_names_parse_and_cycle),
        CUBEY_TEST(test_pbr_forward_pass_declares_scene_and_material_sets),
        CUBEY_TEST(test_pbr_material_factors_are_uniforms_and_push_constants_are_model_only),
        CUBEY_TEST(test_pbr_default_texture_specs_cover_all_sampled_material_bindings),
        CUBEY_TEST(test_pbr_material_table_groups_factors_and_supports_lifetime_operations),
        CUBEY_TEST(test_pbr_material_table_tracks_descriptor_layout_explicitly),
        CUBEY_TEST(test_pbr_scene_uniforms_carry_display_transform),
        CUBEY_TEST(test_pbr_post_pass_declares_uniforms_and_scene_color),
        CUBEY_TEST(test_hdr_post_frame_helpers_pack_scene_color_and_display_transform),
        CUBEY_TEST(test_pbr_skybox_uniforms_are_uniform_buffer_safe),
        CUBEY_TEST(test_pbr_skybox_pass_declares_scene_set),
        CUBEY_TEST(test_pbr_reflectance_helpers_match_filament_convention),
        CUBEY_TEST(test_pbr_shaders_use_filament_style_material_remap),
        CUBEY_TEST(test_forward_pbr_shader_package_uses_renderer_names),
        CUBEY_TEST(test_gltf_material_fallback_textures_preserve_pbr_factor_channels),
        CUBEY_TEST(test_pbr_examples_and_gltf_importer_share_material_resources),
        CUBEY_TEST(test_pbr_consumers_use_atmosphere_lighting_foundation),
        CUBEY_TEST(test_pbr_diagnostics_are_exposed_in_gltf_viewer_and_material_cubes),
        CUBEY_TEST(test_gltf_basisu_transcoder_policy_uses_bc7_and_rgba_fallback),
        CUBEY_TEST(test_gltf_basisu_transcoder_uses_bundled_zstd),
        CUBEY_TEST(test_vulkan_and_gltf_sample_asset_cmake_paths_are_portable_and_pinned),
        CUBEY_TEST(test_gltf_viewer_sample_asset_smoke_tests_cover_material_and_tangent_cases),
        CUBEY_TEST(test_generated_pbr_environment_data_is_deterministic_and_sized),
        CUBEY_TEST(test_generated_pbr_dfg_lut_stores_energy_compensation_term),
        CUBEY_TEST(test_generated_pbr_prefilter_uses_ggx_convolution_not_legacy_average_mix),
        CUBEY_TEST(test_pbr_environment_texture_bindings_validate_required_views),
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
        CUBEY_TEST(test_view_ray_basis_packs_camera_axes_for_shader_contract),
        CUBEY_TEST(test_view_ray_basis_reconstructs_fullscreen_ray_directions),
        CUBEY_TEST(test_view_ray_basis_rejects_invalid_projection_inputs),
        CUBEY_TEST(test_depth_only_rendering_info_describes_sampled_depth_target),
        CUBEY_TEST(test_texture_2d_config_maps_storage_sampled_usage),
        CUBEY_TEST(test_texture_3d_config_maps_storage_sampled_volume_usage),
        CUBEY_TEST(test_texture_2d_config_maps_transfer_sampled_usage),
        CUBEY_TEST(test_texture_2d_config_preserves_mip_count),
        CUBEY_TEST(test_texture_2d_byte_size_uses_compressed_blocks),
        CUBEY_TEST(test_texture_cube_config_maps_transfer_sampled_cube_usage),
        CUBEY_TEST(test_texture_cube_config_maps_color_attachment_sampled_usage),
        CUBEY_TEST(test_depth_texture_config_maps_sampled_depth_usage),
        CUBEY_TEST(test_compute_generated_texture_config_validates_dispatch_shape),
        CUBEY_TEST(test_material_alpha_modes_map_to_blend_and_pass_policy),
        CUBEY_TEST(test_material_info_defaults_to_depth_and_forward_passes),
        CUBEY_TEST(test_material_instance_config_builds_descriptor_set_info),
        CUBEY_TEST(test_material_descriptor_writer_preserves_set_and_write_order),
        CUBEY_TEST(test_material_pass_masks_include_requested_passes),
        CUBEY_TEST(test_material_pass_info_validates_descriptor_and_push_constant_shape),
        CUBEY_TEST(test_push_constant_range_validation_enforces_device_limit_and_alignment),
        CUBEY_TEST(test_material_pass_info_applies_graphics_pipeline_state),
        CUBEY_TEST(test_material_pass_info_builds_descriptor_set_info),
    };
    return tests;
}

} // namespace cubey::tests
