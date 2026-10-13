add_executable(cubey_core_tests
    animation_tests.cpp
    atmosphere_environment_runtime_tests.cpp
    config_schema_tests.cpp
    camera_manager_tests.cpp
    camera_tests.cpp
    capture_queue_tests.cpp
    command_pool_tests.cpp
    command_recorder_tests.cpp
    compute_pipeline_tests.cpp
    descriptor_tests.cpp
    dynamic_rendering_tests.cpp
    engine_tests.cpp
    example_cube_motion_tests.cpp
    entity_tests.cpp
    example_lighting_tests.cpp
    file_io_tests.cpp
    frame_clock_tests.cpp
    gltf_asset_tests.cpp
    gltf_asset_test_support.cpp
    gltf_asset_probe_tests.cpp
    gltf_asset_material_tests.cpp
    gltf_asset_geometry_tests.cpp
    gltf_asset_scene_animation_tests.cpp
    gltf_asset_image_tests.cpp
    terrain_raster_height_source_tests.cpp
    gltf_scene_importer_animation_tests.cpp
    frame_resources_tests.cpp
    host_headless_png_host_tests.cpp
    host_frame_stats_tests.cpp
    host_performance_ui_tests.cpp
    hdr_image_tests.cpp
    submission_tickets_tests.cpp
    gpu_runtime_tests.cpp
    image_io_tests.cpp
    immediate_commands_tests.cpp
    jobs_tests.cpp
    light_manager_tests.cpp
    input_tests.cpp
    main.cpp
    material_instance_tests.cpp
    material_pass_tests.cpp
    math_tests.cpp
    orbit_controller_tests.cpp
    forward_pbr_renderer_3d_tests.cpp
    pipeline_tests.cpp
    profiling_tests.cpp
    procedural_artifact_cache_tests.cpp
    procedural_foundation_tests.cpp
    procedural_shader_parity_tests.cpp
    project_gpu_services_tests.cpp
    project_runtime_tests.cpp
    queue_submit_tests.cpp
    image_transitions_tests.cpp
    render_context_tests.cpp
    render_clipmap_grid_2d_tests.cpp
    render_cloud_layer_tests.cpp
    render_atmosphere_environment_tests.cpp
    render_adaptive_patch_lod_tests.cpp
    render_deformation_tests.cpp
    render_frame_data_tests.cpp
    render_generated_ibl_tests.cpp
    render_graph_execution_tests.cpp
    render_graph_frame_tests.cpp
    render_graph_sync_tests.cpp
    render_graph_tests.cpp
    render_item_tests.cpp
    render_local_tangent_frame_tests.cpp
    render_mesh_tests.cpp
    render_pass_tests.cpp
    render_pbr_tests.cpp
    render_pipeline_resource_tests.cpp
    render_primitive_mesh_tests.cpp
    render_resource_registry_tests.cpp
    render_resource_table_tests.cpp
    render_shadow_map_tests.cpp
    render_shader_package_tests.cpp
    render_target_tests.cpp
    render_terrain_ocean_fields_tests.cpp
    render_texture_tests.cpp
    render_uniform_buffer_tests.cpp
    render_view_ray_basis_tests.cpp
    renderable_manager_tests.cpp
    resource_tests.cpp
    scene_tests.cpp
    scene_builder_tests.cpp
    scene_render_plan_tests.cpp
    scene_view_3d_tests.cpp
    sampler_tests.cpp
    shader_bytecode_tests.cpp
    staged_resource_tests.cpp
    submission_coordinator_tests.cpp
    swapchain_tests.cpp
    transform_manager_tests.cpp
    transform_tests.cpp
    ui_foundation_tests.cpp
    video_encoder_tests.cpp
    test_registry.cpp
    test_registry_asset.cpp
    test_registry_core.cpp
    test_registry_engine_host_input.cpp
    test_registry_render.cpp
    test_registry_scene.cpp
    test_registry_vulkan.cpp
    upload_queue_tests.cpp
    vulkan_buffer_tests.cpp
    windowed_host_tests.cpp
)

