cubey_forward_pbr_shader_sources(CUBEY_FORWARD_PBR_SHADERS)
cubey_forward_pbr_shader_depends(CUBEY_FORWARD_PBR_SHADER_DEPENDS)
cubey_cloud_layer_shader_sources(CUBEY_GLTF_VIEWER_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_GLTF_VIEWER_CLOUD_SHADER_DEPENDS)
cubey_terrain_backdrop_shader_sources(CUBEY_GLTF_VIEWER_TERRAIN_SHADERS)
cubey_terrain_backdrop_shader_depends(CUBEY_GLTF_VIEWER_TERRAIN_SHADER_DEPENDS)
cubey_ocean_surface_shader_sources(CUBEY_GLTF_VIEWER_OCEAN_SHADERS)
cubey_ocean_surface_shader_depends(CUBEY_GLTF_VIEWER_OCEAN_SHADER_DEPENDS)

cubey_add_app_target(
    cubey_project_gltf_viewer
    OUTPUT_NAME gltf_viewer
    SHADER_DEFINE CUBEY_GLTF_VIEWER_SHADER_DIR
    WINDOWED_SMOKE_TEST gltf_viewer_reaches_glfw
    WINDOWED_SMOKE_PATTERN "gltf_viewer: .*rendering glTF/PBR viewer"
    SOURCES
        gltf_viewer_app.cpp
        gltf_viewer_assets.cpp
        gltf_viewer_loading_cage.cpp
        gltf_viewer_loading_metrics.cpp
        gltf_viewer_render.cpp
        gltf_viewer_scene.cpp
        main.cpp
    SHADERS
        ${CUBEY_FORWARD_PBR_SHADERS}
        ${CUBEY_GLTF_VIEWER_CLOUD_SHADERS}
        ${CUBEY_GLTF_VIEWER_TERRAIN_SHADERS}
        ${CUBEY_GLTF_VIEWER_OCEAN_SHADERS}
        shaders/gltf_deform.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        ${CUBEY_FORWARD_PBR_SHADER_DEPENDS}
        ${CUBEY_GLTF_VIEWER_CLOUD_SHADER_DEPENDS}
        ${CUBEY_GLTF_VIEWER_TERRAIN_SHADER_DEPENDS}
        ${CUBEY_GLTF_VIEWER_OCEAN_SHADER_DEPENDS}
)

add_custom_target(cubey_project_gltf_viewer_bilinear_ocean_shader)
cubey_add_glsl_shaders(
    cubey_project_gltf_viewer_bilinear_ocean_shader
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/filters/bilinear"
    DEFINES CUBEY_OCEAN_DETAIL_FILTER=1
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders"
    DEPENDS ${CUBEY_GLTF_VIEWER_OCEAN_SHADER_DEPENDS}
    SOURCES "${CMAKE_SOURCE_DIR}/shaders/cubey/ocean/ocean.frag"
)
add_custom_target(cubey_project_gltf_viewer_bicubic_ocean_shader)
cubey_add_glsl_shaders(
    cubey_project_gltf_viewer_bicubic_ocean_shader
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/filters/bicubic"
    DEFINES CUBEY_OCEAN_DETAIL_FILTER=2
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders"
    DEPENDS ${CUBEY_GLTF_VIEWER_OCEAN_SHADER_DEPENDS}
    SOURCES "${CMAKE_SOURCE_DIR}/shaders/cubey/ocean/ocean.frag"
)
add_dependencies(
    cubey_project_gltf_viewer
    cubey_project_gltf_viewer_bilinear_ocean_shader
    cubey_project_gltf_viewer_bicubic_ocean_shader
)

if (CUBEY_GLTF_SAMPLE_ASSETS_DIR)
    target_compile_definitions(
        cubey_project_gltf_viewer
        PRIVATE
            CUBEY_GLTF_SAMPLE_ASSETS_DIR="${CUBEY_GLTF_SAMPLE_ASSETS_DIR}"
    )
endif()

if (CUBEY_HDR_SAMPLE_ASSETS_DIR)
    target_compile_definitions(
        cubey_project_gltf_viewer
        PRIVATE
            CUBEY_HDR_SAMPLE_ASSETS_DIR="${CUBEY_HDR_SAMPLE_ASSETS_DIR}"
    )
endif()

