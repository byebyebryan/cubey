cubey_add_app_target(
    cubey_example_headless_cube
    OUTPUT_NAME headless_cube
    SHADER_DEFINE CUBEY_HEADLESS_CUBE_SHADER_DIR
    SOURCES
        headless_cube_app.cpp
        headless_cube_render.cpp
        headless_cube_resources.cpp
        main.cpp
    SHADERS
        shaders/headless_cube.vert
        shaders/headless_cube.frag
)

if (BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        add_test(NAME headless_cube_render_only
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_render_only.py"
                "$<TARGET_FILE:cubey_example_headless_cube>")
        set_tests_properties(headless_cube_render_only PROPERTIES
            LABELS "gpu;headless" SKIP_RETURN_CODE 77 TIMEOUT 90)
    endif()
    set(CUBEY_HEADLESS_CUBE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/headless-cube-smoke.png"
    )
    cubey_add_png_smoke_test(
        headless_cube_writes_png
        cubey_example_headless_cube
        "${CUBEY_HEADLESS_CUBE_SMOKE_OUTPUT}"
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_HEADLESS_CUBE_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/headless-cube-smoke.mp4"
        )
        cubey_add_video_smoke_test(
            headless_cube_writes_video
            cubey_example_headless_cube
            "${CUBEY_HEADLESS_CUBE_VIDEO_SMOKE_OUTPUT}"
        )
    endif()
endif()
