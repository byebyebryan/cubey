add_library(cubey_study_terrain_hydrology_core STATIC
    terrain_hydrology.cpp
    terrain_landscape_evolution.cpp
    terrain_landscape_graph.cpp
    terrain_patch.cpp
    terrain_visualization.cpp
    upland_broad_noise_source.cpp
    upland_landscape_evolution_source.cpp
    upland_mountain_source.cpp
)
target_include_directories(
    cubey_study_terrain_hydrology_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_study_terrain_hydrology_core
    PUBLIC
        cubey::core
    PRIVATE
        cubey_project_warnings
)

add_library(cubey_study_terrain_hydrology_export STATIC
    terrain_export.cpp
)
target_include_directories(
    cubey_study_terrain_hydrology_export
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_study_terrain_hydrology_export
    PUBLIC
        cubey_study_terrain_hydrology_core
    PRIVATE
        cubey::engine
        nlohmann_json::nlohmann_json
        cubey_project_warnings
)

add_executable(cubey_study_terrain_hydrology_generate
    terrain_generate_main.cpp
)
set_target_properties(
    cubey_study_terrain_hydrology_generate
    PROPERTIES OUTPUT_NAME terrain_hydrology_generate
)
target_link_libraries(
    cubey_study_terrain_hydrology_generate
    PRIVATE
        cubey_study_terrain_hydrology_export
        cubey_project_warnings
)

add_library(cubey_study_terrain_hydrology_render_support STATIC
    terrain_mesh.cpp
)
target_include_directories(
    cubey_study_terrain_hydrology_render_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_study_terrain_hydrology_render_support
    PUBLIC
        cubey_study_terrain_hydrology_core
        cubey::render
    PRIVATE
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(cubey_study_terrain_hydrology_tests
        terrain_tests.cpp
    )
    target_link_libraries(
        cubey_study_terrain_hydrology_tests
        PRIVATE
            cubey_study_terrain_hydrology_export
            cubey_study_terrain_hydrology_render_support
            nlohmann_json::nlohmann_json
            cubey_project_warnings
    )
    add_test(NAME terrain_hydrology_lab_tests COMMAND cubey_study_terrain_hydrology_tests)

    set(CUBEY_TERRAIN_HYDROLOGY_LAB_GENERATE_TEST_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-hydrology-generate-test"
    )
    add_test(
        NAME terrain_hydrology_generate_exports_fields
        COMMAND cubey_study_terrain_hydrology_generate
            --grid-size 17
            --terrain-seed 42
            --terrain-origin-x -512
            --terrain-origin-z 256
            --terrain-export-raw
            --terrain-output-dir "${CUBEY_TERRAIN_HYDROLOGY_LAB_GENERATE_TEST_OUTPUT}"
    )
    set_tests_properties(
        terrain_hydrology_generate_exports_fields
        PROPERTIES
            FIXTURES_SETUP terrain_hydrology_generate_output
    )
    add_test(
        NAME terrain_hydrology_generate_exports_manifest
        COMMAND "${CMAKE_COMMAND}"
            -DOUTPUT_DIR=${CUBEY_TERRAIN_HYDROLOGY_LAB_GENERATE_TEST_OUTPUT}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_terrain_export.cmake"
    )
    set_tests_properties(
        terrain_hydrology_generate_exports_manifest
        PROPERTIES
            FIXTURES_REQUIRED terrain_hydrology_generate_output
    )

    cubey_label_tests(
        terrain_hydrology
        terrain_hydrology_lab_tests
        terrain_hydrology_generate_exports_fields
        terrain_hydrology_generate_exports_manifest
    )
endif()
