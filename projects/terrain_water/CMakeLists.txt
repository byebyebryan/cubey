# Dedicated application frontend; the retained shallow-water implementation
# and backend adapters are shared, not copied or linked to CUDA.
include("${CMAKE_SOURCE_DIR}/projects/fluid/sim/fluid_25d/Fluid25DApp.cmake")
cubey_add_fluid25d_app(cubey_project_terrain_water terrain_water
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_include_directories(cubey_project_terrain_water PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(BUILD_TESTING)
    add_executable(cubey_project_terrain_water_config_tests terrain_water_config_tests.cpp)
    target_link_libraries(cubey_project_terrain_water_config_tests PRIVATE
        cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_test(NAME terrain_water_config COMMAND cubey_project_terrain_water_config_tests)
    set_tests_properties(terrain_water_config PROPERTIES LABELS "unit;terrain;water;backend")
    if(CUBEY_FLUID_RECORDING_PYTHON)
        add_test(NAME terrain_water_project_split_gpu
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/test_project_split_gpu.py"
                --legacy "$<TARGET_FILE:cubey_project_fluid_25d>"
                --terrain "$<TARGET_FILE:cubey_project_terrain_water>")
        set_tests_properties(terrain_water_project_split_gpu PROPERTIES
            LABELS "gpu;headless;terrain;water;backend" TIMEOUT 120)
    endif()
endif()
