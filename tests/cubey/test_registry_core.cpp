#include "test_registry_common.h"

void test_file_io_round_trips_binary_bytes();
void test_config_schema_composes_typed_bindings_and_emits_template();
void test_config_schema_binds_typed_enum_and_emits_symbolic_value();
void test_config_schema_rejects_duplicate_and_invalid_metadata();
void test_config_schema_rejects_wrong_type_unknown_and_range();
void test_config_schema_bool_aliases_null_and_precedence();
void test_config_schema_layers_config_files_in_argv_order();
void test_config_schema_named_values_can_spell_bootstrap_flags();
void test_common_host_config_normalizes_capture_compatibility();
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
void test_procedural_artifact_cache_failures_remain_nonfatal();
void test_procedural_artifact_cache_hashes_complete_recipes();
void test_procedural_artifact_cache_prunes_oldest_entries();
void test_procedural_artifact_cache_rejects_corrupt_entries();
void test_procedural_artifact_cache_round_trips_and_invalidates_entries();
void test_procedural_artifact_cache_round_trips_structured_payloads();
void test_procedural_artifact_metadata_builders_fill_identity_and_validate();
void test_procedural_artifact_metadata_counts_mipped_samples();
void test_procedural_artifact_metadata_validates_identity_and_layout();
void test_procedural_box_blur_preserves_dimensions_and_smooths_impulse();
void test_procedural_coherent_noise_wraps_fastnoise_lite();
void test_procedural_field_composition_combines_matching_fields();
void test_procedural_field_composition_rejects_invalid_inputs();
void test_procedural_field_composition_transforms_values();
void test_procedural_field_metadata_hashes_field_sets_by_name();
void test_procedural_field_metadata_hashes_scalar_fields();
void test_procedural_field_sets_reject_invalid_fields();
void test_procedural_field_sets_store_named_scalar_fields();
void test_procedural_field_shaping_converts_and_terraces_unit_values();
void test_procedural_hash_builder_encodes_stable_values();
void test_procedural_distribution_summarizes_percentiles();
void test_procedural_percentile_remap_shapes_distribution();
void test_procedural_legacy_noise_golden_values_are_stable();
void test_procedural_local_relief_tracks_neighborhood_windows();
void test_procedural_noise_is_deterministic_and_bounded();
void test_procedural_operators_include_smootherstep();
void test_procedural_patch_domains_expand_bordered_sample_grids();
void test_procedural_patch_domains_hash_addresses_and_seeds();
void test_procedural_sample_domains_index_3d_samples();
void test_procedural_sample_domains_reject_invalid_3d_samples();
void test_procedural_sample_domains_wrap_2d_grids();
void test_procedural_scalar_field_indexes_centered_samples();
void test_procedural_seed_domain_random_is_stable_and_bounded();
void test_procedural_seed_domains_are_stable();
void test_procedural_shader_3d_noise_helpers_match_glsl_contracts();
void test_procedural_shader_fastnoise_lite_include_is_shared();
void test_procedural_shader_hash_helpers_match_glsl_contracts();
void test_procedural_shader_random_helpers_are_shared();
void test_procedural_shader_scalar_helpers_match_glsl_contracts();
void test_shared_shader_debug_and_view_helpers_compile();
void test_procedural_slope_curvature_handles_flat_ramp_and_peak();
void test_procedural_source_fields_fill_scalar_fields();
void test_procedural_source_fields_apply_optional_domain_warp();
void test_procedural_source_fields_wrap_coherent_noise_backend();
void test_procedural_source_fields_wrap_legacy_noise_backends();
void test_procedural_source_recipes_apply_blend_modes();
void test_procedural_source_recipes_compose_layers_and_debug_fields();
void test_procedural_source_recipes_normalize_outputs();
void test_procedural_source_recipes_reject_invalid_layers();
void test_procedural_scalar_field_summarizes_and_normalizes();
void test_active_project_ui_uses_shared_common_controls();
void test_active_project_ui_raw_combo_exceptions_are_explicit();
void test_retired_ocean_ui_exceptions_are_removed();
void test_imgui_helper_layer_covers_active_common_controls();
void test_active_project_ui_uses_shared_performance_panel();
void test_active_project_ui_starts_low_noise_sections_collapsed();
void test_shared_cloud_ui_defaults_to_surface_controls();
void test_video_encoder_validates_config_and_frame_size();
void test_video_encoder_writes_mp4_when_backend_is_available();

