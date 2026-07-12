cubey_cloud_layer_shader_sources(CUBEY_OCEAN_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_OCEAN_CLOUD_SHADER_DEPENDS)
cubey_atmosphere_shader_depends(CUBEY_OCEAN_ATMOSPHERE_SHADER_DEPENDS)

cubey_add_app_target(
    cubey_project_ocean
    OUTPUT_NAME ocean
    SHADER_DEFINE CUBEY_OCEAN_SHADER_DIR
    WINDOWED_SMOKE_TEST ocean_reaches_glfw
    WINDOWED_SMOKE_PATTERN "ocean: .*rendering ocean project"
    WINDOWED_SMOKE_ARGS
        --ocean-map-size
        128
    SOURCES
        ocean_app.cpp
        ocean_gpu_resources.cpp
        ocean_sea_state.cpp
        ocean_ui.cpp
        main.cpp
    SHADERS
        shaders/ocean_spectrum.comp
        shaders/ocean_spectrum_half.comp
        shaders/ocean_modulate.comp
        shaders/ocean_modulate_half.comp
        shaders/ocean_fft.comp
        shaders/ocean_fft_half.comp
        shaders/ocean_unpack.comp
        shaders/ocean_unpack_half.comp
        shaders/ocean_foam_filter.comp
        shaders/ocean_foam_filter_half.comp
        shaders/ocean.vert
        shaders/ocean.frag
        shaders/ocean_reference_pillar.vert
        shaders/ocean_reference_pillar.frag
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere_reflection_prefilter.frag"
        ${CUBEY_OCEAN_CLOUD_SHADERS}
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_spectrum_body.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_modulate_body.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_fft_body.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_unpack_body.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_foam_filter_body.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_shading.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_far_field.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_foam.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_debug.glsl"
        ${CUBEY_OCEAN_ATMOSPHERE_SHADER_DEPENDS}
        ${CUBEY_OCEAN_CLOUD_SHADER_DEPENDS}
)

if (BUILD_TESTING)
    add_executable(cubey_project_ocean_tests
        ocean_config_tests.cpp
        ocean_sea_state.cpp
    )
    target_compile_definitions(cubey_project_ocean_tests
        PRIVATE
            CUBEY_OCEAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_ocean_tests
        PRIVATE
            cubey::core
            cubey::engine
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME ocean_config_tests COMMAND cubey_project_ocean_tests)

    set(CUBEY_OCEAN_HEADLESS_SMOKE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ocean-headless.png")
    cubey_add_png_smoke_test(
        ocean_headless_writes_png
        cubey_project_ocean
        "${CUBEY_OCEAN_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames
        4
        --ocean-map-size
        128
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_OCEAN_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/ocean-headless.mp4"
        )
        cubey_add_video_smoke_test(
            ocean_headless_writes_video
            cubey_project_ocean
            "${CUBEY_OCEAN_HEADLESS_VIDEO_SMOKE_OUTPUT}"
            --ocean-map-size
            128
        )
    endif()
endif()
