#include "test_registry_common.h"

void test_capture_queue_encodes_png_with_inline_executor();
void test_capture_queue_propagates_encoding_errors();
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
void test_headless_capture_frame_helpers_select_png_or_video_timing();
void test_headless_png_host_validates_capture_shape();
void test_host_frame_stats_publish_window_title_metrics();
void test_input_state_accumulates_cursor_and_scroll_per_frame();
void test_filtered_input_frame_masks_captured_ui_channels();
void test_input_state_ignores_unknown_inputs();
void test_input_state_tracks_key_and_button_edges();
void test_orbit_controller_tracks_rotation_drag_pause_and_reset();
void test_orbit_controller_updates_from_input_snapshot();
void test_orbit_controller_scroll_zoom_clamps_distance();
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
void test_forward_pbr_renderer_3d_render_request_validates_required_target_fields();
void test_forward_pbr_renderer_3d_render_request_validates_required_view_fields();
void test_forward_pbr_renderer_3d_scene_uniforms_pack_view_light_environment_and_display();
void test_forward_pbr_renderer_3d_selects_requested_light_or_fallback();
void test_forward_pbr_renderer_3d_settings_defaults_to_aces_display_transform();
void test_forward_pbr_renderer_3d_shadow_vertex_layout_matches_pbr_vertices();
void test_forward_pbr_renderer_3d_skybox_uniforms_pack_inverse_view_camera_environment_and_display();
void test_forward_pbr_renderer_3d_threads_debug_view_into_shader_and_scene_pass();
void test_gltf_scene_importer_applies_rigid_animation_samples_to_imported_nodes();
void test_gltf_scene_importer_preserves_matrix_nodes_and_animation_returns_to_trs();
void test_gltf_scene_importer_classifies_deformable_primitives();
void test_gltf_scene_importer_validates_deformation_inputs_and_culling_policy();
void test_pointer_drag_tracks_active_cursor_and_accumulated_delta();
void test_project_context_exposes_async_runtime_services();
void test_project_context_exposes_optional_gpu_services();
void test_project_gpu_services_enqueue_uploads_and_retire_completed_gpu_work();
void test_project_gpu_services_mark_failed_uploads();
void test_project_gpu_services_rejects_invalid_rgba8_readback_requests();
void test_project_gpu_services_submit_and_wait_runs_on_owner_thread();
void test_project_gpu_services_tracks_failed_rgba8_readbacks();
void test_project_gpu_services_wait_queue_idle_runs_on_owner_thread();
void test_project_runtime_adapter_attaches_gpu_services_to_context();
void test_project_runtime_adapter_exposes_context_and_retirement();
void test_project_runtime_adapter_reuses_frame_for_same_timing();
void test_project_runtime_contract_supports_lifecycle_shape();
void test_project_runtime_services_create_project_frames_and_context();
void test_upload_queue_drains_in_submission_order();
void test_upload_queue_owns_payload_until_drain();
void test_upload_queue_tracks_failed_uploads();
void test_windowed_app_config_preserves_windowed_host_defaults();
void test_windowed_host_config_defaults_to_two_frame_slots();

namespace cubey::tests {

std::span<const TestCase> engine_host_input_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_capture_queue_encodes_png_with_inline_executor),
        CUBEY_TEST(test_capture_queue_propagates_encoding_errors),
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
        CUBEY_TEST(test_forward_pbr_renderer_3d_frame_plan_selects_required_passes),
        CUBEY_TEST(test_forward_pbr_renderer_3d_settings_defaults_to_aces_display_transform),
        CUBEY_TEST(test_forward_pbr_renderer_3d_selects_requested_light_or_fallback),
        CUBEY_TEST(test_forward_pbr_renderer_3d_shadow_vertex_layout_matches_pbr_vertices),
        CUBEY_TEST(
            test_forward_pbr_renderer_3d_scene_uniforms_pack_view_light_environment_and_display),
        CUBEY_TEST(
            test_forward_pbr_renderer_3d_skybox_uniforms_pack_inverse_view_camera_environment_and_display),
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
        CUBEY_TEST(test_input_state_accumulates_cursor_and_scroll_per_frame),
        CUBEY_TEST(test_input_state_ignores_unknown_inputs),
        CUBEY_TEST(test_filtered_input_frame_masks_captured_ui_channels),
        CUBEY_TEST(test_pointer_drag_tracks_active_cursor_and_accumulated_delta),
        CUBEY_TEST(test_pan_zoom_2d_controller_pans_and_zooms_from_input),
        CUBEY_TEST(test_orbit_controller_tracks_rotation_drag_pause_and_reset),
        CUBEY_TEST(test_orbit_controller_updates_from_input_snapshot),
        CUBEY_TEST(test_orbit_controller_scroll_zoom_clamps_distance),
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
        CUBEY_TEST(test_upload_queue_owns_payload_until_drain),
        CUBEY_TEST(test_upload_queue_drains_in_submission_order),
        CUBEY_TEST(test_upload_queue_tracks_failed_uploads),
        CUBEY_TEST(test_windowed_host_config_defaults_to_two_frame_slots),
        CUBEY_TEST(test_windowed_app_config_preserves_windowed_host_defaults),
    };
    return tests;
}

} // namespace cubey::tests
