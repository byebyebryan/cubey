set(
    CUBEY_SHADERTOY_REF_DIR
    "${CMAKE_SOURCE_DIR}/../ref/ShaderToy"
    CACHE PATH
    "Directory containing external ShaderToy reference sources"
)

set(CUBEY_SHADERTOY_MOUNTAINS_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/mountains.glsl")
set(CUBEY_SHADERTOY_MOUNTAIN_PEAK_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/mountain_peak.glsl")
set(CUBEY_SHADERTOY_SWISS_COMMON_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/swiss_alps_common.glsl")
set(CUBEY_SHADERTOY_SWISS_TERRAIN_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/swiss_alps_buffer_b.glsl")
set(CUBEY_SHADERTOY_EROSION_COMMON_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/eroded_mountains_common.glsl")
set(CUBEY_SHADERTOY_EROSION_FILTER_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/eroded_mountains_buffer_b.glsl")
set(
    CUBEY_SHADERTOY_STUDY_SOURCES
    "${CUBEY_SHADERTOY_MOUNTAINS_SOURCE}"
    "${CUBEY_SHADERTOY_MOUNTAIN_PEAK_SOURCE}"
    "${CUBEY_SHADERTOY_SWISS_COMMON_SOURCE}"
    "${CUBEY_SHADERTOY_SWISS_TERRAIN_SOURCE}"
    "${CUBEY_SHADERTOY_EROSION_COMMON_SOURCE}"
    "${CUBEY_SHADERTOY_EROSION_FILTER_SOURCE}"
)
foreach(CUBEY_SHADERTOY_STUDY_SOURCE IN LISTS CUBEY_SHADERTOY_STUDY_SOURCES)
    if (NOT EXISTS "${CUBEY_SHADERTOY_STUDY_SOURCE}")
        message(STATUS "Skipping terrain ShaderToy study: ${CUBEY_SHADERTOY_STUDY_SOURCE} was not found")
        return()
    endif()
endforeach()

add_library(cubey_study_terrain_shadertoy_reference_core STATIC
    terrain_shadertoy_ref_camera.cpp
    terrain_shadertoy_ref_config.cpp
    terrain_shadertoy_ref_mesh.cpp
)
target_include_directories(
    cubey_study_terrain_shadertoy_reference_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(
    cubey_study_terrain_shadertoy_reference_core
    PRIVATE
        CUBEY_TERRAIN_SHADERTOY_REF_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
target_link_libraries(
    cubey_study_terrain_shadertoy_reference_core
    PUBLIC
        cubey::core
        cubey::render
    PRIVATE
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(
        cubey_study_terrain_shadertoy_tests
        terrain_shadertoy_ref_camera.cpp
        terrain_shadertoy_ref_config.cpp
        terrain_shadertoy_ref_tests.cpp
    )
    target_include_directories(
        cubey_study_terrain_shadertoy_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_study_terrain_shadertoy_tests
        PRIVATE
            cubey::core
            cubey_project_warnings
    )
    add_test(
        NAME terrain_shadertoy_ref_tests
        COMMAND cubey_study_terrain_shadertoy_tests
    )
    cubey_label_tests(
        reference
        terrain_shadertoy_ref_tests
    )
endif()
