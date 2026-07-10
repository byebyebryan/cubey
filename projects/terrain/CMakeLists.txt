add_library(cubey_project_terrain_core STATIC
    terrain_hydrology.cpp
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

add_library(cubey_project_terrain_export STATIC
    terrain_export.cpp
)
target_include_directories(
    cubey_project_terrain_export
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_export
    PUBLIC
        cubey_project_terrain_core
    PRIVATE
        cubey::engine
        nlohmann_json::nlohmann_json
        cubey_project_warnings
)

add_executable(cubey_project_terrain_generate
    terrain_generate_main.cpp
)
set_target_properties(cubey_project_terrain_generate PROPERTIES OUTPUT_NAME terrain_generate)
target_link_libraries(
    cubey_project_terrain_generate
    PRIVATE
        cubey_project_terrain_export
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_tests
        terrain_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_tests
        PRIVATE
            cubey_project_terrain_export
            nlohmann_json::nlohmann_json
            cubey_project_warnings
    )
    add_test(NAME terrain_tests COMMAND cubey_project_terrain_tests)

    set(CUBEY_TERRAIN_GENERATE_TEST_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/terrain-generate-test")
    add_test(
        NAME terrain_generate_exports_fields
        COMMAND cubey_project_terrain_generate
            --grid-size 17
            --terrain-seed 42
            --terrain-output-dir "${CUBEY_TERRAIN_GENERATE_TEST_OUTPUT}"
    )
    set_tests_properties(
        terrain_generate_exports_fields
        PROPERTIES
            FIXTURES_SETUP terrain_generate_output
    )
    add_test(
        NAME terrain_generate_exports_manifest
        COMMAND "${CMAKE_COMMAND}"
            -DOUTPUT_DIR=${CUBEY_TERRAIN_GENERATE_TEST_OUTPUT}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_terrain_export.cmake"
    )
    set_tests_properties(
        terrain_generate_exports_manifest
        PROPERTIES
            FIXTURES_REQUIRED terrain_generate_output
    )

    cubey_label_tests(
        terrain
        terrain_tests
        terrain_generate_exports_fields
        terrain_generate_exports_manifest
    )
endif()
