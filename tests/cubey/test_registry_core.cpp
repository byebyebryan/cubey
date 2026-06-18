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
void test_process_resource_stats_sampler_reports_memory();
void test_process_resource_stats_sampler_reports_cpu_after_second_sample();
void test_math_helpers_match_vulkan_projection_conventions();
void test_math_quaternion_helpers_match_rotation_matrices();
void test_profile_recorder_skips_warmup_and_records_spans();
void test_profile_recorder_writes_csv_summary_and_trace_outputs();
void test_procedural_3d_noise_is_deterministic_and_stable();
void test_procedural_box_blur_preserves_dimensions_and_smooths_impulse();
void test_procedural_coherent_noise_wraps_fastnoise_lite();
void test_procedural_field_composition_combines_matching_fields();
void test_procedural_field_composition_rejects_invalid_inputs();
void test_procedural_field_composition_transforms_values();
void test_procedural_legacy_noise_golden_values_are_stable();
void test_procedural_local_relief_tracks_neighborhood_windows();
void test_procedural_noise_is_deterministic_and_bounded();
void test_procedural_operators_include_smootherstep();
void test_procedural_scalar_field_indexes_centered_samples();
void test_procedural_shader_random_helpers_are_shared();
void test_procedural_slope_curvature_handles_flat_ramp_and_peak();
void test_procedural_source_fields_fill_scalar_fields();
void test_procedural_source_fields_wrap_coherent_noise_backend();
void test_procedural_source_fields_wrap_legacy_noise_backends();
void test_procedural_scalar_field_summarizes_and_normalizes();
void test_run_cli_app_sets_default_title_and_returns_runner_status();
void test_active_project_ui_uses_shared_common_controls();
void test_active_project_ui_raw_combo_exceptions_are_explicit();
void test_retired_ocean_ui_exceptions_are_removed();
void test_imgui_helper_layer_covers_active_common_controls();
void test_active_project_ui_uses_shared_performance_panel();
void test_active_project_ui_starts_low_noise_sections_collapsed();
void test_run_config_parses_animation_options();
void test_run_config_parses_video_capture_defaults();
void test_run_config_parses_pbr_debug_view_name();
void test_run_config_parses_atmosphere_options();
void test_run_config_rejects_invalid_atmosphere_options();
void test_run_config_parses_frame_stats_flag();
void test_run_config_parses_profile_options();
void test_run_config_rejects_invalid_profile_diagnostics_options();
void test_run_config_parses_grid_dimensions();
void test_run_config_parses_water_controls();
void test_run_config_parses_ocean_controls();
void test_run_config_parses_terrain_controls();
void test_run_config_rejects_invalid_ocean_controls();
void test_run_config_parses_planet_controls();
void test_run_config_rejects_invalid_planet_controls();
void test_run_config_parses_shadow_volume_controls();
void test_run_config_parses_smoke_injector_count();
void test_run_config_parses_smoke_injector_orbit_controls();
void test_run_config_parses_smoke_injector_force_controls();
void test_run_config_parses_smoke_solver_controls();
void test_run_config_parses_pyro_buoyancy_control();
void test_run_config_parses_pyro_source_controls();
void test_run_config_parses_pyro_fire_controls();
void test_run_config_parses_pyro_obstacle_controls();
void test_run_config_parses_input_path();
void test_run_config_parses_pbr_environment_options();
void test_run_config_rejects_invalid_pbr_options();
void test_run_config_parses_png_output_path();
void test_run_config_preserves_explicit_video_capture_timing_and_output();
void test_run_config_rejects_invalid_capture_options();
void test_run_config_descriptors_have_help_text();
void test_run_config_descriptor_cli_names_are_unique();
void test_run_config_promoted_flags_are_not_explicit_parser_branches();
void test_run_config_descriptors_cover_project_control_paths();
void test_run_config_toggle_descriptors_have_negative_aliases();
void test_run_config_loads_json_config_file();
void test_run_config_cli_and_set_override_config_file();
void test_run_config_descriptor_cli_and_set_precedence();
void test_run_config_rejects_invalid_json_config_file();
void test_run_config_writes_json_template();
void test_video_encoder_validates_config_and_frame_size();
void test_video_encoder_writes_mp4_when_backend_is_available();

