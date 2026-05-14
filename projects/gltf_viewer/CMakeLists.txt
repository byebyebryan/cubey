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
        shaders/gltf_pbr.vert
        shaders/gltf_pbr.frag
        shaders/gltf_skybox.vert
        shaders/gltf_skybox.frag
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr_post.frag"
        shaders/gltf_shadow_depth.vert
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
            "grep -q 'engine_\\.renderers()\\.create_forward_pbr_renderer_3d' \"$1\" && grep -q 'ForwardPbrRenderer3DRenderRequest' \"$2\" && ! grep -R --include='*.cpp' --include='*.h' -q 'ForwardPbrRenderer3D pbr_renderer_' \"$3\" && ! grep -R --include='*.cpp' --include='*.h' -q '<cubey/render/shadow_map.h>' \"$3\""
            gltf_viewer_uses_forward_pbr_renderer_3d
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_assets.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gltf_viewer_render.cpp"
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
endif()
