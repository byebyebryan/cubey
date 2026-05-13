#include "test_registry.h"

#include <array>

void test_run_config_parses_png_output_path();
void test_run_cli_app_sets_default_title_and_returns_runner_status();
void test_camera_2d_pans_zooms_and_reports_view();
void test_camera_2d_clamps_scale();
void test_camera_3d_builds_projection_and_view_from_world_transform();
void test_camera_3d_supports_orthographic_projection();
void test_camera_3d_orbit_helper_matches_existing_cube_view();
void test_camera_manager_entity_destroy_retires_attached_components();
void test_camera_manager_rejects_duplicate_and_stale_entity_edits();
void test_camera_manager_updates_keep_epoch_local_snapshots();
void test_camera_managers_publish_2d_and_3d_camera_snapshots();
void test_capture_queue_encodes_png_with_inline_executor();
void test_capture_queue_propagates_encoding_errors();
void test_command_pool_exposes_command_buffer_ownership();
void test_command_recorder_exposes_non_owning_command_buffer_contract();
void test_command_recorder_rejects_invalid_recording_inputs_before_vulkan_calls();
void test_compute_helpers_describe_pipeline_and_layout_setup();
void test_deferred_gpu_destruction_queue_retires_completed_tickets();
void test_descriptor_helpers_describe_layout_pool_and_writes();
void test_descriptor_write_batch_owns_write_storage_and_preserves_order();
void test_descriptor_set_info_copies_bindings_and_aggregates_pool_sizes();
void test_descriptor_set_allocate_info_describes_multiple_sets();
void test_dynamic_rendering_describes_attachment_setup();
void test_engine_creates_independent_scenes();
void test_engine_destroys_owned_scenes_and_rejects_foreign_scenes();
void test_engine_exposes_render_resource_registry();
void test_engine_created_scenes_validate_render_resource_handles();
void test_engine_attaches_gpu_services_to_project_context();
void test_engine_exposes_project_runtime_services();
void test_engine_reuses_project_frame_for_same_timing();
void test_entity_handles_track_null_reserved_and_alive_states();
void test_entity_manager_concurrent_reservations_are_unique();
void test_entity_manager_invalidates_generations_and_defers_reuse();
void test_entity_manager_rolls_back_reserved_entities();
void test_file_io_round_trips_binary_bytes();
void test_frame_clock_tracks_delta_elapsed_and_index();
void test_frame_resources_expose_slot_based_contract();
void test_host_frame_stats_publish_window_title_metrics();
void test_gpu_submission_ticket_issuer_returns_monotonic_tickets();
void test_gpu_runtime_accepts_cross_thread_enqueue_but_rejects_cross_thread_drain();
void test_gpu_runtime_defaults_to_threaded_execution();
void test_gpu_runtime_drains_inline_on_owner_thread();
void test_gpu_runtime_mark_submission_completed_updates_completed_ticket();
void test_gpu_runtime_preserves_pending_work_after_callback_failure();
void test_gpu_runtime_shutdown_rejects_new_work();
void test_gpu_runtime_submit_and_wait_propagates_threaded_failures();
void test_gpu_runtime_wait_queue_idle_runs_on_owner_thread();
void test_gpu_work_queue_drains_fifo_and_owns_requests();
void test_headless_png_host_validates_capture_shape();
void test_image_io_writes_rgba_png();
void test_image_transitions_describe_layout_barriers();
void test_immediate_commands_accepts_submission_coordinator();
void test_input_state_accumulates_cursor_and_scroll_per_frame();
void test_input_state_ignores_unknown_inputs();
void test_input_state_tracks_key_and_button_edges();
void test_pan_zoom_2d_controller_pans_and_zooms_from_input();
void test_pointer_drag_tracks_active_cursor_and_accumulated_delta();
void test_inline_executor_runs_jobs_immediately();
void test_job_system_runs_jobs_and_propagates_errors();
void test_job_system_shutdown_rejects_new_jobs();
void test_light_manager_entity_destroy_retires_attached_components();
void test_light_manager_publishes_3d_packets_from_scene_read_view();
void test_light_manager_rejects_invalid_edits();
void test_light_manager_skips_invisible_lights_without_transform();
void test_light_manager_updates_keep_epoch_local_snapshots();
void test_light_packets_require_transform_for_point_lights();
void test_material_info_defaults_to_depth_and_forward_passes();
void test_material_descriptor_writer_preserves_set_and_write_order();
void test_material_instance_config_builds_descriptor_set_info();
void test_material_pass_info_applies_graphics_pipeline_state();
void test_material_pass_info_builds_descriptor_set_info();
void test_material_pass_info_validates_descriptor_and_push_constant_shape();
void test_material_pass_masks_include_requested_passes();
void test_math_helpers_match_vulkan_projection_conventions();
void test_math_quaternion_helpers_match_rotation_matrices();
void test_orbit_controller_tracks_rotation_drag_pause_and_reset();
void test_orbit_controller_updates_from_input_snapshot();
void test_pipeline_helpers_describe_dynamic_graphics_pipeline_setup();
void test_pipeline_helpers_describe_depth_only_dynamic_graphics_pipeline_setup();
void test_project_context_exposes_async_runtime_services();
void test_project_context_exposes_optional_gpu_services();
void test_project_gpu_services_enqueue_uploads_and_retire_completed_gpu_work();
void test_project_gpu_services_rejects_invalid_rgba8_readback_requests();
void test_project_gpu_services_tracks_failed_rgba8_readbacks();
void test_project_gpu_services_mark_failed_uploads();
void test_project_gpu_services_submit_and_wait_runs_on_owner_thread();
void test_project_gpu_services_wait_queue_idle_runs_on_owner_thread();
void test_project_runtime_contract_supports_lifecycle_shape();
void test_project_runtime_adapter_exposes_context_and_retirement();
void test_project_runtime_adapter_attaches_gpu_services_to_context();
void test_project_runtime_adapter_reuses_frame_for_same_timing();
void test_project_runtime_services_create_project_frames_and_context();
void test_queue_submit_info_describes_waits_commands_signals_and_fence();
void test_queue_submit_rejects_empty_command_buffer_list();
void test_render_context_exposes_explicit_frame_boundary();
void test_frame_slot_defaults_to_single_frame_slot();
void test_frame_slot_wraps_frame_indices();
void test_frame_slot_rejects_invalid_slots();
void test_frame_uniform_buffer_config_describes_host_visible_uniform_storage();
void test_frame_uniform_buffer_contract_is_slot_based_and_move_only();
void test_render_graph_allows_imported_texture_read_without_prior_write();
void test_render_graph_barrier_recording_rejects_unallocated_transient_resources();
void test_render_graph_creates_transient_texture_and_preserves_pass_order();
void test_render_graph_derives_compute_to_graphics_storage_buffer_barrier();
void test_render_graph_derives_depth_to_sampled_texture_barrier();
void test_render_graph_derives_imported_buffer_acquire_and_release_barriers();
void test_render_graph_derives_imported_texture_acquire_and_release_barriers();
void test_render_graph_derives_transient_texture_first_use_barrier();
void test_render_graph_declares_compute_storage_buffer_flow();
void test_render_graph_declares_shadow_map_then_scene_sample_flow();
void test_render_graph_execute_propagates_callback_exceptions();
void test_render_graph_execute_rejects_missing_callbacks_but_compile_allows_declarations();
void test_render_graph_execute_with_recorder_exposes_command_recorder();
void test_render_graph_executes_callbacks_in_pass_order_and_exposes_context();
void test_render_graph_execution_resolves_bound_transient_resources();
void test_render_graph_frame_executor_tracks_slots_and_rejects_invalid_record_info();
void test_render_graph_frame_resources_manage_frame_slots();
void test_render_graph_frame_resources_reject_invalid_slots();
void test_render_graph_frame_resources_replace_one_slot_without_disturbing_another();
void test_render_graph_imports_color_and_depth_targets();
void test_render_graph_omits_read_after_read_barriers();
void test_render_graph_preserves_material_pass_metadata();
void test_render_graph_rejects_attachment_usage_outside_graphics_pass();
void test_render_graph_resolved_color_target_view_rejects_depth_texture();
void test_render_graph_resolves_color_target_view_from_bound_transient_texture();
void test_render_graph_resolves_sampled_color_texture_view();
void test_render_graph_resolves_sampled_depth_texture_view();
void test_render_graph_sampled_texture_view_rejects_unallocated_transient();
void test_render_graph_rejects_incompatible_same_pass_resource_access();
void test_render_graph_rejects_invalid_resource_descriptors_and_handles();
void test_render_graph_recorder_access_rejects_recorderless_execution();
void test_render_graph_rejects_transient_texture_read_before_write();
void test_render_graph_storage_read_write_initializes_transient_buffers();
void test_render_graph_transfer_pass_accepts_only_transfer_usages();
void test_render_item_resolves_draw_item_fields();
void test_render_item_validates_required_draw_identity();
void test_render_pass_helpers_describe_clear_values_and_fullscreen_triangle();
void test_render_pipeline_resource_allows_vertexless_fullscreen_pipeline_shape();
void test_render_pipeline_resource_builds_layout_and_dynamic_pipeline_info();
void test_render_pipeline_resource_builds_compute_pipeline_info();
void test_render_pipeline_resource_helpers_build_file_recipe_config();
void test_primitive_cube_normal_uv_mesh_preserves_normals_and_face_uvs();
void test_primitive_cube_position_color_mesh_uses_face_colors_and_indices();
void test_instance_buffer_helpers_describe_instance_vertex_data();
void test_primitive_vertex_layouts_match_shader_contracts();
void test_primitive_xz_plane_mesh_uses_center_half_extents_and_up_normal();
void test_compute_generated_texture_config_validates_dispatch_shape();
void test_render_plan_builds_sorted_3d_draw_packets_with_material_metadata();
void test_render_plan_converts_draw_packets_to_render_items();
void test_render_plan_filters_draw_packets_for_recording_policy();
void test_render_recording_rejects_ambiguous_material_binding_sources();
void test_render_plan_rejects_stale_resource_handles();
void test_render_resource_registry_tracks_handle_lifetime_and_labels();
void test_render_resource_handles_are_hashable_keys();
void test_render_resource_registry_round_trips_mesh_and_material_info();
void test_render_resource_table_resolves_move_only_resources_by_handle();
void test_render_target_views_describe_color_only_targets();
void test_render_target_rendering_info_describes_dynamic_rendering();
void test_depth_only_rendering_info_describes_sampled_depth_target();
void test_texture_2d_config_maps_storage_sampled_usage();
void test_texture_2d_config_maps_transfer_sampled_usage();
void test_depth_texture_config_maps_sampled_depth_usage();
void test_render_view_3d_builds_frame_plan_with_environment_draws_and_lights();
void test_render_view_3d_builds_multiple_view_plans_from_one_scene_read_view();
void test_render_view_3d_frame_pass_plan_preserves_explicit_pass_order();
void test_render_view_3d_frustum_culls_world_bounds_and_can_be_disabled();
void test_render_view_3d_preserves_draw_sorting_and_stale_handle_validation();
void test_render_view_3d_rejects_invalid_view_or_missing_camera_transform();
void test_renderable_manager_emits_multiple_visible_primitives();
void test_renderable_manager_entity_destroy_retires_attached_components();
void test_renderable_manager_publishes_3d_packets_from_scene_read_view();
void test_renderable_manager_rejects_invalid_edits_and_missing_transforms();
void test_renderable_manager_skips_invisible_renderables_without_transform();
void test_renderable_manager_updates_keep_epoch_local_snapshots();
void test_resource_helpers_describe_device_local_upload_and_depth_setup();
void test_sampler_config_describes_shadow_sampling();
void test_scene_edit_queue_publishes_reserved_entities_on_commit();
void test_scene_builder_creates_common_3d_entities();
void test_scene_failed_commit_rolls_back_reserved_entities();
void test_scene_read_views_defer_destroyed_entity_reuse_until_release();
void test_shader_bytecode_reads_aligned_spirv_words();
void test_shader_bytecode_rejects_misaligned_spirv_byte_count();
void test_submission_coordinator_completion_tracking_rejects_future_tickets();
void test_submission_coordinator_failed_submit_does_not_issue_ticket();
void test_submission_coordinator_issues_monotonic_gpu_tickets();
void test_submission_coordinator_submit_and_wait_marks_completion();
void test_stable_slot_store_rejects_stale_handles_without_moving_other_slots();
void test_transform_manager_2d_publishes_parented_world_matrices();
void test_transform_manager_3d_publishes_parented_world_matrices();
void test_transform_manager_read_views_keep_epoch_local_snapshots();
void test_transform_manager_rejects_invalid_parenting_and_child_destroy();
void test_transform_manager_reparenting_preserves_local_transform();
void test_transform_2d_builds_affine_matrix();
void test_transform_3d_builds_affine_matrix();
void test_transform_3d_matches_existing_cube_rotation_order();
void test_transfer_helpers_describe_texture_and_readback_paths();
void test_upload_queue_drains_in_submission_order();
void test_upload_queue_owns_payload_until_drain();
void test_upload_queue_tracks_failed_uploads();
void test_windowed_host_config_defaults_to_two_frame_slots();
void test_windowed_app_config_preserves_windowed_host_defaults();

