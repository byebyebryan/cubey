cubey_add_app_target(cubey_project_raytracing
    OUTPUT_NAME raytracing
    SHADER_DEFINE CUBEY_RAYTRACING_SHADER_DIR
    SHADER_TARGET_ENV vulkan1.3
    SOURCES main.cpp raytracing_app.cpp raytracing_scene.cpp raytracing_renderer.cpp
    SHADERS shaders/trace.rgen shaders/primary.rchit shaders/primary.rmiss shaders/shadow.rmiss
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
    SHADER_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

if(BUILD_TESTING)
    add_executable(cubey_project_raytracing_tests raytracing_tests.cpp)
    target_link_libraries(cubey_project_raytracing_tests PRIVATE cubey::host cubey_project_warnings)
    add_test(NAME raytracing_config_and_layout COMMAND cubey_project_raytracing_tests)
    add_executable(cubey_project_raytracing_device_tests raytracing_device_tests.cpp)
    target_link_libraries(cubey_project_raytracing_device_tests PRIVATE cubey::vulkan cubey_project_warnings)
    add_test(NAME raytracing_device_contract COMMAND cubey_project_raytracing_device_tests)
    cubey_add_png_smoke_test(raytracing_headless_writes_png cubey_project_raytracing
        "${CMAKE_CURRENT_BINARY_DIR}/raytracing-smoke.png")
    foreach(stage scene pipeline target)
        cubey_add_png_capture_test(raytracing_retry_${stage} cubey_project_raytracing
            "${CMAKE_CURRENT_BINARY_DIR}/retry-${stage}.png" 128 96 --require-validation --fault ${stage} --exercise-retry)
    endforeach()
    set_tests_properties(raytracing_device_contract raytracing_headless_writes_png raytracing_retry_scene raytracing_retry_pipeline raytracing_retry_target
        PROPERTIES SKIP_REGULAR_EXPRESSION "no compatible Vulkan 1.3 hardware ray-tracing device")
endif()
