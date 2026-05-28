cubey_add_app_target(
    cubey_project_ocean
    OUTPUT_NAME ocean
    SHADER_DEFINE CUBEY_OCEAN_SHADER_DIR
    WINDOWED_SMOKE_TEST ocean_reaches_glfw
    WINDOWED_SMOKE_PATTERN "ocean: .*rendering ocean project"
    SOURCES
        ocean_app.cpp
        ocean_gpu_resources.cpp
        ocean_ui.cpp
        main.cpp
    SHADERS
        shaders/ocean_spectrum_init.comp
        shaders/ocean_spectrum_evolve.comp
        shaders/ocean_fft.comp
        shaders/ocean_finalize.comp
        shaders/ocean_detail.comp
        shaders/ocean_foam_update.comp
        shaders/ocean_sky.vert
        shaders/ocean_sky.frag
        shaders/ocean_scene.frag
        shaders/ocean.vert
        shaders/ocean.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_atmosphere.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ocean_macro_waves.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_ocean_tests
        ocean_config_tests.cpp
    )
    target_compile_definitions(cubey_project_ocean_tests
        PRIVATE
            CUBEY_OCEAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_ocean_tests
        PRIVATE
            cubey::core
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
        --frames 4
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_OCEAN_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/ocean-headless.mp4"
        )
        cubey_add_video_smoke_test(
            ocean_headless_writes_video
            cubey_project_ocean
            "${CUBEY_OCEAN_HEADLESS_VIDEO_SMOKE_OUTPUT}"
        )
    endif()
endif()