namespace cubey::tests {
namespace {

#define CUBEY_TEST(test_function)                                                                  \
    TestCase {                                                                                     \
        #test_function, &test_function                                                             \
    }

} // namespace

std::span<const TestCase> core_tests() {
    static constexpr std::array tests{
        CUBEY_TEST(test_run_config_parses_png_output_path),
        CUBEY_TEST(test_run_cli_app_sets_default_title_and_returns_runner_status),
        CUBEY_TEST(test_camera_2d_pans_zooms_and_reports_view),
        CUBEY_TEST(test_camera_2d_clamps_scale),
        CUBEY_TEST(test_camera_3d_builds_projection_and_view_from_world_transform),
        CUBEY_TEST(test_camera_3d_supports_orthographic_projection),
        CUBEY_TEST(test_camera_3d_orbit_helper_matches_existing_cube_view),
        CUBEY_TEST(test_camera_managers_publish_2d_and_3d_camera_snapshots),
        CUBEY_TEST(test_camera_manager_updates_keep_epoch_local_snapshots),
        CUBEY_TEST(test_camera_manager_entity_destroy_retires_attached_components),
        CUBEY_TEST(test_camera_manager_rejects_duplicate_and_stale_entity_edits),
        CUBEY_TEST(test_capture_queue_encodes_png_with_inline_executor),
        CUBEY_TEST(test_capture_queue_propagates_encoding_errors),
        CUBEY_TEST(test_command_pool_exposes_command_buffer_ownership),
        CUBEY_TEST(test_command_recorder_exposes_non_owning_command_buffer_contract),
        CUBEY_TEST(test_command_recorder_rejects_invalid_recording_inputs_before_vulkan_calls),
        CUBEY_TEST(test_compute_helpers_describe_pipeline_and_layout_setup),
        CUBEY_TEST(test_deferred_gpu_destruction_queue_retires_completed_tickets),
        CUBEY_TEST(test_descriptor_helpers_describe_layout_pool_and_writes),
        CUBEY_TEST(test_descriptor_write_batch_owns_write_storage_and_preserves_order),
        CUBEY_TEST(test_descriptor_set_info_copies_bindings_and_aggregates_pool_sizes),
        CUBEY_TEST(test_descriptor_set_allocate_info_describes_multiple_sets),
        CUBEY_TEST(test_dynamic_rendering_describes_attachment_setup),
        CUBEY_TEST(test_engine_exposes_project_runtime_services),
        CUBEY_TEST(test_engine_attaches_gpu_services_to_project_context),
        CUBEY_TEST(test_engine_reuses_project_frame_for_same_timing),
        CUBEY_TEST(test_engine_creates_independent_scenes),
        CUBEY_TEST(test_engine_destroys_owned_scenes_and_rejects_foreign_scenes),
        CUBEY_TEST(test_engine_exposes_render_resource_registry),
        CUBEY_TEST(test_engine_created_scenes_validate_render_resource_handles),
        CUBEY_TEST(test_entity_handles_track_null_reserved_and_alive_states),
        CUBEY_TEST(test_entity_manager_invalidates_generations_and_defers_reuse),
        CUBEY_TEST(test_entity_manager_rolls_back_reserved_entities),
        CUBEY_TEST(test_entity_manager_concurrent_reservations_are_unique),
        CUBEY_TEST(test_file_io_round_trips_binary_bytes),
        CUBEY_TEST(test_frame_clock_tracks_delta_elapsed_and_index),
        CUBEY_TEST(test_frame_resources_expose_slot_based_contract),
        CUBEY_TEST(test_host_frame_stats_publish_window_title_metrics),
        CUBEY_TEST(test_gpu_submission_ticket_issuer_returns_monotonic_tickets),
        CUBEY_TEST(test_gpu_work_queue_drains_fifo_and_owns_requests),
        CUBEY_TEST(test_gpu_runtime_defaults_to_threaded_execution),
        CUBEY_TEST(test_gpu_runtime_submit_and_wait_propagates_threaded_failures),
        CUBEY_TEST(test_gpu_runtime_wait_queue_idle_runs_on_owner_thread),
        CUBEY_TEST(test_gpu_runtime_mark_submission_completed_updates_completed_ticket),
        CUBEY_TEST(test_gpu_runtime_shutdown_rejects_new_work),
        CUBEY_TEST(test_gpu_runtime_drains_inline_on_owner_thread),
        CUBEY_TEST(test_gpu_runtime_accepts_cross_thread_enqueue_but_rejects_cross_thread_drain),
        CUBEY_TEST(test_gpu_runtime_preserves_pending_work_after_callback_failure),
        CUBEY_TEST(test_headless_png_host_validates_capture_shape),
        CUBEY_TEST(test_image_io_writes_rgba_png),
        CUBEY_TEST(test_image_transitions_describe_layout_barriers),
        CUBEY_TEST(test_immediate_commands_accepts_submission_coordinator),
        CUBEY_TEST(test_input_state_tracks_key_and_button_edges),
        CUBEY_TEST(test_input_state_accumulates_cursor_and_scroll_per_frame),
        CUBEY_TEST(test_input_state_ignores_unknown_inputs),
        CUBEY_TEST(test_pointer_drag_tracks_active_cursor_and_accumulated_delta),
        CUBEY_TEST(test_pan_zoom_2d_controller_pans_and_zooms_from_input),
        CUBEY_TEST(test_inline_executor_runs_jobs_immediately),
        CUBEY_TEST(test_job_system_runs_jobs_and_propagates_errors),
        CUBEY_TEST(test_job_system_shutdown_rejects_new_jobs),
        CUBEY_TEST(test_light_manager_publishes_3d_packets_from_scene_read_view),
        CUBEY_TEST(test_light_manager_updates_keep_epoch_local_snapshots),
        CUBEY_TEST(test_light_manager_entity_destroy_retires_attached_components),
        CUBEY_TEST(test_light_manager_rejects_invalid_edits),
        CUBEY_TEST(test_light_manager_skips_invisible_lights_without_transform),
        CUBEY_TEST(test_light_packets_require_transform_for_point_lights),
        CUBEY_TEST(test_material_info_defaults_to_depth_and_forward_passes),
        CUBEY_TEST(test_material_instance_config_builds_descriptor_set_info),
        CUBEY_TEST(test_material_descriptor_writer_preserves_set_and_write_order),
        CUBEY_TEST(test_material_pass_masks_include_requested_passes),
        CUBEY_TEST(test_material_pass_info_validates_descriptor_and_push_constant_shape),
        CUBEY_TEST(test_material_pass_info_applies_graphics_pipeline_state),
        CUBEY_TEST(test_material_pass_info_builds_descriptor_set_info),
        CUBEY_TEST(test_math_helpers_match_vulkan_projection_conventions),
        CUBEY_TEST(test_math_quaternion_helpers_match_rotation_matrices),
        CUBEY_TEST(test_orbit_controller_tracks_rotation_drag_pause_and_reset),
        CUBEY_TEST(test_orbit_controller_updates_from_input_snapshot),
        CUBEY_TEST(test_pipeline_helpers_describe_dynamic_graphics_pipeline_setup),
        CUBEY_TEST(test_pipeline_helpers_describe_depth_only_dynamic_graphics_pipeline_setup),
        CUBEY_TEST(test_project_context_exposes_async_runtime_services),
        CUBEY_TEST(test_project_context_exposes_optional_gpu_services),
        CUBEY_TEST(test_project_gpu_services_submit_and_wait_runs_on_owner_thread),
        CUBEY_TEST(test_project_gpu_services_wait_queue_idle_runs_on_owner_thread),
        CUBEY_TEST(test_project_gpu_services_enqueue_uploads_and_retire_completed_gpu_work),
        CUBEY_TEST(test_project_gpu_services_mark_failed_uploads),
        CUBEY_TEST(test_project_gpu_services_tracks_failed_rgba8_readbacks),
        CUBEY_TEST(test_project_gpu_services_rejects_invalid_rgba8_readback_requests),
        CUBEY_TEST(test_project_runtime_contract_supports_lifecycle_shape),
        CUBEY_TEST(test_project_runtime_adapter_reuses_frame_for_same_timing),
        CUBEY_TEST(test_project_runtime_adapter_exposes_context_and_retirement),
        CUBEY_TEST(test_project_runtime_adapter_attaches_gpu_services_to_context),
        CUBEY_TEST(test_project_runtime_services_create_project_frames_and_context),
        CUBEY_TEST(test_queue_submit_info_describes_waits_commands_signals_and_fence),
        CUBEY_TEST(test_queue_submit_rejects_empty_command_buffer_list),
        CUBEY_TEST(test_render_context_exposes_explicit_frame_boundary),
        CUBEY_TEST(test_frame_slot_defaults_to_single_frame_slot),
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
        CUBEY_TEST(test_render_pipeline_resource_builds_layout_and_dynamic_pipeline_info),
        CUBEY_TEST(test_render_pipeline_resource_allows_vertexless_fullscreen_pipeline_shape),
        CUBEY_TEST(test_render_pipeline_resource_builds_compute_pipeline_info),
        CUBEY_TEST(test_render_pipeline_resource_helpers_build_file_recipe_config),
        CUBEY_TEST(test_primitive_vertex_layouts_match_shader_contracts),
        CUBEY_TEST(test_instance_buffer_helpers_describe_instance_vertex_data),
        CUBEY_TEST(test_primitive_cube_position_color_mesh_uses_face_colors_and_indices),
        CUBEY_TEST(test_primitive_cube_normal_uv_mesh_preserves_normals_and_face_uvs),
        CUBEY_TEST(test_primitive_xz_plane_mesh_uses_center_half_extents_and_up_normal),
        CUBEY_TEST(test_render_item_validates_required_draw_identity),
        CUBEY_TEST(test_render_item_resolves_draw_item_fields),
        CUBEY_TEST(test_render_pass_helpers_describe_clear_values_and_fullscreen_triangle),
        CUBEY_TEST(test_render_plan_builds_sorted_3d_draw_packets_with_material_metadata),
        CUBEY_TEST(test_render_plan_converts_draw_packets_to_render_items),
        CUBEY_TEST(test_render_plan_filters_draw_packets_for_recording_policy),
        CUBEY_TEST(test_render_recording_rejects_ambiguous_material_binding_sources),
        CUBEY_TEST(test_render_plan_rejects_stale_resource_handles),
        CUBEY_TEST(test_render_resource_registry_tracks_handle_lifetime_and_labels),
        CUBEY_TEST(test_render_resource_handles_are_hashable_keys),
        CUBEY_TEST(test_render_resource_registry_round_trips_mesh_and_material_info),
        CUBEY_TEST(test_render_resource_table_resolves_move_only_resources_by_handle),
        CUBEY_TEST(test_render_target_views_describe_color_only_targets),
        CUBEY_TEST(test_render_target_rendering_info_describes_dynamic_rendering),
        CUBEY_TEST(test_depth_only_rendering_info_describes_sampled_depth_target),
        CUBEY_TEST(test_texture_2d_config_maps_storage_sampled_usage),
        CUBEY_TEST(test_texture_2d_config_maps_transfer_sampled_usage),
        CUBEY_TEST(test_depth_texture_config_maps_sampled_depth_usage),
        CUBEY_TEST(test_compute_generated_texture_config_validates_dispatch_shape),
        CUBEY_TEST(test_render_view_3d_builds_frame_plan_with_environment_draws_and_lights),
        CUBEY_TEST(test_render_view_3d_builds_multiple_view_plans_from_one_scene_read_view),
        CUBEY_TEST(test_render_view_3d_frame_pass_plan_preserves_explicit_pass_order),
        CUBEY_TEST(test_render_view_3d_rejects_invalid_view_or_missing_camera_transform),
        CUBEY_TEST(test_render_view_3d_frustum_culls_world_bounds_and_can_be_disabled),
        CUBEY_TEST(test_render_view_3d_preserves_draw_sorting_and_stale_handle_validation),
        CUBEY_TEST(test_renderable_manager_publishes_3d_packets_from_scene_read_view),
        CUBEY_TEST(test_renderable_manager_emits_multiple_visible_primitives),
        CUBEY_TEST(test_renderable_manager_skips_invisible_renderables_without_transform),
        CUBEY_TEST(test_renderable_manager_updates_keep_epoch_local_snapshots),
        CUBEY_TEST(test_renderable_manager_entity_destroy_retires_attached_components),
        CUBEY_TEST(test_renderable_manager_rejects_invalid_edits_and_missing_transforms),
        CUBEY_TEST(test_resource_helpers_describe_device_local_upload_and_depth_setup),
        CUBEY_TEST(test_sampler_config_describes_shadow_sampling),
        CUBEY_TEST(test_stable_slot_store_rejects_stale_handles_without_moving_other_slots),
        CUBEY_TEST(test_scene_builder_creates_common_3d_entities),
        CUBEY_TEST(test_scene_edit_queue_publishes_reserved_entities_on_commit),
        CUBEY_TEST(test_scene_failed_commit_rolls_back_reserved_entities),
        CUBEY_TEST(test_scene_read_views_defer_destroyed_entity_reuse_until_release),
        CUBEY_TEST(test_shader_bytecode_reads_aligned_spirv_words),
        CUBEY_TEST(test_shader_bytecode_rejects_misaligned_spirv_byte_count),
        CUBEY_TEST(test_submission_coordinator_issues_monotonic_gpu_tickets),
        CUBEY_TEST(test_submission_coordinator_submit_and_wait_marks_completion),
        CUBEY_TEST(test_submission_coordinator_completion_tracking_rejects_future_tickets),
        CUBEY_TEST(test_submission_coordinator_failed_submit_does_not_issue_ticket),
        CUBEY_TEST(test_transform_manager_2d_publishes_parented_world_matrices),
        CUBEY_TEST(test_transform_manager_3d_publishes_parented_world_matrices),
        CUBEY_TEST(test_transform_manager_reparenting_preserves_local_transform),
        CUBEY_TEST(test_transform_manager_read_views_keep_epoch_local_snapshots),
        CUBEY_TEST(test_transform_manager_rejects_invalid_parenting_and_child_destroy),
        CUBEY_TEST(test_transform_2d_builds_affine_matrix),
        CUBEY_TEST(test_transform_3d_builds_affine_matrix),
        CUBEY_TEST(test_transform_3d_matches_existing_cube_rotation_order),
        CUBEY_TEST(test_transfer_helpers_describe_texture_and_readback_paths),
        CUBEY_TEST(test_upload_queue_owns_payload_until_drain),
        CUBEY_TEST(test_upload_queue_drains_in_submission_order),
        CUBEY_TEST(test_upload_queue_tracks_failed_uploads),
        CUBEY_TEST(test_windowed_host_config_defaults_to_two_frame_slots),
        CUBEY_TEST(test_windowed_app_config_preserves_windowed_host_defaults),
    };
    return tests;
}

} // namespace cubey::tests