namespace cubey::tests {

std::span<const TestCase> core_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_run_config_parses_png_output_path),
        CUBEY_TEST(test_active_project_ui_uses_shared_common_controls),
        CUBEY_TEST(test_active_project_ui_raw_combo_exceptions_are_explicit),
        CUBEY_TEST(test_retired_ocean_ui_exceptions_are_removed),
        CUBEY_TEST(test_imgui_helper_layer_covers_active_common_controls),
        CUBEY_TEST(test_active_project_ui_uses_shared_performance_panel),
        CUBEY_TEST(test_active_project_ui_starts_low_noise_sections_collapsed),
        CUBEY_TEST(test_run_config_parses_video_capture_defaults),
        CUBEY_TEST(test_run_config_preserves_explicit_video_capture_timing_and_output),
        CUBEY_TEST(test_run_config_rejects_invalid_capture_options),
        CUBEY_TEST(test_run_config_descriptors_have_help_text),
        CUBEY_TEST(test_run_config_descriptor_cli_names_are_unique),
        CUBEY_TEST(test_run_config_promoted_flags_are_not_explicit_parser_branches),
        CUBEY_TEST(test_run_config_descriptors_cover_project_control_paths),
        CUBEY_TEST(test_run_config_toggle_descriptors_have_negative_aliases),
        CUBEY_TEST(test_run_config_loads_json_config_file),
        CUBEY_TEST(test_run_config_cli_and_set_override_config_file),
        CUBEY_TEST(test_run_config_descriptor_cli_and_set_precedence),
        CUBEY_TEST(test_run_config_rejects_invalid_json_config_file),
        CUBEY_TEST(test_run_config_writes_json_template),
        CUBEY_TEST(test_run_config_parses_pbr_environment_options),
        CUBEY_TEST(test_run_config_rejects_invalid_pbr_options),
        CUBEY_TEST(test_video_encoder_validates_config_and_frame_size),
        CUBEY_TEST(test_video_encoder_writes_mp4_when_backend_is_available),
        CUBEY_TEST(test_run_config_parses_input_path),
        CUBEY_TEST(test_run_config_parses_animation_options),
        CUBEY_TEST(test_run_config_parses_pbr_debug_view_name),
        CUBEY_TEST(test_run_config_parses_atmosphere_options),
        CUBEY_TEST(test_run_config_rejects_invalid_atmosphere_options),
        CUBEY_TEST(test_run_config_parses_frame_stats_flag),
        CUBEY_TEST(test_run_config_parses_profile_options),
        CUBEY_TEST(test_run_config_rejects_invalid_profile_diagnostics_options),
        CUBEY_TEST(test_run_config_parses_grid_dimensions),
        CUBEY_TEST(test_run_config_parses_water_controls),
        CUBEY_TEST(test_run_config_parses_ocean_controls),
        CUBEY_TEST(test_run_config_parses_terrain_controls),
        CUBEY_TEST(test_run_config_rejects_invalid_ocean_controls),
        CUBEY_TEST(test_run_config_parses_planet_controls),
        CUBEY_TEST(test_run_config_rejects_invalid_planet_controls),
        CUBEY_TEST(test_run_config_parses_shadow_volume_controls),
        CUBEY_TEST(test_run_config_parses_smoke_injector_count),
        CUBEY_TEST(test_run_config_parses_smoke_injector_orbit_controls),
        CUBEY_TEST(test_run_config_parses_smoke_injector_force_controls),
        CUBEY_TEST(test_run_config_parses_smoke_solver_controls),
        CUBEY_TEST(test_run_config_parses_pyro_buoyancy_control),
        CUBEY_TEST(test_run_config_parses_pyro_source_controls),
        CUBEY_TEST(test_run_config_parses_pyro_fire_controls),
        CUBEY_TEST(test_run_config_parses_pyro_obstacle_controls),
        CUBEY_TEST(test_run_cli_app_sets_default_title_and_returns_runner_status),
        CUBEY_TEST(test_file_io_round_trips_binary_bytes),
        CUBEY_TEST(test_frame_clock_tracks_delta_elapsed_and_index),
        CUBEY_TEST(test_process_resource_stats_sampler_reports_memory),
        CUBEY_TEST(test_process_resource_stats_sampler_reports_cpu_after_second_sample),
        CUBEY_TEST(test_procedural_scalar_field_indexes_centered_samples),
        CUBEY_TEST(test_procedural_scalar_field_summarizes_and_normalizes),
        CUBEY_TEST(test_procedural_box_blur_preserves_dimensions_and_smooths_impulse),
        CUBEY_TEST(test_procedural_field_composition_transforms_values),
        CUBEY_TEST(test_procedural_field_composition_combines_matching_fields),
        CUBEY_TEST(test_procedural_field_composition_rejects_invalid_inputs),
        CUBEY_TEST(test_procedural_slope_curvature_handles_flat_ramp_and_peak),
        CUBEY_TEST(test_procedural_local_relief_tracks_neighborhood_windows),
        CUBEY_TEST(test_procedural_operators_include_smootherstep),
        CUBEY_TEST(test_procedural_shader_random_helpers_are_shared),
        CUBEY_TEST(test_procedural_noise_is_deterministic_and_bounded),
        CUBEY_TEST(test_procedural_legacy_noise_golden_values_are_stable),
        CUBEY_TEST(test_procedural_3d_noise_is_deterministic_and_stable),
        CUBEY_TEST(test_procedural_coherent_noise_wraps_fastnoise_lite),
        CUBEY_TEST(test_procedural_source_fields_wrap_legacy_noise_backends),
        CUBEY_TEST(test_procedural_source_fields_wrap_coherent_noise_backend),
        CUBEY_TEST(test_procedural_source_fields_fill_scalar_fields),
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
        CUBEY_TEST(test_profile_recorder_skips_warmup_and_records_spans),
        CUBEY_TEST(test_profile_recorder_writes_csv_summary_and_trace_outputs),
    };
    return tests;
}

} // namespace cubey::tests
