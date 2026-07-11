add_library(cubey_project_terrain_runtime STATIC
    terrain_source.cpp
    terrain_source_gpu.cpp
)
target_include_directories(
    cubey_project_terrain_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_runtime
    PUBLIC
        cubey::core
    PRIVATE
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_source_tests
        terrain_source_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_source_tests
        PRIVATE
            cubey_project_terrain_runtime
            cubey_project_warnings
    )
    add_test(NAME terrain_source_tests COMMAND cubey_project_terrain_source_tests)

    add_executable(cubey_project_terrain_source_gpu_parity_tests
        terrain_source_gpu_parity.cpp
    )
    target_link_libraries(
        cubey_project_terrain_source_gpu_parity_tests
        PRIVATE
            cubey_project_terrain_runtime
            cubey::render
            cubey_project_warnings
    )
    target_compile_definitions(
        cubey_project_terrain_source_gpu_parity_tests
        PRIVATE
            CUBEY_TERRAIN_GPU_PARITY_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/gpu-parity"
    )
    cubey_add_glsl_shaders(
        cubey_project_terrain_source_gpu_parity_tests
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gpu-parity"
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_source_parity.comp"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_source.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
            "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
    )
    add_test(
        NAME terrain_source_gpu_parity_tests
        COMMAND cubey_project_terrain_source_gpu_parity_tests
    )
    set_tests_properties(
        terrain_source_gpu_parity_tests
        PROPERTIES
            TIMEOUT 30
            SKIP_RETURN_CODE 77
    )

    cubey_label_tests(
        terrain
        terrain_source_tests
        terrain_source_gpu_parity_tests
    )
endif()
