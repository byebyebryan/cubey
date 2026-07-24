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

    add_test(
        NAME gltf_viewer_terrain_backdrop_is_explicit_and_shared
        COMMAND
            /bin/sh -c
            "grep -q 'config_\.terrain\.heightfield_path' \"$1\" && grep -q 'terrain backdrop requires --pbr-environment-source atmosphere' \"$1\" && grep -q 'TerrainBackdropRuntime' \"$2\" && grep -q 'terrain_backdrop' \"$3\" && ! grep -R --include='*.cpp' --include='*.h' -q 'projects/terrain' \"$4\""
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
        --pbr-environment-source
        static
    )
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
        )
    endfunction()

    cubey_add_gltf_viewer_sample_smoke_test(
        gltf_viewer_alpha_mask_headless_writes_png
        "AlphaMaskModeTest/glTF/AlphaMaskModeTest.gltf"
        "gltf-viewer-alpha-mask-smoke.png"
    )
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
        gltf_viewer_anisotropy_barn_lamp_ktx_basisu_headless_writes_png
        "AnisotropyBarnLamp/glTF-KTX-BasisU/AnisotropyBarnLamp.gltf"
        "gltf-viewer-anisotropy-barn-lamp-ktx-basisu-smoke.png"
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
endif()
