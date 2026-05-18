cubey_forward_pbr_shader_sources(CUBEY_FORWARD_PBR_SHADERS)

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
        shaders/gltf_deform.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
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
    add_test(
        NAME gltf_viewer_uses_forward_pbr_renderer_3d
        COMMAND
            /bin/sh -c
            "grep -q 'engine_\\.renderers()\\.create_forward_pbr_renderer_3d' \"$1\" && grep -q 'forward_pbr_renderer().record({' \"$2\" && ! grep -R --include='*.cpp' --include='*.h' -q 'ForwardPbrRenderer3D pbr_renderer_' \"$3\" && ! grep -R --include='*.cpp' --include='*.h' -q '<cubey/render/shadow_map.h>' \"$3\""
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

    set(CUBEY_GLTF_VIEWER_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/gltf-viewer-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        gltf_viewer_headless_writes_png
        cubey_project_gltf_viewer
        "${CUBEY_GLTF_VIEWER_HEADLESS_SMOKE_OUTPUT}"
        --headless
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
            --headless
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
