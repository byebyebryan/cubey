add_library(cubey_project_terrain_core STATIC
    terrain_patch.cpp
    upland_mountain_source.cpp
)
target_include_directories(
    cubey_project_terrain_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_core
    PUBLIC
        cubey::core
    PRIVATE
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_tests
        terrain_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_tests
        PRIVATE
            cubey_project_terrain_core
            cubey_project_warnings
    )
    add_test(NAME terrain_tests COMMAND cubey_project_terrain_tests)
    cubey_label_tests(terrain terrain_tests)
endif()
