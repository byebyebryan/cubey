cubey_add_app_target(
    cubey_project_terrain_lab
    OUTPUT_NAME terrain_lab
    SHADER_DEFINE CUBEY_TERRAIN_LAB_SHADER_DIR
    WINDOWED_SMOKE_TEST terrain_lab_reaches_glfw
    WINDOWED_SMOKE_PATTERN "terrain_lab: .*rendering terrain lab"
    SOURCES
        main.cpp
        terrain_lab_app.cpp
        terrain_lab_fields.cpp
        terrain_lab_mesh.cpp
    SHADERS
        shaders/terrain_lab.vert
        shaders/terrain_lab.frag
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_lab_tests
        terrain_lab_config_tests.cpp
        terrain_lab_fields.cpp
        terrain_lab_mesh.cpp
    )
    target_include_directories(
        cubey_project_terrain_lab_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(
        cubey_project_terrain_lab_tests
        PRIVATE
            CUBEY_TERRAIN_LAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_terrain_lab_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME terrain_lab_config_tests COMMAND cubey_project_terrain_lab_tests)

    set(CUBEY_TERRAIN_LAB_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_headless_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
    )
    cubey_add_png_stats_test(
        terrain_lab_headless_writes_png
        "${CUBEY_TERRAIN_LAB_HEADLESS_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_FLOW_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-flow-accumulation.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_flow_accumulation_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_FLOW_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        flow-accumulation
    )
    cubey_add_png_stats_test(
        terrain_lab_flow_accumulation_writes_png
        "${CUBEY_TERRAIN_LAB_FLOW_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_RIVER_NETWORK_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-river-network.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_river_network_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_RIVER_NETWORK_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        river-network
    )
    cubey_add_png_stats_test(
        terrain_lab_river_network_writes_png
        "${CUBEY_TERRAIN_LAB_RIVER_NETWORK_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_RIVER_WIDTH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-river-width.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_river_width_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_RIVER_WIDTH_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        river-width
    )
    cubey_add_png_stats_test(
        terrain_lab_river_width_writes_png
        "${CUBEY_TERRAIN_LAB_RIVER_WIDTH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_WATER_PRESENCE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-water-presence.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_water_presence_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_WATER_PRESENCE_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        water-presence
    )
    cubey_add_png_stats_test(
        terrain_lab_water_presence_writes_png
        "${CUBEY_TERRAIN_LAB_WATER_PRESENCE_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_FEATURE_GRAPH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-feature-graph.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_feature_graph_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_FEATURE_GRAPH_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        feature-graph
    )
    cubey_add_png_stats_test(
        terrain_lab_feature_graph_writes_png
        "${CUBEY_TERRAIN_LAB_FEATURE_GRAPH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_WATERSHED_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-watershed.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_watershed_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_WATERSHED_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --terrain-lab-slice
        temperate-mountain-watershed
        --debug-view
        watershed
    )
    cubey_add_png_stats_test(
        terrain_lab_watershed_writes_png
        "${CUBEY_TERRAIN_LAB_WATERSHED_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_DUNES_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-desert-dunes.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_desert_dunes_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_DUNES_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --terrain-lab-slice
        desert-dunes
    )
    cubey_add_png_stats_test(
        terrain_lab_desert_dunes_writes_png
        "${CUBEY_TERRAIN_LAB_DUNES_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_GLACIAL_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-alpine-glacial-valley.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_alpine_glacial_valley_writes_png
        cubey_project_terrain_lab
        "${CUBEY_TERRAIN_LAB_GLACIAL_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --terrain-lab-slice
        alpine-glacial-valley
    )
    cubey_add_png_stats_test(
        terrain_lab_alpine_glacial_valley_writes_png
        "${CUBEY_TERRAIN_LAB_GLACIAL_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )
endif()
