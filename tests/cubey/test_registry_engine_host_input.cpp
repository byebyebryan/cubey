#include "test_registry_common.h"

void test_capture_queue_encodes_png_with_inline_executor();
void test_capture_queue_propagates_encoding_errors();
void test_engine_attaches_gpu_services_to_project_context();
void test_engine_created_scenes_validate_render_resource_handles();
void test_engine_creates_independent_scenes();
void test_engine_destroys_owned_scenes_and_rejects_foreign_scenes();
void test_engine_exposes_project_runtime_services();
void test_engine_exposes_render_resource_registry();
void test_engine_reuses_project_frame_for_same_timing();
void test_headless_png_host_validates_capture_shape();
void test_host_frame_stats_publish_window_title_metrics();
void test_input_state_accumulates_cursor_and_scroll_per_frame();
void test_input_state_ignores_unknown_inputs();
void test_input_state_tracks_key_and_button_edges();
void test_orbit_controller_tracks_rotation_drag_pause_and_reset();
void test_orbit_controller_updates_from_input_snapshot();
void test_pan_zoom_2d_controller_pans_and_zooms_from_input();
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
        CUBEY_TEST(test_headless_png_host_validates_capture_shape),
        CUBEY_TEST(test_host_frame_stats_publish_window_title_metrics),
        CUBEY_TEST(test_input_state_tracks_key_and_button_edges),
        CUBEY_TEST(test_input_state_accumulates_cursor_and_scroll_per_frame),
        CUBEY_TEST(test_input_state_ignores_unknown_inputs),
        CUBEY_TEST(test_pointer_drag_tracks_active_cursor_and_accumulated_delta),
        CUBEY_TEST(test_pan_zoom_2d_controller_pans_and_zooms_from_input),
        CUBEY_TEST(test_orbit_controller_tracks_rotation_drag_pause_and_reset),
        CUBEY_TEST(test_orbit_controller_updates_from_input_snapshot),
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