namespace cubey::tests {

std::span<const TestCase> core_test_cases() {
    static constexpr std::array tests{
        CUBEY_TEST(test_config_schema_composes_typed_bindings_and_emits_template),
        CUBEY_TEST(test_config_schema_binds_typed_enum_and_emits_symbolic_value),
        CUBEY_TEST(test_config_schema_rejects_duplicate_and_invalid_metadata),
        CUBEY_TEST(test_config_schema_rejects_wrong_type_unknown_and_range),
        CUBEY_TEST(test_config_schema_bool_aliases_null_and_precedence),
        CUBEY_TEST(test_config_schema_layers_config_files_in_argv_order),
        CUBEY_TEST(test_config_schema_named_values_can_spell_bootstrap_flags),
        CUBEY_TEST(test_common_host_config_normalizes_capture_compatibility),
        CUBEY_TEST(test_active_project_ui_uses_shared_common_controls),
        CUBEY_TEST(test_active_project_ui_raw_combo_exceptions_are_explicit),
        CUBEY_TEST(test_retired_ocean_ui_exceptions_are_removed),
        CUBEY_TEST(test_imgui_helper_layer_covers_active_common_controls),
        CUBEY_TEST(test_active_project_ui_uses_shared_performance_panel),
        CUBEY_TEST(test_active_project_ui_starts_low_noise_sections_collapsed),
        CUBEY_TEST(test_shared_cloud_ui_defaults_to_surface_controls),
        CUBEY_TEST(test_video_encoder_validates_config_and_frame_size),
        CUBEY_TEST(test_video_encoder_writes_mp4_when_backend_is_available),
        CUBEY_TEST(test_file_io_round_trips_binary_bytes),
        CUBEY_TEST(test_frame_clock_tracks_delta_elapsed_and_index),
        CUBEY_TEST(test_process_resource_stats_sampler_reports_memory),
        CUBEY_TEST(test_process_resource_stats_sampler_reports_cpu_after_second_sample),
        CUBEY_TEST(test_procedural_artifact_cache_failures_remain_nonfatal),
        CUBEY_TEST(test_procedural_artifact_cache_hashes_complete_recipes),
        CUBEY_TEST(test_procedural_artifact_cache_prunes_oldest_entries),
        CUBEY_TEST(test_procedural_artifact_cache_rejects_corrupt_entries),
        CUBEY_TEST(test_procedural_artifact_cache_round_trips_and_invalidates_entries),
        CUBEY_TEST(test_procedural_artifact_cache_round_trips_structured_payloads),
        CUBEY_TEST(test_procedural_scalar_field_indexes_centered_samples),
        CUBEY_TEST(test_procedural_sample_domains_wrap_2d_grids),
        CUBEY_TEST(test_procedural_sample_domains_index_3d_samples),
        CUBEY_TEST(test_procedural_sample_domains_reject_invalid_3d_samples),
        CUBEY_TEST(test_procedural_hash_builder_encodes_stable_values),
        CUBEY_TEST(test_procedural_patch_domains_hash_addresses_and_seeds),
        CUBEY_TEST(test_procedural_patch_domains_expand_bordered_sample_grids),
        CUBEY_TEST(test_procedural_artifact_metadata_counts_mipped_samples),
        CUBEY_TEST(test_procedural_artifact_metadata_validates_identity_and_layout),
        CUBEY_TEST(test_procedural_artifact_metadata_builders_fill_identity_and_validate),
        CUBEY_TEST(test_procedural_field_metadata_hashes_scalar_fields),
        CUBEY_TEST(test_procedural_field_metadata_hashes_field_sets_by_name),
        CUBEY_TEST(test_procedural_scalar_field_summarizes_and_normalizes),
        CUBEY_TEST(test_procedural_field_sets_store_named_scalar_fields),
        CUBEY_TEST(test_procedural_field_sets_reject_invalid_fields),
        CUBEY_TEST(test_procedural_box_blur_preserves_dimensions_and_smooths_impulse),
        CUBEY_TEST(test_procedural_field_composition_transforms_values),
        CUBEY_TEST(test_procedural_distribution_summarizes_percentiles),
        CUBEY_TEST(test_procedural_percentile_remap_shapes_distribution),
        CUBEY_TEST(test_procedural_field_shaping_converts_and_terraces_unit_values),
        CUBEY_TEST(test_procedural_field_composition_combines_matching_fields),
        CUBEY_TEST(test_procedural_field_composition_rejects_invalid_inputs),
        CUBEY_TEST(test_procedural_slope_curvature_handles_flat_ramp_and_peak),
        CUBEY_TEST(test_procedural_local_relief_tracks_neighborhood_windows),
        CUBEY_TEST(test_procedural_operators_include_smootherstep),
        CUBEY_TEST(test_procedural_seed_domains_are_stable),
        CUBEY_TEST(test_procedural_seed_domain_random_is_stable_and_bounded),
        CUBEY_TEST(test_procedural_shader_random_helpers_are_shared),
        CUBEY_TEST(test_procedural_shader_fastnoise_lite_include_is_shared),
        CUBEY_TEST(test_procedural_shader_scalar_helpers_match_glsl_contracts),
        CUBEY_TEST(test_procedural_shader_hash_helpers_match_glsl_contracts),
        CUBEY_TEST(test_procedural_shader_3d_noise_helpers_match_glsl_contracts),
        CUBEY_TEST(test_shared_shader_debug_and_view_helpers_compile),
        CUBEY_TEST(test_procedural_noise_is_deterministic_and_bounded),
        CUBEY_TEST(test_procedural_legacy_noise_golden_values_are_stable),
        CUBEY_TEST(test_procedural_3d_noise_is_deterministic_and_stable),
        CUBEY_TEST(test_procedural_coherent_noise_wraps_fastnoise_lite),
        CUBEY_TEST(test_procedural_source_fields_wrap_legacy_noise_backends),
        CUBEY_TEST(test_procedural_source_fields_wrap_coherent_noise_backend),
        CUBEY_TEST(test_procedural_source_fields_apply_optional_domain_warp),
        CUBEY_TEST(test_procedural_source_fields_fill_scalar_fields),
        CUBEY_TEST(test_procedural_source_recipes_compose_layers_and_debug_fields),
        CUBEY_TEST(test_procedural_source_recipes_apply_blend_modes),
        CUBEY_TEST(test_procedural_source_recipes_normalize_outputs),
        CUBEY_TEST(test_procedural_source_recipes_reject_invalid_layers),
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
