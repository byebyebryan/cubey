cubey_atmosphere_shader_depends(CUBEY_SDF_ATMOSPHERE_DEPENDS)
cubey_cloud_layer_shader_sources(CUBEY_SDF_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_SDF_CLOUD_DEPENDS)
cubey_add_app_target(
    cubey_project_sdf
    OUTPUT_NAME sdf
    SHADER_DEFINE CUBEY_SDF_SHADER_DIR
    WINDOWED_SMOKE_TEST sdf_reaches_glfw
    WINDOWED_SMOKE_PATTERN "sdf: .*rendering sparse SDF bricks"
    SOURCES main.cpp sdf_workshop.cpp sdf_app.cpp sdf_field.cpp sdf_recipe.cpp sdf_validation.cpp sdf_renderer.cpp sdf_environment.cpp sdf_timeline.cpp sdf_authored.cpp sdf_history.cpp sdf_material.cpp sdf_comparison.cpp sdf_regions.cpp sdf_bounds.cpp sdf_shader_selection.cpp sdf_prepared_cones.cpp sdf_render_modes.cpp
    SHADERS
        shaders/local_filter.comp shaders/local_samples.comp shaders/local_canonical.comp
        shaders/local_dom_filter.comp shaders/local_dom_samples.comp
        shaders/cached_stroke_sdf.frag shaders/quality_cached_stroke_sdf.frag
        shaders/cached_stroke_quality_workload.comp shaders/cached_stroke_queries.comp
        shaders/commit_reuse_bricks.comp shaders/commit_canonical_fallback.comp
        shaders/commit_prune_probe.comp
        shaders/commit_pruned_bricks.comp shaders/commit_pruned_filter.comp
        shaders/cached_edit_sdf.frag shaders/quality_cached_edit_sdf.frag
        shaders/cached_edit_quality_workload.comp shaders/cached_edit_queries.comp
        shaders/live_edit_sdf.frag shaders/lit_live_edit_sdf.frag shaders/shadow_lit_live_edit_sdf.frag
        shaders/quality_live_edit_sdf.frag shaders/quality_lit_live_edit_sdf.frag shaders/quality_shadow_lit_live_edit_sdf.frag
        shaders/live_edit_quality_workload.comp shaders/live_edit_queries.comp
        shaders/cached03125_material_study.frag
        shaders/cachedrefinenormal_material_study.frag
        shaders/cachedrefinenormal_sdf_authored.frag
        shaders/quality_cachedrefinenormal_sdf_authored.frag
        shaders/cachedrefinenormal_quality_workload_sampled.comp
        shaders/cachedrefinenormal_comparison_sampled.comp
        shaders/cached_cell_contract.comp
        shaders/cached05_sdf_authored.frag
        shaders/quality_cached05_sdf_authored.frag
        shaders/cached05_quality_workload_sampled.comp
        shaders/cached05_comparison_sampled.comp
        shaders/cached03125_sdf_authored.frag
        shaders/quality_cached03125_sdf_authored.frag
        shaders/cached03125_quality_workload_sampled.comp
        shaders/cached03125_comparison_sampled.comp
        shaders/cachednormal_sdf_authored.frag
        shaders/quality_cachednormal_sdf_authored.frag
        shaders/cachednormal_quality_workload_sampled.comp
        shaders/cachednormal_comparison_sampled.comp
        shaders/quality_sdf_authored.frag
        shaders/quality_root_far_sdf_analytic_pruned.frag
        shaders/quality_lit_root_far_sdf_analytic_pruned.frag
        shaders/quality_combined_lit_root_far_sdf_analytic_pruned.frag
        shaders/quality_adaptive50_combined_lit_root_far_sdf_analytic_pruned.frag
        shaders/quality_workload_reference.comp
        shaders/quality_workload_combined.comp
        shaders/adaptive50_quality_workload_combined.comp
        shaders/quality_workload_sampled.comp
        shaders/relaxed12_guarded_contract.comp shaders/relaxed15_guarded_contract.comp shaders/adaptive50_guarded_contract.comp
        shaders/adaptive50_combined_lit_root_far_sdf_analytic_pruned.frag
        shaders/adaptive50_prepared_root_far_comparison_analytic_pruned.comp
        shaders/adaptive50_query_validate_combined.frag
        shaders/adaptive50_query_validate_shadow.frag
        shaders/adaptive50_query_workload_combined.comp
        shaders/adaptive50_query_workload_shadow.comp
        shaders/adaptive50_root_far_comparison_analytic_pruned.comp
        shaders/adaptive50_shadow_lit_root_far_sdf_analytic_pruned.frag
        shaders/relaxed12_shadow_lit_root_far_sdf_analytic_pruned.frag
        shaders/relaxed12_combined_lit_root_far_sdf_analytic_pruned.frag
        shaders/relaxed12_query_validate_shadow.frag
        shaders/relaxed12_query_validate_combined.frag
        shaders/relaxed12_query_workload_shadow.comp
        shaders/relaxed12_query_workload_combined.comp
        shaders/relaxed12_root_far_comparison_analytic_pruned.comp
        shaders/relaxed12_prepared_root_far_comparison_analytic_pruned.comp
        shaders/relaxed15_shadow_lit_root_far_sdf_analytic_pruned.frag
        shaders/relaxed15_combined_lit_root_far_sdf_analytic_pruned.frag
        shaders/relaxed15_query_validate_shadow.frag
        shaders/relaxed15_query_validate_combined.frag
        shaders/relaxed15_query_workload_shadow.comp
        shaders/relaxed15_query_workload_combined.comp
        shaders/relaxed15_root_far_comparison_analytic_pruned.comp
        shaders/relaxed15_prepared_root_far_comparison_analytic_pruned.comp
        shaders/query_workload_shadow.comp shaders/query_workload_combined.comp shaders/query_workload_reference.comp
        shaders/query_camera.frag
        shaders/query_validate_shadow.frag shaders/query_validate_combined.frag shaders/query_validate_reference.frag
        shaders/shadow_contract_reference.comp shaders/shadow_contract_candidate.comp
        shaders/prepared_cone_probe.comp
        shaders/prepared_lit_root_far_sdf_analytic_pruned.frag
        shaders/combined_lit_root_far_sdf_analytic_pruned.frag
        shaders/prepared_root_far_comparison_analytic_pruned.comp
        shaders/shadow_lit_accel_sdf_analytic_pruned.frag
        shaders/shadow_lit_accel_sdf_hybrid_pruned.frag
        shaders/shadow_lit_far_sdf_analytic_pruned.frag
        shaders/shadow_lit_root_far_sdf_analytic_pruned.frag
        shaders/shadow_lit_root_sdf_analytic_pruned.frag
        shaders/shadow_lit_sdf_analytic.frag
        shaders/shadow_lit_sdf_analytic_pruned.frag
        shaders/shadow_lit_sdf_hybrid.frag
        shaders/shadow_lit_sdf_hybrid_pruned.frag
        shaders/shadow_lit_unclamped_sdf_hybrid_pruned.frag
        shaders/lit_sdf_analytic.frag
        shaders/lit_sdf_hybrid.frag
        shaders/lit_material_analytic.frag
        shaders/lit_material_hybrid.frag
        shaders/lit_sdf_analytic_pruned.frag
        shaders/lit_sdf_hybrid_pruned.frag
        shaders/lit_material_analytic_pruned.frag
        shaders/lit_material_hybrid_pruned.frag
        shaders/lit_accel_sdf_analytic_pruned.frag
        shaders/lit_accel_sdf_hybrid_pruned.frag
        shaders/lit_accel_material_analytic_pruned.frag
        shaders/lit_accel_material_hybrid_pruned.frag
        shaders/lit_unclamped_sdf_hybrid_pruned.frag
        shaders/lit_unclamped_material_hybrid_pruned.frag
        shaders/lit_root_sdf_analytic_pruned.frag
        shaders/lit_root_material_analytic_pruned.frag
        shaders/lit_far_sdf_analytic_pruned.frag
        shaders/lit_far_material_analytic_pruned.frag
        shaders/lit_root_far_sdf_analytic_pruned.frag
        shaders/lit_root_far_material_analytic_pruned.frag
        shaders/material_study.frag shaders/sdf.vert shaders/sdf.frag shaders/sdf_authored.frag shaders/filter.comp shaders/bricks.comp shaders/validate.comp
        ${CUBEY_SDF_CLOUD_SHADERS}
        shaders/authored_filter.comp shaders/authored_bricks.comp
        shaders/authored_validate.comp
        shaders/comparison_full.comp shaders/comparison_hybrid.comp shaders/comparison_sampled.comp
        shaders/comparison_donor.comp
        shaders/comparison_analytic_pruned.comp shaders/comparison_hybrid_pruned.comp
        shaders/sdf_analytic.frag shaders/sdf_hybrid.frag
        shaders/material_analytic.frag shaders/material_hybrid.frag
        shaders/sdf_analytic_pruned.frag shaders/sdf_hybrid_pruned.frag
        shaders/material_analytic_pruned.frag shaders/material_hybrid_pruned.frag
        shaders/investigate_sdf_authored.frag
        shaders/investigate_sdf_analytic_pruned.frag shaders/investigate_sdf_hybrid_pruned.frag
        shaders/workload_analytic.comp shaders/workload_hybrid.comp
        shaders/tight_investigate_sdf_analytic_pruned.frag shaders/tight_investigate_sdf_hybrid_pruned.frag
        shaders/tight_comparison_analytic_pruned.comp shaders/tight_comparison_hybrid_pruned.comp
        shaders/tight_workload_analytic.comp shaders/tight_workload_hybrid.comp
        shaders/accel_sdf_analytic_pruned.frag
        shaders/accel_sdf_hybrid_pruned.frag
        shaders/accel_material_analytic_pruned.frag
        shaders/accel_material_hybrid_pruned.frag
        shaders/accel_comparison_analytic_pruned.comp
        shaders/accel_comparison_hybrid_pruned.comp
        shaders/accel_workload_analytic.comp
        shaders/accel_workload_hybrid.comp
        shaders/unclamped_sdf_hybrid_pruned.frag
        shaders/unclamped_material_hybrid_pruned.frag
        shaders/unclamped_comparison_hybrid_pruned.comp
        shaders/unclamped_workload_hybrid.comp
        shaders/root_sdf_analytic_pruned.frag
        shaders/root_material_analytic_pruned.frag
        shaders/root_comparison_analytic_pruned.comp
        shaders/root_workload_analytic.comp
        shaders/far_sdf_analytic_pruned.frag
        shaders/far_material_analytic_pruned.frag
        shaders/far_comparison_analytic_pruned.comp
        shaders/far_workload_analytic.comp
        shaders/root_far_sdf_analytic_pruned.frag
        shaders/root_far_material_analytic_pruned.frag
        shaders/root_far_comparison_analytic_pruned.comp
        shaders/root_far_workload_analytic.comp
        shaders/root_far_investigate_sdf_analytic_pruned.frag
        shaders/profile_fixed_full.frag
        shaders/profile_fixed_primary.frag
        shaders/profile_fixed_normal.frag
        shaders/profile_fixed_material.frag
        shaders/profile_fixed_shadow.frag
        shaders/profile_nodebug.frag
        shaders/profile_single-material.frag
        shaders/profile_specialized.frag
        shaders/profile_opt-full.frag
        shaders/profile_opt-primary.frag
        shaders/profile_opt-normal.frag
        shaders/profile_opt-material.frag
        shaders/profile_opt-shadow.frag
        shaders/profile_opt-reorder-shadow.frag
        shaders/profile_opt-cull-dark-shadows.frag
        shaders/profile_opt-no-sky.frag
        shaders/profile_no-sky.frag
        shaders/occluder.vert shaders/occluder.frag
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere_reflection_prefilter.frag"
    SHADER_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    SHADER_DEPENDS "${PROJECT_SOURCE_DIR}/shaders/cubey/procedural/surface_material.glsl" "${PROJECT_SOURCE_DIR}/shaders/cubey/procedural/surface_library.glsl" shaders/field.glsl shaders/trace.glsl ${CUBEY_SDF_ATMOSPHERE_DEPENDS} ${CUBEY_SDF_CLOUD_DEPENDS}
)

