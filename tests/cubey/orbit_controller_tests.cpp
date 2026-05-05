#include <cubey/orbit_controller.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_app_config_parses_png_output_path();
void test_capture_queue_encodes_png_with_inline_executor();
void test_capture_queue_propagates_encoding_errors();
void test_command_pool_exposes_command_buffer_ownership();
void test_compute_helpers_describe_pipeline_and_layout_setup();
void test_descriptor_helpers_describe_layout_pool_and_writes();
void test_frame_clock_tracks_delta_elapsed_and_index();
void test_frame_stats_publish_window_title_metrics();
void test_frame_ticket_issuer_returns_monotonic_tickets();
void test_image_output_writes_rgba_png();
void test_inline_executor_runs_jobs_immediately();
void test_job_system_runs_jobs_and_propagates_errors();
void test_job_system_shutdown_rejects_new_jobs();
void test_pipeline_helpers_describe_dynamic_graphics_pipeline_setup();
void test_project_context_exposes_async_runtime_services();
void test_project_runtime_contract_supports_lifecycle_shape();
void test_rendering_helpers_describe_dynamic_rendering_setup();
void test_render_context_exposes_explicit_frame_boundary();
void test_resource_helpers_describe_device_local_upload_and_depth_setup();
void test_spirv_file_reads_aligned_words();
void test_spirv_file_rejects_misaligned_byte_count();
void test_transfer_helpers_describe_texture_and_readback_paths();
void test_upload_queue_drains_in_submission_order();
void test_upload_queue_owns_payload_until_drain();
void test_deferred_destruction_queue_retires_completed_tickets();

int main() {
    try {
        test_app_config_parses_png_output_path();
        test_capture_queue_encodes_png_with_inline_executor();
        test_capture_queue_propagates_encoding_errors();
        test_command_pool_exposes_command_buffer_ownership();
        test_compute_helpers_describe_pipeline_and_layout_setup();
        test_descriptor_helpers_describe_layout_pool_and_writes();
        test_frame_clock_tracks_delta_elapsed_and_index();
        test_frame_stats_publish_window_title_metrics();
        test_frame_ticket_issuer_returns_monotonic_tickets();
        test_image_output_writes_rgba_png();
        test_inline_executor_runs_jobs_immediately();
        test_job_system_runs_jobs_and_propagates_errors();
        test_job_system_shutdown_rejects_new_jobs();
        test_pipeline_helpers_describe_dynamic_graphics_pipeline_setup();
        test_project_context_exposes_async_runtime_services();
        test_project_runtime_contract_supports_lifecycle_shape();
        test_rendering_helpers_describe_dynamic_rendering_setup();
        test_render_context_exposes_explicit_frame_boundary();
        test_resource_helpers_describe_device_local_upload_and_depth_setup();
        test_spirv_file_reads_aligned_words();
        test_spirv_file_rejects_misaligned_byte_count();
        test_transfer_helpers_describe_texture_and_readback_paths();
        test_upload_queue_owns_payload_until_drain();
        test_upload_queue_drains_in_submission_order();
        test_deferred_destruction_queue_retires_completed_tickets();

        cubey::OrbitController controller;

        require_close(controller.yaw(), 0.0F, "initial yaw should be zero");
        require_close(controller.pitch(), 0.0F, "initial pitch should be zero");
        require(!controller.paused(), "controller should start unpaused");

        controller.set_auto_rotation_speed(0.5F);
        controller.update(2.0);
        require_close(controller.yaw(), 1.0F, "auto rotation should advance yaw");

        controller.toggle_pause();
        controller.update(10.0);
        require_close(controller.yaw(), 1.0F, "paused auto rotation should not advance yaw");

        controller.begin_drag(10.0, 10.0);
        controller.drag_to(30.0, 5.0);
        controller.end_drag();
        require_close(controller.yaw(), 1.2F, "horizontal drag should adjust yaw");
        require_close(controller.pitch(), -0.05F, "vertical drag should adjust pitch");

        controller.reset();
        require_close(controller.yaw(), 0.0F, "reset should clear yaw");
        require_close(controller.pitch(), 0.0F, "reset should clear pitch");
        require(!controller.paused(), "reset should resume animation");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "cubey_core_tests: %s\n", error.what());
        return 1;
    }
}
