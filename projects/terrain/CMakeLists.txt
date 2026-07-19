add_library(cubey_project_terrain_runtime STATIC
    terrain_backdrop_density.cpp
    terrain_config.cpp
    terrain_height_source.cpp
    terrain_mountain_backdrop_source.cpp
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
    terrain_backdrop_camera.cpp
    terrain_backdrop_material.cpp
    terrain_backdrop_profile.cpp
    terrain_backdrop_product.cpp
    terrain_backdrop_stage.cpp
    terrain_clipmap.cpp
    terrain_directional_placement.cpp
    terrain_directional_relief.cpp
    terrain_radial_relief.cpp
    terrain_environment_gpu.cpp
    terrain_material_tiles.cpp
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
        cubey::input
        cubey::render
    PRIVATE
        cubey_project_warnings
)

add_library(cubey_project_terrain_source_study_runtime STATIC
    source_study/terrain_raster_height_source.cpp
    source_study/terrain_source_study.cpp
)
target_include_directories(
    cubey_project_terrain_source_study_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source_study"
)
target_link_libraries(
    cubey_project_terrain_source_study_runtime
    PUBLIC
        cubey_project_terrain_runtime
    PRIVATE
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

add_executable(cubey_project_terrain_backdrop_report
    terrain_backdrop_report.cpp
)
set_target_properties(
    cubey_project_terrain_backdrop_report
    PROPERTIES
        OUTPUT_NAME terrain_backdrop_report
)
target_link_libraries(
    cubey_project_terrain_backdrop_report
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

add_executable(cubey_project_terrain_backdrop_stage_report
    terrain_backdrop_stage_report.cpp
)
set_target_properties(
    cubey_project_terrain_backdrop_stage_report
    PROPERTIES
        OUTPUT_NAME terrain_backdrop_stage_report
)
target_link_libraries(
    cubey_project_terrain_backdrop_stage_report
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_warnings
        nlohmann_json::nlohmann_json
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
    SHADER_DEFINES
        CUBEY_TERRAIN_SOURCE_VARIANT=0
    SHADERS
        shaders/terrain.vert
        shaders/terrain_backdrop.vert
        shaders/terrain_backdrop.frag
        shaders/terrain_backdrop_detail.frag
        shaders/terrain_backdrop_material.comp
        shaders/terrain_quality.vert
        shaders/terrain_quality.tesc
        shaders/terrain_quality.tese
        shaders/terrain_material_tile.comp
        shaders/terrain_layered_material_tile.comp
        shaders/terrain.frag
        shaders/terrain_quality.frag
        shaders/terrain_layered.frag
        shaders/terrain_stage_proxy.vert
        shaders/terrain_stage_proxy.frag
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        shaders/terrain_environment.glsl
        shaders/terrain_lighting.glsl
        shaders/terrain_material.glsl
        shaders/terrain_fragment.glsl
        shaders/terrain_layered_material.glsl
        shaders/terrain_quality_material.glsl
        shaders/terrain_vegetation.glsl
        shaders/terrain_shadow.glsl
        shaders/terrain_source.glsl
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
        ${CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS}
        ${CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS}
)
set(CUBEY_TERRAIN_SOURCE_SHADER_SOURCES
    shaders/terrain.vert
    shaders/terrain_quality.tesc
    shaders/terrain_quality.tese
    shaders/terrain.frag
    shaders/terrain_quality.frag
    shaders/terrain_layered.frag
)
add_custom_target(cubey_project_terrain_v2_1_shader_bundle)
cubey_add_glsl_shaders(
    cubey_project_terrain_v2_1_shader_bundle
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/v2-1"
    DEFINES CUBEY_TERRAIN_SOURCE_VARIANT=3
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    DEPENDS
        shaders/terrain_environment.glsl
        shaders/terrain_lighting.glsl
        shaders/terrain_material.glsl
        shaders/terrain_fragment.glsl
        shaders/terrain_layered_material.glsl
        shaders/terrain_quality_material.glsl
        shaders/terrain_vegetation.glsl
        shaders/terrain_shadow.glsl
        shaders/terrain_source.glsl
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
        ${CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS}
        ${CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS}
    SOURCES ${CUBEY_TERRAIN_SOURCE_SHADER_SOURCES}
)
add_custom_target(cubey_project_terrain_v3_shader_bundle)
cubey_add_glsl_shaders(
    cubey_project_terrain_v3_shader_bundle
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/v3"
    DEFINES CUBEY_TERRAIN_SOURCE_VARIANT=1
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    DEPENDS
        shaders/terrain_environment.glsl
        shaders/terrain_lighting.glsl
        shaders/terrain_material.glsl
        shaders/terrain_fragment.glsl
        shaders/terrain_layered_material.glsl
        shaders/terrain_quality_material.glsl
        shaders/terrain_vegetation.glsl
        shaders/terrain_shadow.glsl
        shaders/terrain_source.glsl
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
        ${CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS}
        ${CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS}
    SOURCES ${CUBEY_TERRAIN_SOURCE_SHADER_SOURCES}
)
add_dependencies(
    cubey_project_terrain
    cubey_project_terrain_v2_1_shader_bundle
    cubey_project_terrain_v3_shader_bundle
)
target_link_libraries(
    cubey_project_terrain
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_terrain_runtime
)

cubey_add_app_target(
    cubey_project_terrain_source_study
    OUTPUT_NAME terrain_source_study
    SHADER_DEFINE CUBEY_TERRAIN_SHADER_DIR
    SOURCES
        source_study/main.cpp
        terrain_app.cpp
)
target_link_libraries(
    cubey_project_terrain_source_study
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_terrain_runtime
        cubey_project_terrain_source_study_runtime
)
add_dependencies(cubey_project_terrain_source_study cubey_project_terrain)

cubey_add_app_target(
    cubey_project_terrain_external_source_study
    OUTPUT_NAME terrain_external_source_study
    SHADER_DEFINE CUBEY_TERRAIN_SHADER_DIR
    SOURCES
        source_study/external_source_main.cpp
        terrain_app.cpp
)
target_link_libraries(
    cubey_project_terrain_external_source_study
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_terrain_runtime
        cubey_project_terrain_source_study_runtime
)
add_dependencies(cubey_project_terrain_external_source_study cubey_project_terrain)

cubey_add_app_target(
    cubey_project_terrain_directional_backdrop_study
    OUTPUT_NAME terrain_directional_backdrop_study
    SHADER_DEFINE CUBEY_TERRAIN_SHADER_DIR
    SOURCES
        source_study/directional_backdrop_main.cpp
        source_study/terrain_directional_backdrop_study.cpp
        terrain_app.cpp
)
target_link_libraries(
    cubey_project_terrain_directional_backdrop_study
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_terrain_runtime
        cubey_project_terrain_source_study_runtime
)
add_dependencies(cubey_project_terrain_directional_backdrop_study cubey_project_terrain)

add_executable(cubey_project_terrain_directional_backdrop_report
    source_study/terrain_directional_backdrop_report.cpp
    source_study/terrain_directional_backdrop_study.cpp
)
set_target_properties(
    cubey_project_terrain_directional_backdrop_report
    PROPERTIES OUTPUT_NAME terrain_directional_backdrop_report
)
target_link_libraries(
    cubey_project_terrain_directional_backdrop_report
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_terrain_source_study_runtime
        cubey::engine
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

add_executable(cubey_project_terrain_source_study_report
    source_study/terrain_source_study_report.cpp
)
set_target_properties(
    cubey_project_terrain_source_study_report
    PROPERTIES
        OUTPUT_NAME terrain_source_study_report
)
target_link_libraries(
    cubey_project_terrain_source_study_report
    PRIVATE
        cubey_project_terrain_render_support
        cubey_project_terrain_source_study_runtime
        cubey::engine
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_raster_height_source_tests
        source_study/terrain_raster_height_source_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_raster_height_source_tests
        PRIVATE
            cubey_project_terrain_source_study_runtime
            cubey_project_warnings
            nlohmann_json::nlohmann_json
    )
    add_test(
        NAME terrain_raster_height_source_tests
        COMMAND cubey_project_terrain_raster_height_source_tests
    )
    set_tests_properties(terrain_raster_height_source_tests PROPERTIES LABELS terrain TIMEOUT 120)

    add_executable(cubey_project_terrain_source_study_tests
        source_study/terrain_source_study_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_source_study_tests
        PRIVATE
            cubey_project_terrain_source_study_runtime
            cubey_project_warnings
    )
    add_test(
        NAME terrain_source_study_tests
        COMMAND cubey_project_terrain_source_study_tests
    )
    set_tests_properties(terrain_source_study_tests PROPERTIES LABELS terrain TIMEOUT 120)

    add_executable(cubey_project_terrain_backdrop_product_tests
        terrain_backdrop_product_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_backdrop_product_tests
        PRIVATE
            cubey_project_terrain_render_support
            cubey_project_warnings
    )
    add_test(
        NAME terrain_backdrop_product_tests
        COMMAND cubey_project_terrain_backdrop_product_tests
    )
    set_tests_properties(terrain_backdrop_product_tests PROPERTIES LABELS terrain TIMEOUT 120)

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

    add_executable(cubey_project_terrain_backdrop_stage_tests
        terrain_backdrop_stage_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_backdrop_stage_tests
        PRIVATE
            cubey_project_terrain_render_support
            cubey_project_warnings
    )
    add_test(NAME terrain_backdrop_stage_tests COMMAND cubey_project_terrain_backdrop_stage_tests)

    add_executable(cubey_project_terrain_directional_placement_tests
        terrain_directional_placement_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_directional_placement_tests
        PRIVATE
            cubey_project_terrain_render_support
            cubey_project_warnings
    )
    add_test(
        NAME terrain_directional_placement_tests
        COMMAND cubey_project_terrain_directional_placement_tests
    )
    set_tests_properties(
        terrain_directional_placement_tests
        PROPERTIES LABELS terrain TIMEOUT 120
    )

    add_executable(cubey_project_terrain_directional_relief_tests
        terrain_directional_relief_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_directional_relief_tests
        PRIVATE
            cubey_project_terrain_render_support
            cubey_project_warnings
    )
    add_test(
        NAME terrain_directional_relief_tests
        COMMAND cubey_project_terrain_directional_relief_tests
    )
    set_tests_properties(
        terrain_directional_relief_tests
        PROPERTIES LABELS terrain TIMEOUT 120
    )

    add_executable(cubey_project_terrain_radial_relief_tests
        terrain_radial_relief_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_radial_relief_tests
        PRIVATE
            cubey_project_terrain_render_support
            cubey_project_warnings
    )
    add_test(
        NAME terrain_radial_relief_tests
        COMMAND cubey_project_terrain_radial_relief_tests
    )
    set_tests_properties(
        terrain_radial_relief_tests
        PROPERTIES LABELS terrain TIMEOUT 120
    )

    add_executable(cubey_project_terrain_directional_backdrop_study_tests
        source_study/terrain_directional_backdrop_study_tests.cpp
        source_study/terrain_directional_backdrop_study.cpp
    )
    target_link_libraries(
        cubey_project_terrain_directional_backdrop_study_tests
        PRIVATE
            cubey_project_terrain_render_support
            cubey_project_warnings
    )
    add_test(
        NAME terrain_directional_backdrop_study_tests
        COMMAND cubey_project_terrain_directional_backdrop_study_tests
    )
    set_tests_properties(
        terrain_directional_backdrop_study_tests
        PROPERTIES LABELS terrain TIMEOUT 120
    )

    add_test(
        NAME terrain_source_report_runs
        COMMAND cubey_project_terrain_source_report
    )
    add_test(
        NAME terrain_source_scale_report_runs
        COMMAND cubey_project_terrain_source_report --source-version v2.1 --scale-response
    )
    add_test(
        NAME terrain_backdrop_report_runs
        COMMAND cubey_project_terrain_backdrop_report
    )
    add_test(
        NAME terrain_backdrop_stage_report_runs
        COMMAND cubey_project_terrain_backdrop_stage_report
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
        DEFINES CUBEY_TERRAIN_SOURCE_VARIANT=2
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_source_parity.comp"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_source.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/fastnoise_lite.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
            "${CUBEY_FASTNOISE_LITE_GLSL_DIR}/FastNoiseLite.glsl"
    )
    add_test(
        NAME terrain_source_gpu_parity_tests
        COMMAND cubey_project_terrain_source_gpu_parity_tests
    )
    set_tests_properties(
        terrain_source_gpu_parity_tests
        PROPERTIES
            TIMEOUT 60
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

    set(CUBEY_TERRAIN_BACKDROP_STAGE_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-backdrop-stage-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_backdrop_stage_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_BACKDROP_STAGE_HEADLESS_OUTPUT}"
        --headless
        --terrain-seed
        9012
        --terrain-backdrop-profile
        hard-cut-v1
        --terrain-preset
        mountain
        --terrain-weathering
        off
        --terrain-camera-preset
        backdrop-stage
        --terrain-backdrop-min-distance
        3200
        --terrain-presentation
        backdrop
    )
    set_tests_properties(terrain_backdrop_stage_headless_writes_png PROPERTIES TIMEOUT 60)
    cubey_add_png_stats_test(
        terrain_backdrop_stage_headless_writes_png
        "${CUBEY_TERRAIN_BACKDROP_STAGE_HEADLESS_OUTPUT}"
        0.001
        0.001
    )

    set(CUBEY_TERRAIN_RADIAL_BACKDROP_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-radial-backdrop-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_radial_backdrop_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_RADIAL_BACKDROP_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --terrain-seed
        9012
        --terrain-backdrop-profile
        radial-v1
        --terrain-camera-preset
        backdrop-stage
    )
    set_tests_properties(terrain_radial_backdrop_headless_writes_png PROPERTIES TIMEOUT 120)
    cubey_add_png_stats_test(
        terrain_radial_backdrop_headless_writes_png
        "${CUBEY_TERRAIN_RADIAL_BACKDROP_HEADLESS_OUTPUT}"
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

    set(CUBEY_TERRAIN_GROUND_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-ground-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_ground_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_GROUND_HEADLESS_OUTPUT}"
        --headless
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-weathering
        local
        --terrain-camera-preset
        ground
        --debug-view
        surface
    )
    cubey_add_png_stats_test(
        terrain_ground_headless_writes_png
        "${CUBEY_TERRAIN_GROUND_HEADLESS_OUTPUT}"
        0.001
        0.001
    )

    set(CUBEY_TERRAIN_CLAY_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-clay-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_clay_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_CLAY_HEADLESS_OUTPUT}"
        --headless
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-camera-preset
        oblique
        --debug-view
        clay
    )
    cubey_add_png_stats_test(
        terrain_clay_headless_writes_png
        "${CUBEY_TERRAIN_CLAY_HEADLESS_OUTPUT}"
        0.001
        0.001
    )

    set(CUBEY_TERRAIN_SHADOW_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadow-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_shadow_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_SHADOW_HEADLESS_OUTPUT}"
        --headless
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-camera-preset
        oblique
        --debug-view
        shadow
        --sun-elevation
        12
    )
    cubey_add_png_stats_test(
        terrain_shadow_headless_writes_png
        "${CUBEY_TERRAIN_SHADOW_HEADLESS_OUTPUT}"
        0.001
        0.0001
    )

    set(CUBEY_TERRAIN_AERIAL_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-aerial-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_aerial_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_AERIAL_HEADLESS_OUTPUT}"
        --headless
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-camera-preset
        oblique
        --debug-view
        aerial-transmittance
    )
    cubey_add_png_stats_test(
        terrain_aerial_headless_writes_png
        "${CUBEY_TERRAIN_AERIAL_HEADLESS_OUTPUT}"
        0.001
        0.0001
    )

    set(CUBEY_TERRAIN_BACKDROP_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-backdrop-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_backdrop_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_BACKDROP_HEADLESS_OUTPUT}"
        --headless
        --terrain-seed
        9012
        --terrain-backdrop-profile
        hard-cut-v1
        --terrain-preset
        mountain
        --terrain-source-version
        v2.1
        --terrain-weathering
        off
        --terrain-camera-preset
        backdrop
        --terrain-presentation
        backdrop
    )
    cubey_add_png_stats_test(
        terrain_backdrop_headless_writes_png
        "${CUBEY_TERRAIN_BACKDROP_HEADLESS_OUTPUT}"
        0.001
        0.001
    )
    set_tests_properties(terrain_backdrop_headless_writes_png PROPERTIES TIMEOUT 60)

    set(CUBEY_TERRAIN_LAYERED_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-layered-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_layered_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_LAYERED_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --terrain-seed
        9012
        --terrain-preset
        mountain
        --terrain-weathering
        local
        --terrain-render-path
        quality
        --terrain-source-version
        v2
        --terrain-surface-detail
        layered
        --terrain-camera-preset
        midground
        --terrain-presentation
        backdrop
        --validation
    )
    cubey_add_png_stats_test(
        terrain_layered_headless_writes_png
        "${CUBEY_TERRAIN_LAYERED_HEADLESS_OUTPUT}"
        0.001
        0.001
    )
    set_tests_properties(terrain_layered_headless_writes_png PROPERTIES TIMEOUT 30)

    set(CUBEY_TERRAIN_VEGETATION_COVERAGE_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-vegetation-coverage-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_vegetation_coverage_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_VEGETATION_COVERAGE_HEADLESS_OUTPUT}"
        --headless
        --terrain-seed
        9012
        --terrain-backdrop-profile
        hard-cut-v1
        --terrain-preset
        mountain
        --terrain-source-version
        v2.1
        --terrain-weathering
        off
        --terrain-camera-preset
        backdrop
        --terrain-presentation
        backdrop
        --debug-view
        vegetation-coverage
    )
    cubey_add_png_stats_test(
        terrain_vegetation_coverage_headless_writes_png
        "${CUBEY_TERRAIN_VEGETATION_COVERAGE_HEADLESS_OUTPUT}"
        0.001
        0.001
    )
    set_tests_properties(terrain_vegetation_coverage_headless_writes_png PROPERTIES TIMEOUT 60)

    foreach(diagnostic IN ITEMS normal classification-normal material-weights ambient-visibility)
        string(REPLACE "-" "_" diagnostic_target "${diagnostic}")
        set(diagnostic_output
            "${CMAKE_CURRENT_BINARY_DIR}/terrain-${diagnostic}-headless.png"
        )
        cubey_add_png_smoke_test(
            terrain_${diagnostic_target}_headless_writes_png
            cubey_project_terrain
            "${diagnostic_output}"
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
            --debug-view
            ${diagnostic}
        )
        cubey_add_png_stats_test(
            terrain_${diagnostic_target}_headless_writes_png
            "${diagnostic_output}"
            0.001
            0.0001
        )
    endforeach()

    if (CUBEY_HAS_LIBAV)
        set(CUBEY_TERRAIN_SURFACE_HEADLESS_VIDEO_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/terrain-surface-headless.mp4"
        )
        cubey_add_video_smoke_test(
            terrain_surface_headless_writes_video
            cubey_project_terrain
            "${CUBEY_TERRAIN_SURFACE_HEADLESS_VIDEO_OUTPUT}"
            --terrain-seed
            9012
            --terrain-preset
            mountain
            --terrain-weathering
            local
            --terrain-camera-preset
            surface
            --debug-view
            lod
        )
        set(CUBEY_TERRAIN_GROUND_HEADLESS_VIDEO_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/terrain-ground-headless.mp4"
        )
        cubey_add_video_smoke_test(
            terrain_ground_headless_writes_video
            cubey_project_terrain
            "${CUBEY_TERRAIN_GROUND_HEADLESS_VIDEO_OUTPUT}"
            --terrain-seed
            9012
            --terrain-preset
            mountain
            --terrain-weathering
            local
            --terrain-camera-preset
            ground
            --debug-view
            surface
        )
        set(CUBEY_TERRAIN_BACKDROP_HEADLESS_VIDEO_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/terrain-backdrop-headless.mp4"
        )
        cubey_add_video_smoke_test(
            terrain_backdrop_headless_writes_video
            cubey_project_terrain
            "${CUBEY_TERRAIN_BACKDROP_HEADLESS_VIDEO_OUTPUT}"
            --terrain-seed
            9012
            --terrain-preset
            mountain
            --terrain-weathering
            local
            --terrain-camera-preset
            surface
            --terrain-presentation
            backdrop
        )
        cubey_label_tests(
            terrain
            terrain_surface_headless_writes_video
            terrain_ground_headless_writes_video
            terrain_backdrop_headless_writes_video
        )
    endif()

    cubey_label_tests(
        terrain
        terrain_reaches_glfw
        terrain_source_tests
        terrain_render_tests
        terrain_backdrop_stage_tests
        terrain_source_report_runs
        terrain_source_scale_report_runs
        terrain_backdrop_report_runs
        terrain_backdrop_stage_report_runs
        terrain_source_gpu_parity_tests
        terrain_headless_writes_png
        terrain_headless_writes_png_stats
        terrain_backdrop_stage_headless_writes_png
        terrain_backdrop_stage_headless_writes_png_stats
        terrain_radial_backdrop_headless_writes_png
        terrain_radial_backdrop_headless_writes_png_stats
        terrain_surface_headless_writes_png
        terrain_surface_headless_writes_png_stats
        terrain_lod_headless_writes_png
        terrain_lod_headless_writes_png_stats
        terrain_ground_headless_writes_png
        terrain_ground_headless_writes_png_stats
        terrain_clay_headless_writes_png
        terrain_clay_headless_writes_png_stats
        terrain_shadow_headless_writes_png
        terrain_shadow_headless_writes_png_stats
        terrain_aerial_headless_writes_png
        terrain_aerial_headless_writes_png_stats
        terrain_backdrop_headless_writes_png
        terrain_backdrop_headless_writes_png_stats
        terrain_layered_headless_writes_png
        terrain_layered_headless_writes_png_stats
        terrain_vegetation_coverage_headless_writes_png
        terrain_vegetation_coverage_headless_writes_png_stats
    )
endif()
