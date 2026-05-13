cubey_add_app_target(
    cubey_example_headless_render
    OUTPUT_NAME headless_render
    SOURCES
        headless_render_app.cpp
        main.cpp
)

if (BUILD_TESTING)
    set(CUBEY_HEADLESS_RENDER_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/headless-render-smoke.png"
    )
    cubey_add_png_smoke_test(
        headless_render_writes_png
        cubey_example_headless_render
        "${CUBEY_HEADLESS_RENDER_SMOKE_OUTPUT}"
    )
endif()
