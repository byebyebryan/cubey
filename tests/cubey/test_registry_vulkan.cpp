#include "test_registry_common.h"

void test_command_pool_exposes_command_buffer_ownership();
void test_command_recorder_exposes_non_owning_command_buffer_contract();
void test_command_recorder_rejects_invalid_recording_inputs_before_vulkan_calls();
void test_compute_helpers_describe_pipeline_and_layout_setup();
void test_deferred_gpu_destruction_queue_retires_completed_tickets();
void test_descriptor_helpers_describe_layout_pool_and_writes();
void test_descriptor_set_allocate_info_describes_multiple_sets();
void test_descriptor_set_info_copies_bindings_and_aggregates_pool_sizes();
void test_descriptor_write_batch_owns_write_storage_and_preserves_order();
void test_dynamic_rendering_describes_attachment_setup();
void test_frame_resources_expose_slot_based_contract();
void test_gpu_runtime_accepts_cross_thread_enqueue_but_rejects_cross_thread_drain();
void test_gpu_runtime_defaults_to_threaded_execution();
void test_gpu_runtime_drains_inline_on_owner_thread();
void test_gpu_runtime_mark_submission_completed_updates_completed_ticket();
void test_gpu_runtime_preserves_pending_work_after_callback_failure();
void test_gpu_runtime_shutdown_rejects_new_work();
void test_gpu_runtime_submit_and_wait_propagates_threaded_failures();
void test_gpu_runtime_threaded_submit_and_wait_handles_owner_thread_calls();
void test_gpu_runtime_wait_queue_idle_runs_on_owner_thread();
void test_gpu_submission_ticket_issuer_returns_monotonic_tickets();
void test_gpu_work_queue_drains_fifo_and_owns_requests();
void test_image_transitions_describe_layout_barriers();
void test_immediate_commands_accepts_submission_coordinator();
void test_pipeline_helpers_describe_depth_only_dynamic_graphics_pipeline_setup();
void test_pipeline_helpers_describe_dynamic_graphics_pipeline_setup();
void test_queue_submit_info_describes_waits_commands_signals_and_fence();
void test_queue_submit_rejects_empty_command_buffer_list();
void test_render_context_exposes_explicit_frame_boundary();
void test_resource_helpers_describe_device_local_upload_and_depth_setup();
void test_sampler_config_describes_shadow_sampling();
void test_shader_bytecode_reads_aligned_spirv_words();
void test_shader_bytecode_rejects_misaligned_spirv_byte_count();
void test_submission_coordinator_completion_tracking_rejects_future_tickets();
void test_submission_coordinator_failed_submit_does_not_issue_ticket();
void test_submission_coordinator_issues_monotonic_gpu_tickets();
void test_submission_coordinator_submit_and_wait_marks_completion();
void test_swapchain_surface_format_falls_back_to_unorm_when_srgb_is_missing();
void test_swapchain_surface_format_prefers_srgb_color_attachment();
void test_swapchain_surface_format_uses_srgb_default_for_undefined_surface();
void test_transfer_helpers_describe_texture_and_readback_paths();

namespace cubey::tests {

std::span<const TestCase> vulkan_test_cases() {
    static constexpr std::array tests{
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
        CUBEY_TEST(test_frame_resources_expose_slot_based_contract),
        CUBEY_TEST(test_gpu_submission_ticket_issuer_returns_monotonic_tickets),
        CUBEY_TEST(test_gpu_work_queue_drains_fifo_and_owns_requests),
        CUBEY_TEST(test_gpu_runtime_defaults_to_threaded_execution),
        CUBEY_TEST(test_gpu_runtime_submit_and_wait_propagates_threaded_failures),
        CUBEY_TEST(test_gpu_runtime_threaded_submit_and_wait_handles_owner_thread_calls),
        CUBEY_TEST(test_gpu_runtime_wait_queue_idle_runs_on_owner_thread),
        CUBEY_TEST(test_gpu_runtime_mark_submission_completed_updates_completed_ticket),
        CUBEY_TEST(test_gpu_runtime_shutdown_rejects_new_work),
        CUBEY_TEST(test_gpu_runtime_drains_inline_on_owner_thread),
        CUBEY_TEST(test_gpu_runtime_accepts_cross_thread_enqueue_but_rejects_cross_thread_drain),
        CUBEY_TEST(test_gpu_runtime_preserves_pending_work_after_callback_failure),
        CUBEY_TEST(test_image_transitions_describe_layout_barriers),
        CUBEY_TEST(test_immediate_commands_accepts_submission_coordinator),
        CUBEY_TEST(test_pipeline_helpers_describe_dynamic_graphics_pipeline_setup),
        CUBEY_TEST(test_pipeline_helpers_describe_depth_only_dynamic_graphics_pipeline_setup),
        CUBEY_TEST(test_queue_submit_info_describes_waits_commands_signals_and_fence),
        CUBEY_TEST(test_queue_submit_rejects_empty_command_buffer_list),
        CUBEY_TEST(test_render_context_exposes_explicit_frame_boundary),
        CUBEY_TEST(test_resource_helpers_describe_device_local_upload_and_depth_setup),
        CUBEY_TEST(test_sampler_config_describes_shadow_sampling),
        CUBEY_TEST(test_shader_bytecode_reads_aligned_spirv_words),
        CUBEY_TEST(test_shader_bytecode_rejects_misaligned_spirv_byte_count),
        CUBEY_TEST(test_submission_coordinator_issues_monotonic_gpu_tickets),
        CUBEY_TEST(test_submission_coordinator_submit_and_wait_marks_completion),
        CUBEY_TEST(test_submission_coordinator_completion_tracking_rejects_future_tickets),
        CUBEY_TEST(test_submission_coordinator_failed_submit_does_not_issue_ticket),
        CUBEY_TEST(test_swapchain_surface_format_prefers_srgb_color_attachment),
        CUBEY_TEST(test_swapchain_surface_format_falls_back_to_unorm_when_srgb_is_missing),
        CUBEY_TEST(test_swapchain_surface_format_uses_srgb_default_for_undefined_surface),
        CUBEY_TEST(test_transfer_helpers_describe_texture_and_readback_paths),
    };
    return tests;
}

} // namespace cubey::tests