if (BUILD_TESTING)
    add_executable(cubey_project_gltf_viewer_tests
        gltf_viewer_config_tests.cpp
    )
    target_include_directories(
        cubey_project_gltf_viewer_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_gltf_viewer_tests
        PRIVATE
            cubey::engine
            cubey::host
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME gltf_viewer_config_tests COMMAND cubey_project_gltf_viewer_tests)
    set_property(
        TEST gltf_viewer_config_tests
        APPEND
        PROPERTY LABELS "gltf;viewer;structural"
    )

    add_executable(
        cubey_project_gltf_viewer_loading_cage_tests
        gltf_viewer_loading_cage.cpp
        gltf_viewer_loading_cage_tests.cpp
    )
    target_link_libraries(
        cubey_project_gltf_viewer_loading_cage_tests
        PRIVATE
            cubey::engine
            cubey::host
            cubey_project_options
            cubey_project_warnings
    )
    add_test(
        NAME gltf_viewer_loading_cage_tests
        COMMAND cubey_project_gltf_viewer_loading_cage_tests
    )
    set_property(
        TEST gltf_viewer_loading_cage_tests
        APPEND
        PROPERTY LABELS "gltf;viewer;structural"
    )

    add_executable(
        cubey_project_gltf_viewer_loading_metrics_tests
        gltf_viewer_loading_metrics.cpp
        gltf_viewer_loading_metrics_tests.cpp
    )
    target_link_libraries(
        cubey_project_gltf_viewer_loading_metrics_tests
        PRIVATE
            cubey::engine
            cubey::host
            cubey_project_options
            cubey_project_warnings
    )
    add_test(
        NAME gltf_viewer_loading_metrics_tests
        COMMAND cubey_project_gltf_viewer_loading_metrics_tests
    )
    set_property(
        TEST gltf_viewer_loading_metrics_tests
        APPEND
        PROPERTY LABELS "gltf;viewer;structural"
    )

    add_test(
        NAME gltf_viewer_loading_profile_script_tests
        COMMAND
            bash
            "${CMAKE_CURRENT_SOURCE_DIR}/profile_gltf_loading_tests.sh"
    )
    set_property(
        TEST gltf_viewer_loading_profile_script_tests
        APPEND
        PROPERTY LABELS "gltf;viewer;structural"
    )

    set(
        CUBEY_GLTF_VIEWER_TERRAIN_SMOKE_ASSET
        "${CMAKE_SOURCE_DIR}/tests/assets/terrain/backdrop-smoke"
    )
    add_test(
        NAME gltf_viewer_uses_forward_pbr_renderer_3d
        COMMAND
            /bin/sh -c
            "grep -q 'engine_\\.renderers()\\.create_forward_pbr_renderer_3d' \"$1\" && grep -q 'frame_resources.wait_for_frame' \"$1\" && grep -q 'forward_pbr_renderer().record({' \"$2\" && grep -q 'clouds.record_pending_update' \"$2\" && grep -q 'atmosphere_clouds' \"$2\" && grep -q 'forward_pbr_renderer().update_environment' \"$2\" && ! grep -R --include='*.cpp' --include='*.h' -q 'ForwardPbrRenderer3D pbr_renderer_' \"$3\" && ! grep -R --include='*.cpp' --include='*.h' -q '<cubey/render/shadow_map.h>' \"$3\""
            gltf_viewer_uses_forward_pbr_renderer_3d
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_assets.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_render.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME gltf_viewer_autoplays_first_gltf_animation
        COMMAND
            /bin/sh -c
            "grep -R --include='*.cpp' --include='*.h' -q 'advance_gltf_animation_playback' \"$1\" && grep -R --include='*.cpp' --include='*.h' -q 'sample_gltf_animation' \"$1\" && grep -R --include='*.cpp' --include='*.h' -q 'apply_gltf_rigid_animation_sample' \"$1\" && grep -R --include='*.cpp' --include='*.h' -q 'update_gltf_deformation_frame' \"$1\""
            gltf_viewer_autoplays_first_gltf_animation
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    # Viewer-specific lifecycle guard: fallback/global resources are created
    # through the host's global phase, whereas parsing is only requested via
    # StagedResource and activation publishes a complete replacement generation.
    add_test(
        NAME gltf_viewer_stages_import_after_fallback
        COMMAND
            /bin/sh -c
            "grep -q 'callbacks.create_global_resources' \"$1\" && grep -q 'create_fallback_scene' \"$2\" && grep -q 'asset_builds_\.request' \"$2\" && grep -q 'callbacks.create_swapchain_resources' \"$1\" && grep -q 'poll_imported_asset_build' \"$1\" && grep -q 'std::shared_ptr<GltfViewerSceneGeneration> previous' \"$2\" && grep -q 'active_generation_ = std::move(next)' \"$2\" && grep -q 'dispose_spent_imported_asset_generation' \"$2\" && grep -q 'GltfViewerResidentGeneration spent) noexcept' \"$2\" && grep -q 'asset_jobs_\.submit' \"$2\" && grep -q 'dispose_completed_import_cpu_shell' \"$2\" && grep -q 'catch (...)' \"$2\" && grep -q 'asset_activation_error_' \"$2\""
            gltf_viewer_stages_import_after_fallback
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_app.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_assets.cpp"
    )
    set_property(
        TEST gltf_viewer_stages_import_after_fallback
        APPEND
        PROPERTY LABELS "gltf;viewer;structural"
    )

    add_test(
        NAME gltf_viewer_uses_metadata_loading_cage
        COMMAND
            /bin/sh -c
            "grep -q 'probe_gltf_scene_bounds' \"$1\" && grep -q 'make_gltf_viewer_loading_cage' \"$1\" && grep -q 'loading_cage' \"$2\" && grep -q 'kVerticesPerPrism = 24U' \"$3\" && grep -q 'kIndicesPerPrism = 36U' \"$3\""
            gltf_viewer_uses_metadata_loading_cage
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_assets.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_loading_cage.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_loading_cage_tests.cpp"
    )
    set_property(
        TEST gltf_viewer_uses_metadata_loading_cage
        APPEND
        PROPERTY LABELS "gltf;viewer;structural"
    )

    add_test(
        NAME gltf_viewer_terrain_backdrop_is_explicit_and_shared
        COMMAND
            /bin/sh -c
            "grep -q 'config_\.terrain\.heightfield_path' \"$1\" && grep -q 'terrain backdrop requires --pbr-environment-source atmosphere' \"$1\" && grep -q 'TerrainBackdropRuntime' \"$2\" && grep -q 'generation\.terrain_surface\.has_value()' \"$3\" && ! grep -R --include='*.cpp' --include='*.h' -q 'projects/terrain' \"$4\""
            gltf_viewer_terrain_backdrop_is_explicit_and_shared
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_assets.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_app_internal.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_render.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME gltf_viewer_ocean_backdrop_is_explicit_and_shared
        COMMAND
            /bin/sh -c
            "grep -q 'config_\\.ocean\\.backdrop' \"$1\" && grep -q 'ocean backdrop requires --pbr-environment-source atmosphere' \"$2\" && grep -q 'OceanSurfaceRuntime' \"$3\" && grep -q '\\.ocean_surface = ocean_surface' \"$2\" && ! grep -R --include='*.cpp' --include='*.h' -q 'projects/ocean' \"$4\""
            gltf_viewer_ocean_backdrop_is_explicit_and_shared
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_assets.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_render.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_app_internal.h"
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    set(CUBEY_GLTF_VIEWER_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        gltf_viewer_headless_writes_png
        cubey_project_gltf_viewer
        "${CUBEY_GLTF_VIEWER_HEADLESS_SMOKE_OUTPUT}"
    )
    set(CUBEY_GLTF_VIEWER_STATIC_ENV_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-static-env-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        gltf_viewer_static_environment_headless_writes_png
        cubey_project_gltf_viewer
        "${CUBEY_GLTF_VIEWER_STATIC_ENV_HEADLESS_SMOKE_OUTPUT}"
        --require-validation
        --pbr-environment-source
        static
    )
    if (CUBEY_HDR_SAMPLE_ASSETS_DIR)
        set(CUBEY_GLTF_VIEWER_STATIC_TIME_DAY_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-static-time-day.png"
        )
        set(CUBEY_GLTF_VIEWER_STATIC_TIME_NIGHT_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-static-time-night.png"
        )
        set(CUBEY_GLTF_VIEWER_STATIC_ROTATED_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-static-rotated.png"
        )
        set(CUBEY_GLTF_VIEWER_STATIC_ENVIRONMENT_PATH
            "${CUBEY_HDR_SAMPLE_ASSETS_DIR}/lightroom_14b.hdr"
        )
        cubey_add_png_capture_test(
            gltf_viewer_static_environment_day_capture
            cubey_project_gltf_viewer
            "${CUBEY_GLTF_VIEWER_STATIC_TIME_DAY_OUTPUT}"
            128
            72
            --require-validation
            --pbr-environment-source
            static
            --environment
            "${CUBEY_GLTF_VIEWER_STATIC_ENVIRONMENT_PATH}"
            --time-of-day-mode
            solar
            --time-hours
            6
            --no-clouds
        )
        cubey_add_png_capture_test(
            gltf_viewer_static_environment_night_capture
            cubey_project_gltf_viewer
            "${CUBEY_GLTF_VIEWER_STATIC_TIME_NIGHT_OUTPUT}"
            128
            72
            --require-validation
            --pbr-environment-source
            static
            --environment
            "${CUBEY_GLTF_VIEWER_STATIC_ENVIRONMENT_PATH}"
            --time-of-day-mode
            solar
            --time-hours
            18
            --no-clouds
        )
        cubey_add_png_capture_test(
            gltf_viewer_static_environment_rotated_capture
            cubey_project_gltf_viewer
            "${CUBEY_GLTF_VIEWER_STATIC_ROTATED_OUTPUT}"
            128
            72
            --require-validation
            --pbr-environment-source
            static
            --environment
            "${CUBEY_GLTF_VIEWER_STATIC_ENVIRONMENT_PATH}"
            --environment-rotation-degrees
            90
            --time-of-day-mode
            solar
            --time-hours
            6
            --no-clouds
        )
        add_test(
            NAME gltf_viewer_static_environment_is_time_invariant_and_rotatable
            COMMAND
                /bin/sh
                -c
                "day=$1; night=$2; rotated=$3; day_skip=$4; night_skip=$5; rotated_skip=$6; if test -f \"$day_skip\" || test -f \"$night_skip\" || test -f \"$rotated_skip\"; then printf 'static environment capture skipped because Vulkan is unavailable\\n'; exit 77; fi; test -s \"$day\" && test -s \"$night\" && test -s \"$rotated\"; if ! cmp -s \"$day\" \"$night\"; then printf 'static environment changed when only atmosphere time changed\\n'; exit 1; fi; if cmp -s \"$day\" \"$rotated\"; then printf 'static IBL rotation did not change the HDR capture\\n'; exit 1; fi"
                gltf_viewer_static_environment_is_time_invariant_and_rotatable
                "${CUBEY_GLTF_VIEWER_STATIC_TIME_DAY_OUTPUT}"
                "${CUBEY_GLTF_VIEWER_STATIC_TIME_NIGHT_OUTPUT}"
                "${CUBEY_GLTF_VIEWER_STATIC_ROTATED_OUTPUT}"
                "${CUBEY_GLTF_VIEWER_STATIC_TIME_DAY_OUTPUT}.skip"
                "${CUBEY_GLTF_VIEWER_STATIC_TIME_NIGHT_OUTPUT}.skip"
                "${CUBEY_GLTF_VIEWER_STATIC_ROTATED_OUTPUT}.skip"
        )
        set_tests_properties(
            gltf_viewer_static_environment_is_time_invariant_and_rotatable
            PROPERTIES
                TIMEOUT 10
                DEPENDS
                    "gltf_viewer_static_environment_day_capture;gltf_viewer_static_environment_night_capture;gltf_viewer_static_environment_rotated_capture"
                SKIP_RETURN_CODE
                    77
        )
        set_property(
            TEST gltf_viewer_static_environment_is_time_invariant_and_rotatable
            APPEND
            PROPERTY LABELS "gltf;viewer;headless;gpu;artifact"
        )
    endif()
    set(
        CUBEY_GLTF_VIEWER_TERRAIN_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-terrain-backdrop-smoke.png"
    )
    cubey_add_png_smoke_test(
        gltf_viewer_terrain_backdrop_headless_writes_png
        cubey_project_gltf_viewer
        "${CUBEY_GLTF_VIEWER_TERRAIN_HEADLESS_SMOKE_OUTPUT}"
        --terrain-heightfield
        "${CUBEY_GLTF_VIEWER_TERRAIN_SMOKE_ASSET}"
        --terrain-foreground-height
        200
        --pbr-environment-source
        atmosphere
    )
    cubey_add_png_stats_test(
        gltf_viewer_terrain_backdrop_headless_writes_png
        "${CUBEY_GLTF_VIEWER_TERRAIN_HEADLESS_SMOKE_OUTPUT}"
        0.02
        0.05
    )
    set(
        CUBEY_GLTF_VIEWER_OCEAN_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-ocean-backdrop-smoke.png"
    )
    cubey_add_png_smoke_test(
        gltf_viewer_ocean_backdrop_headless_writes_png
        cubey_project_gltf_viewer
        "${CUBEY_GLTF_VIEWER_OCEAN_HEADLESS_SMOKE_OUTPUT}"
        --ocean-backdrop
        --ocean-map-size
        128
        --pbr-environment-source
        atmosphere
    )
    cubey_add_png_stats_test(
        gltf_viewer_ocean_backdrop_headless_writes_png
        "${CUBEY_GLTF_VIEWER_OCEAN_HEADLESS_SMOKE_OUTPUT}"
        0.02
        0.05
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_GLTF_VIEWER_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-headless-smoke.mp4"
        )
        cubey_add_video_smoke_test(
            gltf_viewer_headless_writes_video
            cubey_project_gltf_viewer
            "${CUBEY_GLTF_VIEWER_HEADLESS_VIDEO_SMOKE_OUTPUT}"
        )
    endif()

    set(CUBEY_GLTF_VIEWER_SAMPLE_SMOKE_TESTS)
    function(cubey_add_gltf_viewer_sample_smoke_test test_name sample_relative_path output_name)
        if (NOT CUBEY_GLTF_SAMPLE_ASSETS_DIR)
            return()
        endif()

        set(CUBEY_GLTF_VIEWER_SAMPLE_PATH
            "${CUBEY_GLTF_SAMPLE_ASSETS_DIR}/Models/${sample_relative_path}"
        )
        if (NOT EXISTS "${CUBEY_GLTF_VIEWER_SAMPLE_PATH}")
            message(
                FATAL_ERROR
                "Configured glTF sample asset is missing: ${CUBEY_GLTF_VIEWER_SAMPLE_PATH}"
            )
        endif()
        cubey_add_png_smoke_test(
            "${test_name}"
            cubey_project_gltf_viewer
            "${CMAKE_CURRENT_BINARY_DIR}/${output_name}"
            --input "${CUBEY_GLTF_VIEWER_SAMPLE_PATH}"
            --pbr-environment-source static
            --no-clouds
            ${ARGN}
        )
        set_property(
            TEST "${test_name}"
            APPEND
            PROPERTY LABELS "gltf;gltf_sample;compatibility"
        )
        set(sample_tests "${CUBEY_GLTF_VIEWER_SAMPLE_SMOKE_TESTS}")
        list(APPEND sample_tests "${test_name}")
        set(CUBEY_GLTF_VIEWER_SAMPLE_SMOKE_TESTS "${sample_tests}" PARENT_SCOPE)
    endfunction()

    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_alpha_blend_headless_writes_png
        "AlphaBlendModeTest/glTF/AlphaBlendModeTest.gltf"
        "gltf-viewer-alpha-blend-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_damaged_helmet_headless_writes_png
        "DamagedHelmet/glTF/DamagedHelmet.gltf"
        "gltf-viewer-damaged-helmet-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_unlit_test_headless_writes_png
        "UnlitTest/glTF/UnlitTest.gltf"
        "gltf-viewer-unlit-test-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_emissive_strength_test_headless_writes_png
        "EmissiveStrengthTest/glTF/EmissiveStrengthTest.gltf"
        "gltf-viewer-emissive-strength-test-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_clearcoat_test_headless_writes_png
        "ClearCoatTest/glTF/ClearCoatTest.gltf"
        "gltf-viewer-clearcoat-test-smoke.png"
        --ibl-intensity
        0.5
        --exposure
        0
        --capture-camera-distance-scale
        0.55
        --capture-camera-yaw
        0
        --capture-camera-pitch
        0
        --time-of-day-mode
        manual
        --sun-elevation
        14
        --sun-azimuth
        180
        --pause-time
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_transmission_test_headless_writes_png
        "TransmissionTest/glTF/TransmissionTest.gltf"
        "gltf-viewer-transmission-test-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_transmission_roughness_test_headless_writes_png
        "TransmissionRoughnessTest/glTF/TransmissionRoughnessTest.gltf"
        "gltf-viewer-transmission-roughness-test-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_transmission_order_test_headless_writes_png
        "TransmissionOrderTest/glTF/TransmissionOrderTest.gltf"
        "gltf-viewer-transmission-order-test-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_transmission_thinwall_volume_grid_headless_writes_png
        "TransmissionThinwallTestGrid/glTF/TransmissionThinwallTestGrid.gltf"
        "gltf-viewer-transmission-thinwall-volume-grid-smoke.png"
    )
    function(cubey_add_gltf_viewer_material_conformance_fixture
        fixture_name
        fixture_label
        sample_relative_path
        sample_sha256
        output_name
        conformance_case
    )
        if (NOT CUBEY_GLTF_SAMPLE_ASSETS_DIR)
            return()
        endif()

        set(sample_path
            "${CUBEY_GLTF_SAMPLE_ASSETS_DIR}/Models/${sample_relative_path}"
        )
        if (NOT EXISTS "${sample_path}")
            message(
                FATAL_ERROR
                "Configured glTF ${fixture_label} fixture is missing: ${sample_path}"
            )
        endif()

        set(asset_test_name "gltf_viewer_${fixture_name}_asset_is_pinned")
        add_test(
            NAME "${asset_test_name}"
            COMMAND
                /bin/sh
                -c
                "path=\$1; expected=\$2; actual=\$(sha256sum \"\$path\" | cut -d ' ' -f 1); printf '${fixture_label} fixture sha256: %s\\n' \"\$actual\"; test \"\$actual\" = \"\$expected\""
                "${asset_test_name}"
                "${sample_path}"
                "${sample_sha256}"
        )
        set_tests_properties("${asset_test_name}" PROPERTIES TIMEOUT 10)
        set_property(
            TEST "${asset_test_name}"
            APPEND
            PROPERTY LABELS "gltf;gltf_sample;conformance"
        )

        set(output_path "${CMAKE_CURRENT_BINARY_DIR}/${output_name}")
        set(capture_test_name "gltf_viewer_${fixture_name}_conformance_writes_png")
        cubey_add_png_capture_test(
            "${capture_test_name}"
            cubey_project_gltf_viewer
            "${output_path}"
            1920
            1080
            --input
            "${sample_path}"
            ${ARGN}
        )
        cubey_add_png_material_conformance_test(
            "${capture_test_name}"
            "${output_path}"
            "${conformance_case}"
        )
        set(conformance_test_name "${capture_test_name}_material_conformance")
        set_tests_properties(
            "${conformance_test_name}"
            PROPERTIES
                DEPENDS
                    "${capture_test_name};${asset_test_name}"
        )
        cubey_label_tests(
            conformance
            "${capture_test_name}"
            "${conformance_test_name}"
        )
        cubey_label_tests(
            gltf
            "${capture_test_name}"
            "${conformance_test_name}"
        )
        cubey_label_tests(
            gltf_sample
            "${capture_test_name}"
            "${conformance_test_name}"
        )
    endfunction()

    # The semantic analyzer is calibrated to the authored five-by-five layout in the pinned
    # 2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf checkout.
    # The asset's oracle requires face-on viewing. Preserve the grid's framing with a long lens
    # so per-cell screen refraction does not become a row-dependent presentation variable.
    cubey_add_gltf_viewer_material_conformance_fixture(
        attenuation_test
        attenuation
        "AttenuationTest/glTF-Binary/AttenuationTest.glb"
        7ca161b7f8a9e4b2ac1f7f75816b5848bb31f3b4c226c4cb731b487c8809b756
        "gltf-viewer-attenuation-test-conformance.png"
        gltf-attenuation-test
        --pbr-environment-source
        static
        --no-clouds
        --exposure
        0
        --pbr-tonemap
        linear
        --capture-camera-fov-y-degrees
        14.65
        --capture-camera-distance-scale
        2
        --capture-camera-yaw
        0
        --capture-camera-pitch
        0
        --pause-time
    )

    cubey_add_gltf_viewer_material_conformance_fixture(
        transmission_roughness_test
        "transmission roughness"
        "TransmissionRoughnessTest/glTF-Binary/TransmissionRoughnessTest.glb"
        43ace8f8dfe0b6148dc4668e6a768c8853ba37cf1f0fe7de76eaf0ea8e94bb73
        "gltf-viewer-transmission-roughness-test-conformance.png"
        gltf-transmission-roughness-test
        --pbr-environment-source
        static
        --no-clouds
        --exposure
        0
        --pbr-tonemap
        linear
        --capture-camera-fov-y-degrees
        14.65
        --capture-camera-distance-scale
        2
        --capture-camera-yaw
        0
        --capture-camera-pitch
        0
        --pause-time
    )

    # Pitch the camera down slightly so the authored prisms expose their refractive edges while
    # the complete five-by-five grid remains in frame.
    cubey_add_gltf_viewer_material_conformance_fixture(
        dispersion_test
        dispersion
        "DispersionTest/glTF-Binary/DispersionTest.glb"
        bbac38386632b5a7a27289737963314c2f92095074dfdf0e8c0c1c25270b3e37
        "gltf-viewer-dispersion-test-conformance.png"
        gltf-dispersion-test
        --pbr-environment-source
        static
        --no-clouds
        --exposure
        0
        --pbr-tonemap
        linear
        --capture-camera-fov-y-degrees
        20
        --capture-camera-distance-scale
        0.2
        --capture-camera-yaw
        0
        --capture-camera-pitch
        -25
        --pause-time
    )

    # The authored grid is a face-on optical oracle. Preserve its complete checker layout with
    # the same long-lens capture contract as the other transmission fixtures.
    cubey_add_gltf_viewer_material_conformance_fixture(
        transmission_thinwall_test
        "transmission thin-wall"
        "TransmissionThinwallTestGrid/glTF-Binary/TransmissionThinwallTestGrid.glb"
        e4ee8e5a28f81f75ef40b264ad22725e9065470b8ee02f9a4c6fa96b669cbcc7
        "gltf-viewer-transmission-thinwall-test-conformance.png"
        gltf-transmission-thinwall-test
        --pbr-environment-source
        static
        --no-clouds
        --exposure
        0
        --pbr-tonemap
        linear
        --capture-camera-fov-y-degrees
        14.65
        --capture-camera-distance-scale
        2
        --capture-camera-yaw
        0
        --capture-camera-pitch
        0
        --pause-time
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_dragon_attenuation_volume_headless_writes_png
        "DragonAttenuation/glTF/DragonAttenuation.gltf"
        "gltf-viewer-dragon-attenuation-volume-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_dispersion_test_headless_writes_png
        "DispersionTest/glTF/DispersionTest.gltf"
        "gltf-viewer-dispersion-test-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_dragon_dispersion_headless_writes_png
        "DragonDispersion/glTF/DragonDispersion.gltf"
        "gltf-viewer-dragon-dispersion-smoke.png"
    )
    if (CUBEY_GLTF_SAMPLE_ASSETS_DIR)
        set(CUBEY_GLTF_VIEWER_DRAGON_DISPERSION_DIAGNOSTIC_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-dragon-dispersion-atmosphere-diagnostic.png"
        )
        cubey_add_png_capture_test(
            gltf_viewer_dragon_dispersion_atmosphere_diagnostic_writes_png
            cubey_project_gltf_viewer
            "${CUBEY_GLTF_VIEWER_DRAGON_DISPERSION_DIAGNOSTIC_OUTPUT}"
            512
            512
            --input
            "${CUBEY_GLTF_SAMPLE_ASSETS_DIR}/Models/DragonDispersion/glTF/DragonDispersion.gltf"
            --pbr-environment-source
            atmosphere
            --no-clouds
            --ibl-intensity
            1
            --exposure
            0
            --capture-camera-distance-scale
            0.25
            --capture-camera-yaw
            0
            --capture-camera-pitch
            0
            --time-of-day-mode
            manual
            --sun-elevation
            35
            --sun-azimuth
            180
            --pause-time
        )
        set_property(
            TEST gltf_viewer_dragon_dispersion_atmosphere_diagnostic_writes_png
            APPEND
            PROPERTY LABELS "gltf;gltf_sample;dispersion;diagnostic"
        )
    endif()
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_anisotropy_barn_lamp_ktx_basisu_headless_writes_png
        "AnisotropyBarnLamp/glTF-KTX-BasisU/AnisotropyBarnLamp.gltf"
        "gltf-viewer-anisotropy-barn-lamp-ktx-basisu-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_compare_iridescence_headless_writes_png
        "CompareIridescence/glTF/CompareIridescence.gltf"
        "gltf-viewer-compare-iridescence-smoke.png"
        --capture-camera-distance-scale
        0.5
        --capture-camera-yaw
        0
        --capture-camera-pitch
        0
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_sheen_test_grid_headless_writes_png
        "SheenTestGrid/glTF/SheenTestGrid.gltf"
        "gltf-viewer-sheen-test-grid-smoke.png"
        --capture-camera-distance-scale
        0.2
        --capture-camera-yaw
        0
        --capture-camera-pitch
        0
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_stained_glass_lamp_ktx_basisu_headless_writes_png
        "StainedGlassLamp/glTF-KTX-BasisU/StainedGlassLamp.gltf"
        "gltf-viewer-stained-glass-lamp-ktx-basisu-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_specular_test_headless_writes_png
        "SpecularTest/glTF/SpecularTest.gltf"
        "gltf-viewer-specular-test-smoke.png"
    )
    if (CUBEY_GLTF_SAMPLE_ASSETS_DIR)
        set(CUBEY_GLTF_VIEWER_SPECULAR_TEST_CONFORMANCE_PATH
            "${CUBEY_GLTF_SAMPLE_ASSETS_DIR}/Models/SpecularTest/glTF/SpecularTest.gltf"
        )
        set(CUBEY_GLTF_VIEWER_SPECULAR_TEST_CONFORMANCE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-specular-test-conformance.png"
        )
        cubey_add_png_capture_test(
            gltf_viewer_specular_test_material_conformance_writes_png
            cubey_project_gltf_viewer
            "${CUBEY_GLTF_VIEWER_SPECULAR_TEST_CONFORMANCE_OUTPUT}"
            512
            512
            --input
            "${CUBEY_GLTF_VIEWER_SPECULAR_TEST_CONFORMANCE_PATH}"
            --pbr-environment-source
            static
            --ibl-intensity
            0.5
            --no-clouds
            --exposure
            0
            --capture-camera-distance-scale
            0.5
            --capture-camera-yaw
            0
            --capture-camera-pitch
            0
            --time-of-day-mode
            manual
            --sun-elevation
            14
            --sun-azimuth
            180
            --pause-time
        )
        cubey_add_png_material_conformance_test(
            gltf_viewer_specular_test_material_conformance_writes_png
            "${CUBEY_GLTF_VIEWER_SPECULAR_TEST_CONFORMANCE_OUTPUT}"
            gltf-specular-test
        )
        cubey_label_tests(
            conformance
            gltf_viewer_specular_test_material_conformance_writes_png
            gltf_viewer_specular_test_material_conformance_writes_png_material_conformance
        )
        cubey_label_tests(
            gltf
            gltf_viewer_specular_test_material_conformance_writes_png
            gltf_viewer_specular_test_material_conformance_writes_png_material_conformance
        )
        cubey_label_tests(
            gltf_sample
            gltf_viewer_specular_test_material_conformance_writes_png
            gltf_viewer_specular_test_material_conformance_writes_png_material_conformance
        )
    endif()
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_texture_transform_headless_writes_png
        "TextureTransformTest/glTF/TextureTransformTest.gltf"
        "gltf-viewer-texture-transform-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_texture_transform_multi_headless_writes_png
        "TextureTransformMultiTest/glTF/TextureTransformMultiTest.gltf"
        "gltf-viewer-texture-transform-multi-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_normal_tangent_headless_writes_png
        "NormalTangentTest/glTF/NormalTangentTest.gltf"
        "gltf-viewer-normal-tangent-smoke.png"
    )
    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_normal_tangent_mirror_headless_writes_png
        "NormalTangentMirrorTest/glTF/NormalTangentMirrorTest.gltf"
        "gltf-viewer-normal-tangent-mirror-smoke.png"
    )
    if (CUBEY_GLTF_SAMPLE_ASSETS_DIR)
        list(
            LENGTH
            CUBEY_GLTF_VIEWER_SAMPLE_SMOKE_TESTS
            CUBEY_GLTF_VIEWER_SAMPLE_SMOKE_TEST_COUNT
        )
        if (CUBEY_GLTF_VIEWER_SAMPLE_SMOKE_TEST_COUNT EQUAL 0)
            message(
                FATAL_ERROR
                "Configured Khronos glTF Sample Assets produced zero expected viewer sample smoke tests"
            )
        endif()
        message(
            STATUS
            "Cubey glTF sample compatibility inventory: ${CUBEY_GLTF_VIEWER_SAMPLE_SMOKE_TEST_COUNT} tests"
        )
    endif()
endif()
