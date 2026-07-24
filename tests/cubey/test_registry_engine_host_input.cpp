#include "test_registry_common.h"

void test_capture_queue_encodes_png_with_inline_executor();
void test_capture_queue_propagates_encoding_errors();
void test_capture_queue_creates_png_parent_directories();
void test_capture_backlog_drains_at_threshold();
void test_capture_queue_encodes_video_frames_in_order();
void test_capture_queue_video_encoder_rejects_dimension_mismatch();
void test_capture_queue_video_encoder_propagates_worker_errors();
void test_atmosphere_environment_runtime_derives_lighting_and_scene_environment();
void test_atmosphere_environment_runtime_reports_changed_environment();
void test_atmosphere_reflection_probe_timeline_publishes_coherent_captures();
void test_atmosphere_reflection_probe_timeline_is_change_driven();
void test_atmosphere_environment_runtime_builds_frame_payload();
void test_atmosphere_environment_runtime_builds_celestial_frame_payload();
void test_cloud_environment_runtime_builds_coherent_surface_frame();
void test_atmosphere_environment_runtime_owns_optional_cloud_foundation();
void test_atmosphere_environment_runtime_requires_resources_before_bindings();
void test_atmosphere_environment_runtime_publishes_coherent_probe_updates();
void test_engine_attaches_gpu_services_to_project_context();
void test_engine_created_scenes_validate_render_resource_handles();
void test_engine_creates_independent_scenes();
void test_engine_destroys_owned_scenes_and_rejects_foreign_scenes();
void test_engine_exposes_project_runtime_services();
void test_engine_exposes_render_resource_registry();
void test_engine_exposes_renderer_service();
void test_engine_reuses_project_frame_for_same_timing();
void test_renderer_service_owns_forward_pbr_renderer_instances();
void test_renderer_service_rejects_foreign_forward_pbr_renderer();
void test_renderer_service_resource_lifecycle_is_safe_without_renderers();
void test_staged_resource_finishes_owned_cpu_and_gpu_stages();
void test_staged_resource_keeps_only_latest_pending_generation();
void test_staged_resource_poll_does_not_wait_for_cpu_preparation();
void test_staged_resource_reports_prepare_and_install_failures();
void test_staged_resource_shutdown_discards_work_and_rejects_requests();
void test_headless_capture_frame_helpers_select_png_or_video_timing();
void test_headless_png_host_validates_capture_shape();
void test_host_frame_stats_publish_window_title_metrics();
void test_input_state_accumulates_cursor_and_scroll_per_frame();
void test_filtered_input_frame_masks_captured_ui_channels();
void test_input_state_ignores_unknown_inputs();
void test_input_state_tracks_key_and_button_edges();
void test_input_state_tracks_wasd_keys();
void test_orbit_controller_tracks_rotation_drag_pause_and_reset();
void test_orbit_controller_updates_from_input_snapshot();
void test_orbit_controller_scroll_zoom_clamps_distance();
void test_orbit_controller_supports_configurable_pitch_limits();
void test_pan_zoom_2d_controller_pans_and_zooms_from_input();
void test_forward_pbr_renderer_3d_config_requires_shader_paths_and_shadow_extent();
void test_forward_pbr_renderer_3d_config_defaults_to_hdr_scene_color();
void test_forward_pbr_renderer_3d_config_from_shader_directory_fills_package_paths();
void test_forward_pbr_renderer_3d_config_from_shader_directory_rejects_empty_directory();
void test_forward_pbr_renderer_3d_target_resources_use_material_table();
void test_forward_pbr_renderer_3d_builds_render_request_from_frame_info();
void test_forward_pbr_renderer_3d_record_accepts_frame_request_info();
void test_forward_pbr_renderer_3d_record_requires_created_resources();
void test_forward_pbr_renderer_3d_lifecycle_guards_resource_ordering();
void test_forward_pbr_renderer_3d_binds_shadow_depth_with_depth_read_layout();
void test_forward_pbr_renderer_3d_records_masked_shadow_path_with_material_alpha();
void test_forward_pbr_renderer_3d_post_uniforms_pack_display_transform();
void test_forward_pbr_renderer_3d_frame_plan_selects_required_passes();
void test_forward_pbr_renderer_3d_render_request_validates_required_resource_fields();
void test_forward_pbr_renderer_3d_render_request_validates_atmosphere_background_uniforms();
void test_forward_pbr_renderer_3d_render_request_validates_atmosphere_clouds();
void test_forward_pbr_renderer_3d_render_request_validates_terrain_backdrop();
void test_forward_pbr_renderer_3d_render_request_validates_ocean_surface();
void test_terrain_backdrop_reflection_uses_product_materials_lighting_and_horizon();
void test_ocean_surface_reflection_uses_water_material_lighting_and_horizon();
void test_forward_pbr_renderer_3d_render_request_validates_required_target_fields();
void test_forward_pbr_renderer_3d_render_request_validates_required_view_fields();
void test_forward_pbr_renderer_3d_scene_uniforms_pack_view_light_environment_and_display();
void test_forward_pbr_renderer_3d_selects_requested_light_or_fallback();
void test_forward_pbr_renderer_3d_settings_defaults_to_aces_display_transform();
void test_forward_pbr_renderer_3d_shadow_vertex_layout_matches_pbr_vertices();
void test_forward_pbr_renderer_3d_skybox_uniforms_pack_inverse_view_camera_environment_and_display();
void test_forward_pbr_renderer_3d_threads_atmosphere_background_path();
void test_forward_pbr_renderer_3d_threads_debug_view_into_shader_and_scene_pass();
void test_gltf_scene_importer_applies_rigid_animation_samples_to_imported_nodes();
void test_gltf_scene_importer_preserves_matrix_nodes_and_animation_returns_to_trs();
void test_gltf_scene_importer_classifies_deformable_primitives();
void test_gltf_scene_importer_validates_deformation_inputs_and_culling_policy();
void test_pointer_drag_tracks_active_cursor_and_accumulated_delta();
void test_project_context_exposes_async_runtime_services();
void test_project_context_exposes_optional_gpu_services();
void test_project_gpu_services_enqueue_uploads_and_track_completion();
void test_project_gpu_services_mark_failed_uploads();
void test_project_gpu_services_rejects_invalid_rgba8_readback_requests();
void test_project_gpu_services_submit_and_wait_runs_on_owner_thread();
void test_project_gpu_services_tracks_failed_rgba8_readbacks();
void test_project_gpu_services_wait_queue_idle_runs_on_owner_thread();
void test_project_runtime_adapter_attaches_gpu_services_to_context();
void test_project_runtime_adapter_exposes_context();
void test_project_runtime_adapter_reuses_frame_for_same_timing();
void test_project_runtime_contract_supports_lifecycle_shape();
void test_project_runtime_services_create_project_frames_and_context();
void test_upload_queue_drains_in_submission_order();
void test_upload_queue_owns_payload_until_drain();
void test_upload_queue_tracks_failed_uploads();
void test_windowed_app_config_preserves_windowed_host_defaults();
void test_windowed_host_config_defaults_to_two_frame_slots();
void test_atmosphere_environment_run_config_resolves_manual_and_solar_modes();
void test_atmosphere_environment_run_config_advances_dynamic_time();
void test_atmosphere_environment_look_options_apply_without_time_or_sun();
void test_atmosphere_environment_run_state_resolves_control_mutations();