if (BUILD_TESTING)
    add_executable(cubey_project_sdf_cached_stroke_study tools/cached_stroke_study.cpp tools/stroke_service.cpp tools/stroke_renderer.cpp sdf_cached_stroke.cpp sdf_cached_commit.cpp sdf_cached_local.cpp sdf_cached_edit.cpp sdf_environment.cpp sdf_field.cpp sdf_regions.cpp sdf_bounds.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp sdf_shader_selection.cpp sdf_prepared_cones.cpp sdf_render_modes.cpp)
    target_link_libraries(cubey_project_sdf_cached_stroke_study PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_project_sdf_cached_stroke_study PRIVATE CUBEY_SDF_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
    add_dependencies(cubey_project_sdf_cached_stroke_study cubey_project_sdf)
    add_executable(cubey_project_sdf_cached_commit_probe tools/cached_commit_probe.cpp sdf_regions.cpp sdf_bounds.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp)
    target_link_libraries(cubey_project_sdf_cached_commit_probe PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_project_sdf_cached_commit_probe PRIVATE CUBEY_SDF_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
    add_dependencies(cubey_project_sdf_cached_commit_probe cubey_project_sdf)
    add_executable(cubey_project_sdf_cached_commit_study tools/cached_commit_study.cpp sdf_cached_commit.cpp sdf_cached_local.cpp sdf_cached_stroke.cpp sdf_cached_edit.cpp sdf_field.cpp sdf_regions.cpp sdf_bounds.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp)
    target_link_libraries(cubey_project_sdf_cached_commit_study PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_project_sdf_cached_commit_study PRIVATE CUBEY_SDF_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
    add_dependencies(cubey_project_sdf_cached_commit_study cubey_project_sdf)
    add_executable(cubey_project_sdf_cached_edit_study tools/cached_edit_study.cpp sdf_cached_commit.cpp sdf_cached_local.cpp sdf_cached_stroke.cpp sdf_cached_edit.cpp sdf_renderer.cpp sdf_environment.cpp sdf_field.cpp sdf_regions.cpp sdf_bounds.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp sdf_shader_selection.cpp sdf_prepared_cones.cpp sdf_render_modes.cpp)
    target_link_libraries(cubey_project_sdf_cached_edit_study PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_project_sdf_cached_edit_study PRIVATE CUBEY_SDF_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
    add_dependencies(cubey_project_sdf_cached_edit_study cubey_project_sdf)
    add_executable(cubey_project_sdf_live_edit_study tools/live_edit_study.cpp sdf_live_edit.cpp sdf_renderer.cpp sdf_environment.cpp sdf_field.cpp sdf_regions.cpp sdf_bounds.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp sdf_shader_selection.cpp sdf_prepared_cones.cpp sdf_render_modes.cpp)
    target_link_libraries(cubey_project_sdf_live_edit_study PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_project_sdf_live_edit_study PRIVATE CUBEY_SDF_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
    add_dependencies(cubey_project_sdf_live_edit_study cubey_project_sdf)
    add_executable(cubey_project_sdf_cached_cell_contract tools/cached_cell_contract.cpp)
    target_link_libraries(cubey_project_sdf_cached_cell_contract PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_executable(cubey_project_sdf_guarded_contract tools/guarded_contract.cpp)
    target_link_libraries(cubey_project_sdf_guarded_contract PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_executable(cubey_project_sdf_query_pruning_pilot tools/query_pruning_pilot.cpp sdf_regions.cpp sdf_bounds.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp)
    target_link_libraries(cubey_project_sdf_query_pruning_pilot PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_executable(cubey_project_sdf_shadow_contract tools/shadow_contract.cpp)
    target_link_libraries(cubey_project_sdf_shadow_contract PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_executable(cubey_project_sdf_prepared_cone_probe tools/prepared_cone_probe.cpp sdf_prepared_cones.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp)
    target_link_libraries(cubey_project_sdf_prepared_cone_probe PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_executable(cubey_project_sdf_render_mode_tests sdf_render_modes.cpp sdf_render_mode_tests.cpp sdf_timeline.cpp sdf_recipe.cpp)
    target_link_libraries(cubey_project_sdf_render_mode_tests PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_render_mode_tests COMMAND cubey_project_sdf_render_mode_tests)
    add_executable(cubey_project_sdf_shader_selection_tests sdf_shader_selection.cpp sdf_shader_selection_tests.cpp sdf_render_modes.cpp)
    target_link_libraries(cubey_project_sdf_shader_selection_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_shader_selection_tests COMMAND cubey_project_sdf_shader_selection_tests)
    add_executable(cubey_project_sdf_shader_stats tools/profile_shader_stats.cpp)
    target_link_libraries(cubey_project_sdf_shader_stats PRIVATE Vulkan::Vulkan nlohmann_json::nlohmann_json)
    add_executable(cubey_project_sdf_region_tests sdf_regions.cpp sdf_bounds.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp sdf_region_tests.cpp)
    target_link_libraries(cubey_project_sdf_region_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_region_tests COMMAND cubey_project_sdf_region_tests "${CMAKE_CURRENT_SOURCE_DIR}/comparison/fixtures.json")
    add_executable(cubey_project_sdf_recipe_tests sdf_recipe.cpp sdf_recipe_tests.cpp sdf_timeline.cpp)
    target_link_libraries(cubey_project_sdf_recipe_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_recipe_tests COMMAND cubey_project_sdf_recipe_tests)
    add_executable(cubey_project_sdf_authored_tests sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_recipe.cpp sdf_authored_tests.cpp sdf_material.cpp)
    target_link_libraries(cubey_project_sdf_authored_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_authored_tests COMMAND cubey_project_sdf_authored_tests)
    add_executable(cubey_project_sdf_stroke_preparation_tests sdf_cached_stroke.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_recipe.cpp sdf_stroke_preparation_tests.cpp sdf_material.cpp)
    target_link_libraries(cubey_project_sdf_stroke_preparation_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_stroke_preparation_tests COMMAND cubey_project_sdf_stroke_preparation_tests)
    add_executable(cubey_project_sdf_material_tests sdf_material.cpp sdf_material_tests.cpp)
    target_link_libraries(cubey_project_sdf_material_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_material_tests COMMAND cubey_project_sdf_material_tests)
    add_test(NAME sdf_normal_projection_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/normal_projection_tests.py")
    add_test(NAME sdf_mirror_author_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mirror_author_tests.py")
    add_test(NAME sdf_stroke_sampling_tests
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_stroke_sampling.py")
endif()

target_compile_definitions(cubey_project_sdf PRIVATE CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")

if(BUILD_TESTING)
    add_executable(cubey_sdf_workshop_tests sdf_workshop_tests.cpp sdf_workshop.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_recipe.cpp sdf_material.cpp)
    target_link_libraries(cubey_sdf_workshop_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_sdf_workshop_tests PRIVATE CUBEY_SOURCE_DIR="${PROJECT_SOURCE_DIR}" CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")
    add_test(NAME sdf_workshop_tests COMMAND cubey_sdf_workshop_tests)
endif()
