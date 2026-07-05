add_executable(cubey_project_terrain
    main.cpp
    terrain_debug_export.cpp
    terrain_engine_reference.cpp
    terrain_generator.cpp
    terrain_phase_profile.cpp
    terrain_process_fields.cpp
    terrain_product.cpp
)
set_target_properties(cubey_project_terrain PROPERTIES OUTPUT_NAME terrain)
target_include_directories(cubey_project_terrain PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    cubey_project_terrain
    PRIVATE
        cubey::core
        cubey::engine
        nlohmann_json::nlohmann_json
        cubey_project_warnings
)

cubey_add_app_target(
    cubey_project_terrain_preview
    OUTPUT_NAME terrain_preview
    SHADER_DEFINE CUBEY_TERRAIN_PREVIEW_SHADER_DIR
    WINDOWED_SMOKE_TEST terrain_preview_reaches_glfw
    WINDOWED_SMOKE_PATTERN "terrain_preview: .*rendering terrain preview"
    WINDOWED_SMOKE_ARGS
        --grid-size
        65
    SOURCES
        terrain_preview_main.cpp
        terrain_preview_app.cpp
        terrain_preview_config.cpp
        terrain_engine_reference.cpp
        terrain_preview_mesh.cpp
        terrain_generator.cpp
        terrain_phase_profile.cpp
        terrain_process_fields.cpp
        terrain_product.cpp
    SHADERS
        shaders/terrain_preview.vert
        shaders/terrain_preview.frag
)
target_link_libraries(cubey_project_terrain_preview PRIVATE nlohmann_json::nlohmann_json)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_tests
        terrain_debug_export.cpp
        terrain_engine_reference.cpp
        terrain_generator.cpp
        terrain_preview_config.cpp
        terrain_preview_mesh.cpp
        terrain_phase_profile.cpp
        terrain_process_fields.cpp
        terrain_product.cpp
        terrain_tests.cpp
    )
    target_include_directories(cubey_project_terrain_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_project_terrain_tests
        PRIVATE
            cubey::core
            cubey::engine
            cubey::render
            nlohmann_json::nlohmann_json
            cubey_project_warnings
    )
    add_test(NAME terrain_product_tests COMMAND cubey_project_terrain_tests)

    set(CUBEY_TERRAIN_HEADLESS_FINAL_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-headless-final.png"
    )
    cubey_add_png_smoke_test(
        terrain_headless_final_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_HEADLESS_FINAL_OUTPUT}"
        --headless
        --terrain-debug-view
        final
    )
    cubey_add_png_stats_test(
        terrain_headless_final_writes_png
        "${CUBEY_TERRAIN_HEADLESS_FINAL_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_HEADLESS_FLOW_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-headless-flow-accumulation.png"
    )
    cubey_add_png_smoke_test(
        terrain_headless_flow_accumulation_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_HEADLESS_FLOW_OUTPUT}"
        --headless
        --terrain-debug-view
        flow-accumulation
    )
    cubey_add_png_stats_test(
        terrain_headless_flow_accumulation_writes_png
        "${CUBEY_TERRAIN_HEADLESS_FLOW_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_HEADLESS_STRESS_RIVER_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-headless-stress-river-mask.png"
    )
    cubey_add_png_smoke_test(
        terrain_headless_stress_river_network_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_HEADLESS_STRESS_RIVER_OUTPUT}"
        --headless
        --grid-size
        257
        --recipe
        temperate-mountain-river-stress
        --terrain-debug-view
        river-mask
    )
    cubey_add_png_stats_test(
        terrain_headless_stress_river_network_writes_png
        "${CUBEY_TERRAIN_HEADLESS_STRESS_RIVER_OUTPUT}"
        0.00001
        0.00005
    )

    set(CUBEY_TERRAIN_PREVIEW_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-preview-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_preview_headless_writes_png
        cubey_project_terrain_preview
        "${CUBEY_TERRAIN_PREVIEW_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --grid-size
        65
        --terrain-recipe
        temperate-mountain-range-stress
    )
    cubey_add_png_stats_test(
        terrain_preview_headless_writes_png
        "${CUBEY_TERRAIN_PREVIEW_HEADLESS_OUTPUT}"
        0.00001
        0.00005
    )
endif()
