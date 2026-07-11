add_library(cubey_project_terrain_runtime STATIC
    terrain_config.cpp
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

add_executable(cubey_project_terrain_source_report
    terrain_source_report.cpp
)
set_target_properties(
    cubey_project_terrain_source_report
    PROPERTIES
        OUTPUT_NAME terrain_source_report
)
target_link_libraries(
    cubey_project_terrain_source_report
    PRIVATE
        cubey_project_terrain_runtime
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

add_library(cubey_project_terrain_render_support STATIC
    terrain_clipmap.cpp
    terrain_surface_controller.cpp
)
target_include_directories(
    cubey_project_terrain_render_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_render_support
    PUBLIC
        cubey_project_terrain_runtime
        cubey::render
    PRIVATE
        cubey_project_warnings
)

cubey_atmosphere_shader_depends(CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS)
cubey_forward_pbr_shader_depends(CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS)

cubey_add_app_target(
    cubey_project_terrain
    OUTPUT_NAME terrain
    SHADER_DEFINE CUBEY_TERRAIN_SHADER_DIR
    WINDOWED_SMOKE_TEST terrain_reaches_glfw
    WINDOWED_SMOKE_PATTERN "terrain: .*rendering terrain v1"
    SOURCES
        main.cpp
        terrain_app.cpp
    SHADERS
        shaders/terrain.vert
        shaders/terrain.frag
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        shaders/terrain_source.glsl
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
        ${CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS}
        ${CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS}
)
target_link_libraries(
    cubey_project_terrain
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_terrain_runtime
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

    add_executable(cubey_project_terrain_render_tests
        terrain_render_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_render_tests
        PRIVATE
            cubey_project_terrain_render_support
            cubey_project_warnings
    )
    add_test(NAME terrain_render_tests COMMAND cubey_project_terrain_render_tests)

    add_test(
        NAME terrain_source_report_runs
        COMMAND cubey_project_terrain_source_report
    )

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

    set(CUBEY_TERRAIN_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-camera-preset
        oblique
    )
    cubey_add_png_stats_test(
        terrain_headless_writes_png
        "${CUBEY_TERRAIN_HEADLESS_OUTPUT}"
        0.001
        0.001
    )

    set(CUBEY_TERRAIN_SURFACE_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-surface-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_surface_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_SURFACE_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-camera-preset
        surface
    )
    cubey_add_png_stats_test(
        terrain_surface_headless_writes_png
        "${CUBEY_TERRAIN_SURFACE_HEADLESS_OUTPUT}"
        0.001
        0.001
    )

    set(CUBEY_TERRAIN_LOD_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-lod-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_lod_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_LOD_HEADLESS_OUTPUT}"
        --headless
        --width
        256
        --height
        256
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-weathering
        off
        --terrain-camera-preset
        top
        --debug-view
        lod
    )
    cubey_add_png_stats_test(
        terrain_lod_headless_writes_png
        "${CUBEY_TERRAIN_LOD_HEADLESS_OUTPUT}"
        0.001
        0.001
    )

    cubey_label_tests(
        terrain
        terrain_reaches_glfw
        terrain_source_tests
        terrain_render_tests
        terrain_source_report_runs
        terrain_source_gpu_parity_tests
        terrain_headless_writes_png
        terrain_headless_writes_png_stats
        terrain_surface_headless_writes_png
        terrain_surface_headless_writes_png_stats
        terrain_lod_headless_writes_png
        terrain_lod_headless_writes_png_stats
    )
endif()
