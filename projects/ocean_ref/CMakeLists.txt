cubey_add_app_target(
    cubey_project_ocean_ref
    OUTPUT_NAME ocean_ref
    SHADER_DEFINE CUBEY_OCEAN_REF_SHADER_DIR
    WINDOWED_SMOKE_TEST ocean_ref_reaches_glfw
    WINDOWED_SMOKE_PATTERN "ocean_ref: .*rendering ocean reference project"
    WINDOWED_SMOKE_ARGS
        --ocean-ref-map-size
        128
    SOURCES
        ocean_ref_app.cpp
        ocean_ref_gpu_resources.cpp
        ocean_ref_ui.cpp
        main.cpp
    SHADERS
        shaders/ocean_ref_spectrum.comp
        shaders/ocean_ref_modulate.comp
        shaders/ocean_ref_fft.comp
        shaders/ocean_ref_unpack.comp
        shaders/ocean_ref_sky.vert
        shaders/ocean_ref_sky.frag
        shaders/ocean_ref.vert
        shaders/ocean_ref.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_ref_atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_ocean_ref_tests
        ocean_ref_config_tests.cpp
    )
    target_compile_definitions(cubey_project_ocean_ref_tests
        PRIVATE
            CUBEY_OCEAN_REF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_ocean_ref_tests
        PRIVATE
            cubey::core
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME ocean_ref_config_tests COMMAND cubey_project_ocean_ref_tests)

    set(CUBEY_OCEAN_REF_HEADLESS_SMOKE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ocean-ref-headless.png")
    cubey_add_png_smoke_test(
        ocean_ref_headless_writes_png
        cubey_project_ocean_ref
        "${CUBEY_OCEAN_REF_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames
        4
        --ocean-ref-map-size
        128
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_OCEAN_REF_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/ocean-ref-headless.mp4"
        )
        cubey_add_video_smoke_test(
            ocean_ref_headless_writes_video
            cubey_project_ocean_ref
            "${CUBEY_OCEAN_REF_HEADLESS_VIDEO_SMOKE_OUTPUT}"
            --ocean-ref-map-size
            128
        )
    endif()
endif()
