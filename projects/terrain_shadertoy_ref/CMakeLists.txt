set(
    CUBEY_SHADERTOY_REF_DIR
    "${CMAKE_SOURCE_DIR}/../ref/ShaderToy"
    CACHE PATH
    "Directory containing external ShaderToy reference sources"
)

set(CUBEY_SHADERTOY_MOUNTAINS_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/mountains.glsl")
set(CUBEY_SHADERTOY_MOUNTAIN_PEAK_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/mountain_peak.glsl")
set(CUBEY_SHADERTOY_SWISS_COMMON_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/swiss_alps_common.glsl")
set(CUBEY_SHADERTOY_SWISS_TERRAIN_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/swiss_alps_buffer_b.glsl")
set(CUBEY_SHADERTOY_EROSION_COMMON_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/eroded_mountains_common.glsl")
set(CUBEY_SHADERTOY_EROSION_FILTER_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/eroded_mountains_buffer_b.glsl")
set(
    CUBEY_SHADERTOY_STUDY_SOURCES
    "${CUBEY_SHADERTOY_MOUNTAINS_SOURCE}"
    "${CUBEY_SHADERTOY_MOUNTAIN_PEAK_SOURCE}"
    "${CUBEY_SHADERTOY_SWISS_COMMON_SOURCE}"
    "${CUBEY_SHADERTOY_SWISS_TERRAIN_SOURCE}"
    "${CUBEY_SHADERTOY_EROSION_COMMON_SOURCE}"
    "${CUBEY_SHADERTOY_EROSION_FILTER_SOURCE}"
)
foreach(CUBEY_SHADERTOY_STUDY_SOURCE IN LISTS CUBEY_SHADERTOY_STUDY_SOURCES)
    if (NOT EXISTS "${CUBEY_SHADERTOY_STUDY_SOURCE}")
        message(STATUS "Skipping terrain_shadertoy_ref: ${CUBEY_SHADERTOY_STUDY_SOURCE} was not found")
        return()
    endif()
endforeach()

cubey_add_app_target(
    cubey_project_terrain_shadertoy_ref
    OUTPUT_NAME terrain_shadertoy_ref
    SHADER_DEFINE CUBEY_TERRAIN_SHADERTOY_REF_SHADER_DIR
    SOURCES
        main.cpp
        terrain_shadertoy_ref_app.cpp
        terrain_shadertoy_ref_camera.cpp
        terrain_shadertoy_ref_config.cpp
        terrain_shadertoy_ref_mesh.cpp
    SHADERS
        shaders/fullscreen.vert
        shaders/erosion_height_bake.frag
        shaders/erosion_input_bake.frag
        shaders/mountain_peak_height_bake.frag
        shaders/mountains_camera_probe.frag
        shaders/mountains_atlas_diagnostic.frag
        shaders/mountains_height_bake.frag
        shaders/mountains_mesh.frag
        shaders/mountains_mesh.vert
        shaders/mountains_raymarch.frag
        shaders/mountains_sky.frag
        shaders/swiss_alps_height_bake.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CUBEY_SHADERTOY_REF_DIR}"
    SHADER_DEPENDS
        ${CUBEY_SHADERTOY_STUDY_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/reference_frame.glsl"
)

if (BUILD_TESTING)
    add_executable(
        cubey_project_terrain_shadertoy_ref_tests
        terrain_shadertoy_ref_camera.cpp
        terrain_shadertoy_ref_config.cpp
        terrain_shadertoy_ref_tests.cpp
    )
    target_include_directories(
        cubey_project_terrain_shadertoy_ref_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_terrain_shadertoy_ref_tests
        PRIVATE
            cubey::core
            cubey_project_warnings
    )
    add_test(
        NAME terrain_shadertoy_ref_tests
        COMMAND cubey_project_terrain_shadertoy_ref_tests
    )

    set(
        CUBEY_TERRAIN_SHADERTOY_REF_RAYMARCH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-raymarch-smoke.png"
    )
    cubey_add_png_smoke_test(
        terrain_shadertoy_ref_raymarch_writes_png
        cubey_project_terrain_shadertoy_ref
        "${CUBEY_TERRAIN_SHADERTOY_REF_RAYMARCH_SMOKE_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --reference-render
        raymarch
        --reference-time
        20
    )
    cubey_add_png_stats_test(
        terrain_shadertoy_ref_raymarch_writes_png
        "${CUBEY_TERRAIN_SHADERTOY_REF_RAYMARCH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(
        CUBEY_TERRAIN_SHADERTOY_REF_MESH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-mesh-smoke.png"
    )
    cubey_add_png_smoke_test(
        terrain_shadertoy_ref_mesh_writes_png
        cubey_project_terrain_shadertoy_ref
        "${CUBEY_TERRAIN_SHADERTOY_REF_MESH_SMOKE_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --reference-render
        mesh
        --reference-time
        20
        --reference-mesh-cells
        256
        --reference-yaw-offset-deg
        90
        --reference-normal
        atlas
    )
    cubey_add_png_stats_test(
        terrain_shadertoy_ref_mesh_writes_png
        "${CUBEY_TERRAIN_SHADERTOY_REF_MESH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(
        CUBEY_TERRAIN_SHADERTOY_REF_HEIGHT_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-height-smoke.png"
    )
    cubey_add_png_smoke_test(
        terrain_shadertoy_ref_height_writes_png
        cubey_project_terrain_shadertoy_ref
        "${CUBEY_TERRAIN_SHADERTOY_REF_HEIGHT_SMOKE_OUTPUT}"
        --headless
        --width
        256
        --height
        256
        --reference-render
        mesh
        --reference-time
        20
        --reference-mesh-cells
        256
        --reference-mesh-surface
        terrain
        --reference-diagnostic
        height
    )
    cubey_add_png_stats_test(
        terrain_shadertoy_ref_height_writes_png
        "${CUBEY_TERRAIN_SHADERTOY_REF_HEIGHT_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    foreach(CUBEY_TERRAIN_SHADERTOY_REF_STUDY IN ITEMS swiss-alps mountain-peak erosion-filter)
        string(REPLACE "-" "_" CUBEY_TERRAIN_SHADERTOY_REF_STUDY_ID
               "${CUBEY_TERRAIN_SHADERTOY_REF_STUDY}")
        set(
            CUBEY_TERRAIN_SHADERTOY_REF_STUDY_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-${CUBEY_TERRAIN_SHADERTOY_REF_STUDY}-smoke.png"
        )
        cubey_add_png_smoke_test(
            terrain_shadertoy_ref_${CUBEY_TERRAIN_SHADERTOY_REF_STUDY_ID}_writes_png
            cubey_project_terrain_shadertoy_ref
            "${CUBEY_TERRAIN_SHADERTOY_REF_STUDY_OUTPUT}"
            --headless
            --width
            320
            --height
            180
            --reference-study
            "${CUBEY_TERRAIN_SHADERTOY_REF_STUDY}"
            --reference-mesh-cells
            256
        )
        cubey_add_png_stats_test(
            terrain_shadertoy_ref_${CUBEY_TERRAIN_SHADERTOY_REF_STUDY_ID}_writes_png
            "${CUBEY_TERRAIN_SHADERTOY_REF_STUDY_OUTPUT}"
            0.00001
            0.00005
        )
    endforeach()

    if (CUBEY_HAS_LIBAV)
        add_test(
            NAME terrain_shadertoy_ref_profile_writes_gpu_spans
            COMMAND
                "${CMAKE_COMMAND}"
                "-DAPP=$<TARGET_FILE:cubey_project_terrain_shadertoy_ref>"
                "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-profile.mp4"
                "-DPROFILE=${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-profile"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/validate_profile.cmake"
        )
    endif()

    cubey_label_tests(
        reference
        terrain_shadertoy_ref_tests
        terrain_shadertoy_ref_raymarch_writes_png
        terrain_shadertoy_ref_raymarch_writes_png_stats
        terrain_shadertoy_ref_mesh_writes_png
        terrain_shadertoy_ref_mesh_writes_png_stats
        terrain_shadertoy_ref_height_writes_png
        terrain_shadertoy_ref_height_writes_png_stats
        terrain_shadertoy_ref_swiss_alps_writes_png
        terrain_shadertoy_ref_swiss_alps_writes_png_stats
        terrain_shadertoy_ref_mountain_peak_writes_png
        terrain_shadertoy_ref_mountain_peak_writes_png_stats
        terrain_shadertoy_ref_erosion_filter_writes_png
        terrain_shadertoy_ref_erosion_filter_writes_png_stats
    )
    if (CUBEY_HAS_LIBAV)
        cubey_label_tests(reference terrain_shadertoy_ref_profile_writes_gpu_spans)
    endif()
endif()
