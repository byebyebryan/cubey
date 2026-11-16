cubey_add_app_target(cubey_project_raytracing
    OUTPUT_NAME raytracing
    SHADER_DEFINE CUBEY_RAYTRACING_SHADER_DIR
    SHADER_TARGET_ENV vulkan1.3
    SOURCES main.cpp raytracing_app.cpp raytracing_scene.cpp raytracing_renderer.cpp raytracing_environment.cpp raytracing_optical_scene.cpp raytracing_dragon_support.cpp raytracing_materials.cpp raytracing_material_geometry.cpp raytracing_optical_asset.cpp
    SHADERS shaders/trace.rgen shaders/primary.rchit shaders/primary.rmiss shaders/shadow.rmiss
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere_reflection_prefilter.frag"
    SHADER_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
target_compile_definitions(cubey_project_raytracing PRIVATE CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")
target_compile_definitions(cubey_project_raytracing PRIVATE CUBEY_RAYTRACING_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

if(BUILD_TESTING)
    add_test(NAME raytracing_dragon_glass_acceptance
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_dragon_glass_acceptance.py")
    add_test(NAME raytracing_spawn_geometry
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_ray_spawn_geometry.py")
    add_test(NAME raytracing_material_comparison_inputs
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/author_material_comparison.py" --check)
    add_executable(cubey_project_raytracing_material_reference raytracing_material_reference.cpp)
    target_link_libraries(cubey_project_raytracing_material_reference PRIVATE cubey::cubey cubey_project_warnings)
    add_executable(cubey_project_raytracing_tests raytracing_tests.cpp raytracing_scene.cpp raytracing_optical_scene.cpp raytracing_dragon_support.cpp raytracing_materials.cpp raytracing_material_geometry.cpp raytracing_optical_asset.cpp)
    target_compile_definitions(cubey_project_raytracing_tests PRIVATE CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")
    target_compile_definitions(cubey_project_raytracing_tests PRIVATE CUBEY_RAYTRACING_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    target_link_libraries(cubey_project_raytracing_tests PRIVATE cubey::cubey cubey::host cubey_project_warnings)
    add_test(NAME raytracing_config_and_layout COMMAND cubey_project_raytracing_tests)
    add_executable(cubey_project_raytracing_device_tests raytracing_device_tests.cpp)
    target_link_libraries(cubey_project_raytracing_device_tests PRIVATE cubey::vulkan cubey::render cubey_project_warnings)
    add_test(NAME raytracing_device_contract COMMAND cubey_project_raytracing_device_tests)
    cubey_add_png_smoke_test(raytracing_headless_writes_png cubey_project_raytracing
        "${CMAKE_CURRENT_BINARY_DIR}/raytracing-smoke.png")
    foreach(stage scene pipeline target)
        cubey_add_png_capture_test(raytracing_retry_${stage} cubey_project_raytracing
            "${CMAKE_CURRENT_BINARY_DIR}/retry-${stage}.png" 128 96 --require-validation --fault ${stage} --exercise-retry)
    endforeach()
    cubey_add_png_capture_test(raytracing_atmosphere_reflection cubey_project_raytracing
        "${CMAKE_CURRENT_BINARY_DIR}/reflection-smoke.png" 128 96 --require-validation --scene reflection-fixture --environment atmosphere --target-y .55)
    cubey_add_png_capture_test(raytracing_atmosphere_refraction cubey_project_raytracing
        "${CMAKE_CURRENT_BINARY_DIR}/refraction-smoke.png" 128 96 --require-validation --scene refraction-fixture --environment atmosphere --target-y .55)
    cubey_add_png_capture_test(raytracing_atmosphere_retry cubey_project_raytracing
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-retry.png" 128 96 --require-validation --scene reflection-fixture --environment atmosphere --fault pipeline --exercise-retry)
    cubey_add_png_capture_test(raytracing_materials_environment cubey_project_raytracing
        "${CMAKE_CURRENT_BINARY_DIR}/materials-environment.png" 128 96 --require-validation --scene materials --samples 8)
    cubey_add_png_capture_test(raytracing_materials_scene cubey_project_raytracing
        "${CMAKE_CURRENT_BINARY_DIR}/materials-scene.png" 128 96 --require-validation --scene materials --samples 8 --reflection-source scene)
    foreach(stage pipeline target)
        cubey_add_png_capture_test(raytracing_materials_retry_${stage} cubey_project_raytracing
            "${CMAKE_CURRENT_BINARY_DIR}/materials-retry-${stage}.png" 128 96 --require-validation --scene materials --samples 8 --fault ${stage} --exercise-retry)
    endforeach()
    foreach(material glass frosted-glass gallery)
        cubey_add_png_capture_test(raytracing_materials_${material} cubey_project_raytracing
            "${CMAKE_CURRENT_BINARY_DIR}/materials-${material}.png" 128 96 --require-validation --scene materials --material ${material} --samples 8)
        set_tests_properties(raytracing_materials_${material} PROPERTIES LABELS "gpu;headless;sky;material"
            SKIP_REGULAR_EXPRESSION "no compatible Vulkan 1.3 hardware ray-tracing device")
    endforeach()
    add_test(NAME raytracing_sky_refresh
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_refresh.py"
            --binary "$<TARGET_FILE:cubey_project_raytracing>" --output "${CMAKE_CURRENT_BINARY_DIR}/sky-refresh")
    set_tests_properties(raytracing_atmosphere_reflection raytracing_atmosphere_refraction raytracing_atmosphere_retry raytracing_sky_refresh
        PROPERTIES LABELS "gpu;headless;sky" SKIP_REGULAR_EXPRESSION "no compatible Vulkan 1.3 hardware ray-tracing device")
    set_tests_properties(raytracing_materials_environment raytracing_materials_scene raytracing_materials_retry_pipeline raytracing_materials_retry_target
        PROPERTIES LABELS "gpu;headless;sky;material" SKIP_REGULAR_EXPRESSION "no compatible Vulkan 1.3 hardware ray-tracing device")
    set_tests_properties(raytracing_device_contract raytracing_headless_writes_png raytracing_retry_scene raytracing_retry_pipeline raytracing_retry_target
        PROPERTIES SKIP_REGULAR_EXPRESSION "no compatible Vulkan 1.3 hardware ray-tracing device")
endif()
