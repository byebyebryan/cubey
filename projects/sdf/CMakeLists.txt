cubey_atmosphere_shader_depends(CUBEY_SDF_ATMOSPHERE_DEPENDS)
cubey_cloud_layer_shader_sources(CUBEY_SDF_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_SDF_CLOUD_DEPENDS)
cubey_add_app_target(
    cubey_project_sdf
    OUTPUT_NAME sdf
    SHADER_DEFINE CUBEY_SDF_SHADER_DIR
    WINDOWED_SMOKE_TEST sdf_reaches_glfw
    WINDOWED_SMOKE_PATTERN "sdf: .*rendering sparse SDF bricks"
    SOURCES main.cpp sdf_workshop.cpp sdf_app.cpp sdf_field.cpp sdf_recipe.cpp sdf_validation.cpp sdf_renderer.cpp sdf_environment.cpp sdf_timeline.cpp sdf_authored.cpp sdf_history.cpp sdf_material.cpp sdf_comparison.cpp sdf_regions.cpp sdf_bounds.cpp sdf_shader_selection.cpp sdf_prepared_cones.cpp
    SHADERS
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
    add_executable(cubey_project_sdf_shadow_contract tools/shadow_contract.cpp)
    target_link_libraries(cubey_project_sdf_shadow_contract PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_executable(cubey_project_sdf_prepared_cone_probe tools/prepared_cone_probe.cpp sdf_prepared_cones.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_material.cpp sdf_recipe.cpp)
    target_link_libraries(cubey_project_sdf_prepared_cone_probe PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_executable(cubey_project_sdf_shader_selection_tests sdf_shader_selection.cpp sdf_shader_selection_tests.cpp)
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
    add_executable(cubey_project_sdf_material_tests sdf_material.cpp sdf_material_tests.cpp)
    target_link_libraries(cubey_project_sdf_material_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_material_tests COMMAND cubey_project_sdf_material_tests)
    add_test(NAME sdf_normal_projection_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/normal_projection_tests.py")
    add_test(NAME sdf_mirror_author_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mirror_author_tests.py")
endif()

target_compile_definitions(cubey_project_sdf PRIVATE CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")

if(BUILD_TESTING)
    add_executable(cubey_sdf_workshop_tests sdf_workshop_tests.cpp sdf_workshop.cpp sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_recipe.cpp sdf_material.cpp)
    target_link_libraries(cubey_sdf_workshop_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_sdf_workshop_tests PRIVATE CUBEY_SOURCE_DIR="${PROJECT_SOURCE_DIR}" CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")
    add_test(NAME sdf_workshop_tests COMMAND cubey_sdf_workshop_tests)
endif()
