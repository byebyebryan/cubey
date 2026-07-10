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

add_library(cubey_project_terrain_render_support STATIC
    terrain_mesh.cpp
)
target_include_directories(
    cubey_project_terrain_render_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_render_support
    PUBLIC
        cubey_project_terrain_core
        cubey::render
    PRIVATE
        cubey_project_warnings
)

cubey_add_app_target(
    cubey_project_terrain
    OUTPUT_NAME terrain
    SHADER_DEFINE CUBEY_TERRAIN_SHADER_DIR
    WINDOWED_SMOKE_TEST terrain_reaches_glfw
    WINDOWED_SMOKE_PATTERN "terrain: .*rendering terrain product"
    WINDOWED_SMOKE_ARGS
        --grid-size
        65
    SOURCES
        main.cpp
        terrain_app.cpp
    SHADERS
        shaders/terrain.vert
        shaders/terrain.frag
)
target_link_libraries(
    cubey_project_terrain
    PRIVATE
        cubey_project_terrain_render_support
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_tests
        terrain_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_tests
        PRIVATE
            cubey_project_terrain_export
            cubey_project_terrain_render_support
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

    set(CUBEY_TERRAIN_HEADLESS_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/terrain-headless.png")
    cubey_add_png_smoke_test(
        terrain_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --grid-size
        65
        --debug-view
        surface
        --terrain-camera-preset
        oblique
    )
    cubey_add_png_stats_test(
        terrain_headless_writes_png
        "${CUBEY_TERRAIN_HEADLESS_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_SURFACE_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-surface-low-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_surface_low_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_SURFACE_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --grid-size
        65
        --debug-view
        surface
        --terrain-camera-preset
        surface-low
    )
    cubey_add_png_stats_test(
        terrain_surface_low_headless_writes_png
        "${CUBEY_TERRAIN_SURFACE_HEADLESS_OUTPUT}"
        0.00001
        0.00005
    )

    cubey_label_tests(
        terrain
        terrain_reaches_glfw
        terrain_tests
        terrain_generate_exports_fields
        terrain_generate_exports_manifest
        terrain_headless_writes_png
        terrain_headless_writes_png_stats
        terrain_surface_low_headless_writes_png
        terrain_surface_low_headless_writes_png_stats
    )
endif()
