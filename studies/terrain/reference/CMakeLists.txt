add_library(cubey_study_terrain_reference_core STATIC
    shadertoy_biome_reference.cpp
    shadertoy_erosion_reference.cpp
    shadertoy_mountain_reference.cpp
    terrain_engine_reference.cpp
    terrain_ref_config.cpp
    terrain_ref_mesh.cpp
)
target_include_directories(
    cubey_study_terrain_reference_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_study_terrain_reference_core
    PUBLIC
        cubey::core
        cubey::render
    PRIVATE
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(cubey_study_terrain_reference_tests
        terrain_ref_tests.cpp
    )
    target_link_libraries(
        cubey_study_terrain_reference_tests
        PRIVATE
            cubey_study_terrain_reference_core
            cubey_project_warnings
    )
    add_test(NAME terrain_ref_tests COMMAND cubey_study_terrain_reference_tests)
    cubey_label_tests(
        reference
        terrain_ref_tests
    )
endif()
