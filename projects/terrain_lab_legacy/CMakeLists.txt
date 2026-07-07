cubey_add_app_target(
    cubey_project_terrain_lab_legacy
    OUTPUT_NAME terrain_lab_legacy
    SHADER_DEFINE CUBEY_TERRAIN_LAB_SHADER_DIR
    WINDOWED_SMOKE_TEST terrain_lab_legacy_reaches_glfw
    WINDOWED_SMOKE_PATTERN "terrain_lab_legacy: .*rendering legacy terrain lab"
    SOURCES
        main.cpp
        terrain_lab_app.cpp
        terrain_lab_fields.cpp
        terrain_lab_mesh.cpp
    SHADERS
        shaders/terrain_lab.vert
        shaders/terrain_lab.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/random.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/operators.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_lab_legacy_tests
        terrain_lab_config_tests.cpp
        terrain_lab_fields.cpp
        terrain_lab_mesh.cpp
    )
    target_include_directories(
        cubey_project_terrain_lab_legacy_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(
        cubey_project_terrain_lab_legacy_tests
        PRIVATE
            CUBEY_TERRAIN_LAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_terrain_lab_legacy_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME terrain_lab_legacy_config_tests COMMAND cubey_project_terrain_lab_legacy_tests)

    set(CUBEY_TERRAIN_LAB_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_headless_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_headless_writes_png
        "${CUBEY_TERRAIN_LAB_HEADLESS_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_FLOW_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-flow-accumulation.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_flow_accumulation_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_FLOW_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        flow-accumulation
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_flow_accumulation_writes_png
        "${CUBEY_TERRAIN_LAB_FLOW_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_RIVER_NETWORK_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-river-network.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_river_network_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_RIVER_NETWORK_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        river-network
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_river_network_writes_png
        "${CUBEY_TERRAIN_LAB_RIVER_NETWORK_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_RIVER_WIDTH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-river-width.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_river_width_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_RIVER_WIDTH_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        river-width
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_river_width_writes_png
        "${CUBEY_TERRAIN_LAB_RIVER_WIDTH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_WATER_PRESENCE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-water-presence.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_water_presence_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_WATER_PRESENCE_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        water-presence
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_water_presence_writes_png
        "${CUBEY_TERRAIN_LAB_WATER_PRESENCE_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_FEATURE_GRAPH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-feature-graph.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_feature_graph_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_FEATURE_GRAPH_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --debug-view
        feature-graph
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_feature_graph_writes_png
        "${CUBEY_TERRAIN_LAB_FEATURE_GRAPH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_DRAINAGE_REGIONS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-drainage-regions.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_drainage_regions_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_DRAINAGE_REGIONS_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --terrain-lab-slice
        temperate-mountain-rivers
        --debug-view
        drainage-regions
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_drainage_regions_writes_png
        "${CUBEY_TERRAIN_LAB_DRAINAGE_REGIONS_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_DUNES_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-desert-dunes.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_desert_dunes_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_DUNES_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --terrain-lab-slice
        desert-dunes
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_desert_dunes_writes_png
        "${CUBEY_TERRAIN_LAB_DUNES_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_FASTNOISE_DUNES_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-desert-dunes-fastnoise.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_desert_dunes_fastnoise_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_FASTNOISE_DUNES_SMOKE_OUTPUT}"
        --headless
        --grid-width
        65
        --grid-height
        65
        --terrain-lab-slice
        desert-dunes
        --terrain-lab-noise-source
        fastnoise-lite
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_desert_dunes_fastnoise_writes_png
        "${CUBEY_TERRAIN_LAB_FASTNOISE_DUNES_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_FASTNOISE_WARPED_DUNES_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-desert-dunes-fastnoise-warped.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_desert_dunes_fastnoise_warped_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_FASTNOISE_WARPED_DUNES_SMOKE_OUTPUT}"
        --headless
        --grid-width
        65
        --grid-height
        65
        --terrain-lab-slice
        desert-dunes
        --terrain-lab-noise-source
        fastnoise-lite-warped
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_desert_dunes_fastnoise_warped_writes_png
        "${CUBEY_TERRAIN_LAB_FASTNOISE_WARPED_DUNES_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_GLACIAL_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-alpine-glacial-valley.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_alpine_glacial_valley_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_GLACIAL_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --terrain-lab-slice
        alpine-glacial-valley
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_alpine_glacial_valley_writes_png
        "${CUBEY_TERRAIN_LAB_GLACIAL_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_LAB_MOUNTAIN_RIDGES_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lab-legacy-mountain-ridges-peaks.png"
    )
    cubey_add_png_smoke_test(
        terrain_lab_legacy_mountain_ridges_peaks_writes_png
        cubey_project_terrain_lab_legacy
        "${CUBEY_TERRAIN_LAB_MOUNTAIN_RIDGES_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
        --terrain-lab-slice
        mountain-ridges-peaks
    )
    cubey_add_png_stats_test(
        terrain_lab_legacy_mountain_ridges_peaks_writes_png
        "${CUBEY_TERRAIN_LAB_MOUNTAIN_RIDGES_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    cubey_label_tests(
        legacy
        terrain_lab_legacy_reaches_glfw
        terrain_lab_legacy_config_tests
        terrain_lab_legacy_headless_writes_png
        terrain_lab_legacy_headless_writes_png_stats
        terrain_lab_legacy_flow_accumulation_writes_png
        terrain_lab_legacy_flow_accumulation_writes_png_stats
        terrain_lab_legacy_river_network_writes_png
        terrain_lab_legacy_river_network_writes_png_stats
        terrain_lab_legacy_river_width_writes_png
        terrain_lab_legacy_river_width_writes_png_stats
        terrain_lab_legacy_water_presence_writes_png
        terrain_lab_legacy_water_presence_writes_png_stats
        terrain_lab_legacy_feature_graph_writes_png
        terrain_lab_legacy_feature_graph_writes_png_stats
        terrain_lab_legacy_drainage_regions_writes_png
        terrain_lab_legacy_drainage_regions_writes_png_stats
        terrain_lab_legacy_desert_dunes_writes_png
        terrain_lab_legacy_desert_dunes_writes_png_stats
        terrain_lab_legacy_desert_dunes_fastnoise_writes_png
        terrain_lab_legacy_desert_dunes_fastnoise_writes_png_stats
        terrain_lab_legacy_desert_dunes_fastnoise_warped_writes_png
        terrain_lab_legacy_desert_dunes_fastnoise_warped_writes_png_stats
        terrain_lab_legacy_alpine_glacial_valley_writes_png
        terrain_lab_legacy_alpine_glacial_valley_writes_png_stats
        terrain_lab_legacy_mountain_ridges_peaks_writes_png
        terrain_lab_legacy_mountain_ridges_peaks_writes_png_stats
    )
endif()
