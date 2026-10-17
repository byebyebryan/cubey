add_executable(cubey_fluid_resource_lifecycle_tests resource_lifecycle_tests.cpp
    ../sim/smoke_2d/smoke_2d_gpu_resources.cpp
    ../sim/smoke_2d/smoke_2d_injectors.cpp
    ../sim/water_2d/water_2d_gpu_resources.cpp
    ../sim/water_3d/water_3d_gpu_resources.cpp
    ../sim/pyro_3d/pyro_3d_gpu_resources.cpp
    ../sim/pyro_3d/pyro_3d_sources.cpp
    ../sim/fluid_25d/fluid_25d_gpu_resources.cpp)
target_link_libraries(cubey_fluid_resource_lifecycle_tests PRIVATE cubey::cubey cubey::host cubey_project_fluid_common
    cubey_project_options cubey_project_warnings)
foreach(family smoke_2d water_2d water_3d pyro_3d fluid_25d)
    string(TOUPPER "${family}" prefix)
    set(frontend "${family}")
    if(family STREQUAL "pyro_3d")
        set(frontend fire_3d)
    endif()
    target_compile_definitions(cubey_fluid_resource_lifecycle_tests PRIVATE
        CUBEY_${prefix}_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/fluid-resource-shaders/${family}"
        CUBEY_TEST_${prefix}_SOURCE_SHADER_DIR="${CMAKE_BINARY_DIR}/projects/fluid/${frontend}/shaders")
    add_dependencies(cubey_fluid_resource_lifecycle_tests cubey_project_${frontend}_shaders)
endforeach()
add_test(NAME fluid_resource_lifecycle_gpu COMMAND cubey_fluid_resource_lifecycle_tests)
add_test(NAME fluid_resource_lifecycle_sync COMMAND cubey_fluid_resource_lifecycle_tests)
set_tests_properties(fluid_resource_lifecycle_gpu PROPERTIES LABELS "fluid;headless;gpu"
    TIMEOUT 360 FAIL_REGULAR_EXPRESSION "vulkan validation error")
set_tests_properties(fluid_resource_lifecycle_sync PROPERTIES LABELS "fluid;headless;gpu;sync_validation"
    ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1" TIMEOUT 360
    FAIL_REGULAR_EXPRESSION "vulkan validation error")
set_tests_properties(fluid_resource_lifecycle_gpu fluid_resource_lifecycle_sync PROPERTIES
    RESOURCE_LOCK fluid_resource_shader_links)