namespace cubey::tests {

std::span<const TestCase> engine_host_input_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_capture_queue_encodes_png_with_inline_executor),
        CUBEY_TEST(test_capture_queue_propagates_encoding_errors),
        CUBEY_TEST(test_capture_queue_creates_png_parent_directories),
        CUBEY_TEST(test_capture_backlog_drains_at_threshold),
        CUBEY_TEST(test_capture_queue_encodes_video_frames_in_order),
        CUBEY_TEST(test_capture_queue_video_encoder_rejects_dimension_mismatch),
        CUBEY_TEST(test_capture_queue_video_encoder_propagates_worker_errors),
        CUBEY_TEST(test_atmosphere_environment_run_config_resolves_manual_and_solar_modes),
        CUBEY_TEST(test_atmosphere_environment_run_config_advances_dynamic_time),
        CUBEY_TEST(test_atmosphere_environment_look_options_apply_without_time_or_sun),
        CUBEY_TEST(test_atmosphere_environment_run_state_resolves_control_mutations),
        CUBEY_TEST(test_atmosphere_environment_runtime_derives_lighting_and_scene_environment),
        CUBEY_TEST(test_atmosphere_environment_runtime_reports_changed_environment),
        CUBEY_TEST(test_atmosphere_reflection_probe_timeline_publishes_coherent_captures),
        CUBEY_TEST(test_atmosphere_reflection_probe_timeline_is_change_driven),
        CUBEY_TEST(test_atmosphere_environment_runtime_builds_frame_payload),
        CUBEY_TEST(test_atmosphere_environment_runtime_builds_celestial_frame_payload),
        CUBEY_TEST(test_cloud_environment_runtime_builds_coherent_surface_frame),
        CUBEY_TEST(test_atmosphere_environment_runtime_owns_optional_cloud_foundation),
        CUBEY_TEST(test_atmosphere_environment_runtime_requires_resources_before_bindings),
        CUBEY_TEST(test_atmosphere_environment_runtime_publishes_coherent_probe_updates),
        CUBEY_TEST(test_engine_exposes_project_runtime_services),
        CUBEY_TEST(test_engine_attaches_gpu_services_to_project_context),
        CUBEY_TEST(test_engine_reuses_project_frame_for_same_timing),
        CUBEY_TEST(test_engine_creates_independent_scenes),
        CUBEY_TEST(test_engine_destroys_owned_scenes_and_rejects_foreign_scenes),
        CUBEY_TEST(test_engine_exposes_render_resource_registry),
        CUBEY_TEST(test_engine_created_scenes_validate_render_resource_handles),
        CUBEY_TEST(test_engine_exposes_renderer_service),
        CUBEY_TEST(test_renderer_service_owns_forward_pbr_renderer_instances),
        CUBEY_TEST(test_renderer_service_rejects_foreign_forward_pbr_renderer),
        CUBEY_TEST(test_renderer_service_resource_lifecycle_is_safe_without_renderers),
        CUBEY_TEST(test_staged_resource_finishes_owned_cpu_and_gpu_stages),
        CUBEY_TEST(test_staged_resource_poll_does_not_wait_for_cpu_preparation),
        CUBEY_TEST(test_staged_resource_keeps_only_latest_pending_generation),
        CUBEY_TEST(test_staged_resource_reports_prepare_and_install_failures),
        CUBEY_TEST(test_staged_resource_shutdown_discards_work_and_rejects_requests),
        CUBEY_TEST(test_forward_pbr_renderer_3d_config_requires_shader_paths_and_shadow_extent),
        CUBEY_TEST(test_forward_pbr_renderer_3d_config_defaults_to_hdr_scene_color),
        CUBEY_TEST(test_forward_pbr_renderer_3d_config_from_shader_directory_fills_package_paths),
        CUBEY_TEST(
            test_forward_pbr_renderer_3d_config_from_shader_directory_rejects_empty_directory),
        CUBEY_TEST(test_forward_pbr_renderer_3d_target_resources_use_material_table),
        CUBEY_TEST(test_forward_pbr_renderer_3d_builds_render_request_from_frame_info),
        CUBEY_TEST(test_forward_pbr_renderer_3d_record_accepts_frame_request_info),
        CUBEY_TEST(test_forward_pbr_renderer_3d_record_requires_created_resources),
        CUBEY_TEST(test_forward_pbr_renderer_3d_lifecycle_guards_resource_ordering),
        CUBEY_TEST(test_forward_pbr_renderer_3d_binds_shadow_depth_with_depth_read_layout),
        CUBEY_TEST(test_forward_pbr_renderer_3d_records_masked_shadow_path_with_material_alpha),
        CUBEY_TEST(test_forward_pbr_renderer_3d_render_request_validates_required_target_fields),
        CUBEY_TEST(test_forward_pbr_renderer_3d_render_request_validates_required_view_fields),
        CUBEY_TEST(test_forward_pbr_renderer_3d_render_request_validates_required_resource_fields),
        CUBEY_TEST(
            test_forward_pbr_renderer_3d_render_request_validates_atmosphere_background_uniforms),
        CUBEY_TEST(test_forward_pbr_renderer_3d_render_request_validates_atmosphere_clouds),
        CUBEY_TEST(test_forward_pbr_renderer_3d_render_request_validates_terrain_backdrop),
        CUBEY_TEST(test_forward_pbr_renderer_3d_render_request_validates_ocean_surface),
        CUBEY_TEST(test_terrain_backdrop_reflection_uses_product_materials_lighting_and_horizon),
        CUBEY_TEST(test_ocean_surface_reflection_uses_water_material_lighting_and_horizon),
        CUBEY_TEST(test_forward_pbr_renderer_3d_frame_plan_selects_required_passes),
        CUBEY_TEST(test_forward_pbr_renderer_3d_settings_defaults_to_aces_display_transform),
        CUBEY_TEST(test_forward_pbr_renderer_3d_selects_requested_light_or_fallback),
        CUBEY_TEST(test_forward_pbr_renderer_3d_shadow_vertex_layout_matches_pbr_vertices),
        CUBEY_TEST(
            test_forward_pbr_renderer_3d_scene_uniforms_pack_view_light_environment_and_display),
        CUBEY_TEST(
            test_forward_pbr_renderer_3d_skybox_uniforms_pack_inverse_view_camera_environment_and_display),
        CUBEY_TEST(test_forward_pbr_renderer_3d_threads_atmosphere_background_path),
        CUBEY_TEST(test_forward_pbr_renderer_3d_threads_debug_view_into_shader_and_scene_pass),
        CUBEY_TEST(test_forward_pbr_renderer_3d_post_uniforms_pack_display_transform),
        CUBEY_TEST(test_gltf_scene_importer_applies_rigid_animation_samples_to_imported_nodes),
        CUBEY_TEST(test_gltf_scene_importer_preserves_matrix_nodes_and_animation_returns_to_trs),
        CUBEY_TEST(test_gltf_scene_importer_classifies_deformable_primitives),
        CUBEY_TEST(test_gltf_scene_importer_validates_deformation_inputs_and_culling_policy),
        CUBEY_TEST(test_headless_png_host_validates_capture_shape),
        CUBEY_TEST(test_headless_capture_frame_helpers_select_png_or_video_timing),
        CUBEY_TEST(test_host_frame_stats_publish_window_title_metrics),
        CUBEY_TEST(test_input_state_tracks_key_and_button_edges),
        CUBEY_TEST(test_input_state_tracks_wasd_keys),
        CUBEY_TEST(test_input_state_accumulates_cursor_and_scroll_per_frame),
        CUBEY_TEST(test_input_state_ignores_unknown_inputs),
        CUBEY_TEST(test_filtered_input_frame_masks_captured_ui_channels),
        CUBEY_TEST(test_pointer_drag_tracks_active_cursor_and_accumulated_delta),
        CUBEY_TEST(test_pan_zoom_2d_controller_pans_and_zooms_from_input),
        CUBEY_TEST(test_orbit_controller_tracks_rotation_drag_pause_and_reset),
        CUBEY_TEST(test_orbit_controller_updates_from_input_snapshot),
        CUBEY_TEST(test_orbit_controller_scroll_zoom_clamps_distance),
        CUBEY_TEST(test_orbit_controller_supports_configurable_pitch_limits),
        CUBEY_TEST(test_project_context_exposes_async_runtime_services),
        CUBEY_TEST(test_project_context_exposes_optional_gpu_services),
        CUBEY_TEST(test_project_gpu_services_submit_and_wait_runs_on_owner_thread),
        CUBEY_TEST(test_project_gpu_services_wait_queue_idle_runs_on_owner_thread),
        CUBEY_TEST(test_project_gpu_services_enqueue_uploads_and_track_completion),
        CUBEY_TEST(test_project_gpu_services_mark_failed_uploads),
        CUBEY_TEST(test_project_gpu_services_tracks_failed_rgba8_readbacks),
        CUBEY_TEST(test_project_gpu_services_rejects_invalid_rgba8_readback_requests),
        CUBEY_TEST(test_project_runtime_contract_supports_lifecycle_shape),
        CUBEY_TEST(test_project_runtime_adapter_reuses_frame_for_same_timing),
        CUBEY_TEST(test_project_runtime_adapter_exposes_context),
        CUBEY_TEST(test_project_runtime_adapter_attaches_gpu_services_to_context),
        CUBEY_TEST(test_project_runtime_services_create_project_frames_and_context),
        CUBEY_TEST(test_upload_queue_owns_payload_until_drain),
        CUBEY_TEST(test_upload_queue_drains_in_submission_order),
        CUBEY_TEST(test_upload_queue_tracks_failed_uploads),
        CUBEY_TEST(test_windowed_host_config_defaults_to_two_frame_slots),
        CUBEY_TEST(test_windowed_app_config_preserves_windowed_host_defaults),
    };
    return tests;
}

} // namespace cubey::tests