target_link_libraries(
    cubey_core_tests
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

target_compile_definitions(
    cubey_core_tests
    PRIVATE
        CUBEY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

cubey_shared_shader_depends(CUBEY_TEST_SHARED_SHADER_DEPENDS)

add_custom_target(cubey_procedural_shader_smoke)
cubey_add_glsl_shaders(
    cubey_procedural_shader_smoke
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/procedural_shader_smoke"
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/procedural_include_smoke.comp"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
        ${CUBEY_TEST_SHARED_SHADER_DEPENDS}
        "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
)
add_dependencies(cubey_core_tests cubey_procedural_shader_smoke)

add_test(NAME cubey_core_tests COMMAND cubey_core_tests)
add_executable(cubey_terrain_water_capture_tests terrain_water_capture_tests.cpp)
target_link_libraries(cubey_terrain_water_capture_tests PRIVATE
    cubey::asset nlohmann_json::nlohmann_json cubey_project_warnings)
target_compile_definitions(cubey_terrain_water_capture_tests PRIVATE CUBEY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME terrain_water_capture_tests COMMAND cubey_terrain_water_capture_tests)
set_tests_properties(terrain_water_capture_tests PROPERTIES LABELS "unit;terrain;asset")
set_property(TEST cubey_core_tests APPEND PROPERTY LABELS "gltf;core")

add_executable(cubey_headless_failure_gpu_tests headless_failure_gpu_tests.cpp)
target_link_libraries(cubey_headless_failure_gpu_tests PRIVATE cubey::cubey cubey::host)
add_test(NAME headless_failure_gpu_tests
    COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY --unset=XAUTHORITY
        --unset=XDG_SESSION_TYPE $<TARGET_FILE:cubey_headless_failure_gpu_tests>)
set_tests_properties(headless_failure_gpu_tests PROPERTIES
    LABELS "headless;gpu;core" SKIP_RETURN_CODE 77 TIMEOUT 60
    FAIL_REGULAR_EXPRESSION "vulkan validation error")

if (CUBEY_ENABLE_TSAN_TESTS)
    add_test(
        NAME cubey_tsan_concurrency_tests
        COMMAND
            cubey_core_tests
            --filter test_job_system_
            --filter test_gpu_work_queue_
            --filter test_gpu_runtime_
            --filter test_capture_queue_
            --filter test_capture_backlog_
            --filter test_upload_queue_
            --filter test_staged_resource_
            --filter test_project_context_
            --filter test_project_gpu_services_
            --filter test_project_runtime_
            --filter test_entity_manager_concurrent_reservations_are_unique
            --filter test_scene_concurrent_edit_queues_publish_through_serialized_commits
    )
    set_tests_properties(
        cubey_tsan_concurrency_tests
        PROPERTIES
            LABELS "core;tsan"
            TIMEOUT 60
    )
endif()

add_executable(cubey_config_v2_tests config_v2_tests.cpp)
target_link_libraries(
    cubey_config_v2_tests
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_options
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)
add_test(NAME cubey_config_v2_tests COMMAND cubey_config_v2_tests)

add_executable(
    cubey_backdrop_surface_placement_tests
    backdrop_surface_placement_tests.cpp
)
target_link_libraries(
    cubey_backdrop_surface_placement_tests
    PRIVATE
        cubey::render
        cubey_project_warnings
)
add_test(
    NAME backdrop_surface_placement_tests
    COMMAND cubey_backdrop_surface_placement_tests
)

foreach(test_name IN ITEMS
    terrain_backdrop_product
    terrain_backdrop_product_cache
    terrain_backdrop_placement
    terrain_backdrop_preparation
    terrain_backdrop_surface
    terrain_directional_placement
    terrain_surface_field
)
    add_executable(
        cubey_${test_name}_tests
        "terrain/${test_name}_tests.cpp"
    )
    target_link_libraries(
        cubey_${test_name}_tests
        PRIVATE
            cubey::terrain
            cubey_project_warnings
    )
    add_test(NAME ${test_name}_tests COMMAND cubey_${test_name}_tests)
    set_tests_properties(
        ${test_name}_tests
        PROPERTIES LABELS terrain TIMEOUT 120
    )
endforeach()

target_compile_definitions(
    cubey_terrain_backdrop_preparation_tests
    PRIVATE
        CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET="${CMAKE_SOURCE_DIR}/tests/assets/terrain/backdrop-smoke"
)

add_executable(cubey_procedural_gpu_parity_tests
    procedural_gpu_parity_main.cpp
)
target_link_libraries(
    cubey_procedural_gpu_parity_tests
    PRIVATE
        cubey::render
        cubey_project_warnings
)
target_compile_definitions(
    cubey_procedural_gpu_parity_tests
    PRIVATE
        CUBEY_PROCEDURAL_GPU_PARITY_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/procedural_gpu_parity"
)
cubey_add_glsl_shaders(
    cubey_procedural_gpu_parity_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/procedural_gpu_parity"
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/procedural_gpu_parity.comp"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/operators.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/random.glsl"
        "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
)
add_test(NAME procedural_gpu_parity_tests COMMAND cubey_procedural_gpu_parity_tests)
set_tests_properties(
    procedural_gpu_parity_tests
    PROPERTIES
        LABELS "gpu;sanitizer_external"
        TIMEOUT 20
        SKIP_RETURN_CODE 77
)
add_executable(cubey_terrain_surface_patch_gap_tests terrain_surface_patch_gap_tests.cpp)
add_executable(cubey_terrain_site_tests terrain_site_tests.cpp)
target_link_libraries(cubey_terrain_site_tests PRIVATE cubey::asset cubey_project_warnings)
add_test(NAME terrain_site_cpu COMMAND cubey_terrain_site_tests)

add_executable(cubey_terrain_surface_context_tests terrain_surface_context_tests.cpp)
add_executable(cubey_terrain_surface_patch_lod_tests terrain_surface_patch_lod_tests.cpp)
target_link_libraries(cubey_terrain_surface_patch_lod_tests PRIVATE cubey::terrain cubey_project_warnings)
add_test(NAME terrain_surface_patch_lod_cpu COMMAND cubey_terrain_surface_patch_lod_tests)
set_tests_properties(terrain_surface_patch_lod_cpu PROPERTIES TIMEOUT 60)
add_executable(cubey_terrain_world_water_lod_preflight terrain_world_water_lod_preflight.cpp)
target_link_libraries(cubey_terrain_world_water_lod_preflight PRIVATE cubey::terrain cubey_project_warnings)
add_test(NAME terrain_world_water_lod_cpu COMMAND cubey_terrain_world_water_lod_preflight)
set_tests_properties(terrain_world_water_lod_cpu PROPERTIES LABELS "terrain;water;unit" TIMEOUT 60)
target_link_libraries(cubey_terrain_surface_context_tests PRIVATE cubey::terrain cubey_project_warnings)
target_compile_definitions(cubey_terrain_surface_context_tests PRIVATE
    CUBEY_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets")
add_test(NAME terrain_surface_context_cpu COMMAND cubey_terrain_surface_context_tests)
set_tests_properties(terrain_surface_context_cpu PROPERTIES TIMEOUT 60)
add_executable(cubey_terrain_surface_context_edge_tests terrain_surface_context_edge_tests.cpp)
target_link_libraries(cubey_terrain_surface_context_edge_tests PRIVATE cubey::render cubey::terrain cubey_project_warnings)
target_compile_definitions(cubey_terrain_surface_context_edge_tests PRIVATE
    CUBEY_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets"
    CUBEY_TERRAIN_PATCH_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/context-test-shaders")
cubey_terrain_surface_patch_shader_depends(CUBEY_TERRAIN_CONTEXT_TEST_DEPENDS)
cubey_add_glsl_shaders(cubey_terrain_surface_context_edge_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/context-test-shaders"
    SOURCES terrain_surface_context_edge.comp DEPENDS ${CUBEY_TERRAIN_CONTEXT_TEST_DEPENDS})
add_test(NAME terrain_surface_context_edge_gpu COMMAND cubey_terrain_surface_context_edge_tests)
set_tests_properties(terrain_surface_context_edge_gpu PROPERTIES LABELS "terrain;water;headless;gpu"
    SKIP_RETURN_CODE 77 TIMEOUT 60 FAIL_REGULAR_EXPRESSION "vulkan validation error")
target_link_libraries(cubey_terrain_surface_patch_gap_tests PRIVATE cubey::render cubey_project_warnings)
target_compile_definitions(cubey_terrain_surface_patch_gap_tests PRIVATE
    CUBEY_TERRAIN_PATCH_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/patch-test-shaders")
cubey_terrain_surface_patch_shader_depends(CUBEY_TERRAIN_PATCH_TEST_DEPENDS)
add_executable(cubey_terrain_surface_patch_lod_gap_tests terrain_surface_patch_lod_gap_tests.cpp)
target_link_libraries(cubey_terrain_surface_patch_lod_gap_tests PRIVATE cubey::render cubey::terrain cubey_project_warnings)
target_compile_definitions(cubey_terrain_surface_patch_lod_gap_tests PRIVATE
    CUBEY_TERRAIN_PATCH_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/lod-test-shaders")
cubey_add_glsl_shaders(cubey_terrain_surface_patch_lod_gap_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/lod-test-shaders"
    SOURCES terrain_surface_patch_lod_gap.comp DEPENDS ${CUBEY_TERRAIN_PATCH_TEST_DEPENDS})
add_test(NAME terrain_surface_patch_lod_gap_gpu COMMAND cubey_terrain_surface_patch_lod_gap_tests)
set_tests_properties(terrain_surface_patch_lod_gap_gpu PROPERTIES LABELS "terrain;water;headless;gpu"
    SKIP_RETURN_CODE 77 TIMEOUT 60 FAIL_REGULAR_EXPRESSION "vulkan validation error")
cubey_add_glsl_shaders(cubey_terrain_surface_patch_gap_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/patch-test-shaders"
    SOURCES terrain_surface_patch_gap.comp DEPENDS ${CUBEY_TERRAIN_PATCH_TEST_DEPENDS})
add_test(NAME terrain_surface_patch_gap_gpu COMMAND cubey_terrain_surface_patch_gap_tests)
set_tests_properties(terrain_surface_patch_gap_gpu PROPERTIES LABELS "terrain;water;headless;gpu"
    SKIP_RETURN_CODE 77 TIMEOUT 60 FAIL_REGULAR_EXPRESSION "vulkan validation error")
add_executable(cubey_terrain_water_capture_lifecycle_tests terrain_water_capture_lifecycle_tests.cpp)
target_link_libraries(cubey_terrain_water_capture_lifecycle_tests PRIVATE cubey::engine cubey_project_warnings)
target_compile_definitions(cubey_terrain_water_capture_lifecycle_tests PRIVATE
    CUBEY_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets"
    CUBEY_TERRAIN_CAPTURE_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/capture-test-shaders")
cubey_terrain_surface_patch_shader_sources(CUBEY_TERRAIN_CAPTURE_TEST_SHADERS)
cubey_add_glsl_shaders(cubey_terrain_water_capture_lifecycle_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/capture-test-shaders"
    SOURCES ${CUBEY_TERRAIN_CAPTURE_TEST_SHADERS}
        "${PROJECT_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${PROJECT_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        "${PROJECT_SOURCE_DIR}/shaders/cubey/atmosphere_reflection_prefilter.frag"
    DEPENDS ${CUBEY_TERRAIN_PATCH_TEST_DEPENDS})
add_test(NAME terrain_water_capture_lifecycle_gpu COMMAND cubey_terrain_water_capture_lifecycle_tests)
set_tests_properties(terrain_water_capture_lifecycle_gpu PROPERTIES LABELS "terrain;water;headless;gpu"
    SKIP_RETURN_CODE 77 TIMEOUT 60 FAIL_REGULAR_EXPRESSION "vulkan validation error")
