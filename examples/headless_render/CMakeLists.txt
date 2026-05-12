add_executable(cubey_example_headless_render
    headless_render_app.cpp
    main.cpp
)
set_target_properties(cubey_example_headless_render PROPERTIES OUTPUT_NAME headless_render)

target_link_libraries(
    cubey_example_headless_render
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
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
