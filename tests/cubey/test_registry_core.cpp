#include "test_registry_common.h"

void test_file_io_round_trips_binary_bytes();
void test_frame_clock_tracks_delta_elapsed_and_index();
void test_gltf_animation_wraps_looping_playback_time();
void test_gltf_animation_samples_linear_translation();
void test_gltf_animation_samples_step_scale();
void test_gltf_animation_slerps_rotation_and_normalizes();
void test_gltf_animation_samples_cubic_translation();
void test_gltf_animation_samples_morph_weights();
void test_gltf_animation_computes_joint_palette_from_world_matrices();
void test_image_io_writes_rgba_png();
void test_inline_executor_runs_jobs_immediately();
void test_job_system_runs_jobs_and_propagates_errors();
void test_job_system_shutdown_rejects_new_jobs();
void test_math_helpers_match_vulkan_projection_conventions();
void test_math_quaternion_helpers_match_rotation_matrices();
void test_run_cli_app_sets_default_title_and_returns_runner_status();
void test_run_config_parses_input_path();
void test_run_config_parses_png_output_path();

namespace cubey::tests {

std::span<const TestCase> core_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_run_config_parses_png_output_path),
        CUBEY_TEST(test_run_config_parses_input_path),
        CUBEY_TEST(test_run_cli_app_sets_default_title_and_returns_runner_status),
        CUBEY_TEST(test_file_io_round_trips_binary_bytes),
        CUBEY_TEST(test_frame_clock_tracks_delta_elapsed_and_index),
        CUBEY_TEST(test_gltf_animation_wraps_looping_playback_time),
        CUBEY_TEST(test_gltf_animation_samples_linear_translation),
        CUBEY_TEST(test_gltf_animation_samples_step_scale),
        CUBEY_TEST(test_gltf_animation_slerps_rotation_and_normalizes),
        CUBEY_TEST(test_gltf_animation_samples_cubic_translation),
        CUBEY_TEST(test_gltf_animation_samples_morph_weights),
        CUBEY_TEST(test_gltf_animation_computes_joint_palette_from_world_matrices),
        CUBEY_TEST(test_image_io_writes_rgba_png),
        CUBEY_TEST(test_inline_executor_runs_jobs_immediately),
        CUBEY_TEST(test_job_system_runs_jobs_and_propagates_errors),
        CUBEY_TEST(test_job_system_shutdown_rejects_new_jobs),
        CUBEY_TEST(test_math_helpers_match_vulkan_projection_conventions),
        CUBEY_TEST(test_math_quaternion_helpers_match_rotation_matrices),
    };
    return tests;
}

} // namespace cubey::tests
