add_executable(cubey_example_window_clear
    main.cpp
    window_clear_app.cpp
)
set_target_properties(cubey_example_window_clear PROPERTIES OUTPUT_NAME window_clear)

target_link_libraries(
    cubey_example_window_clear
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
)

if (BUILD_TESTING)
    cubey_add_windowed_smoke_test(
        window_clear_reaches_glfw
        cubey_example_window_clear
        "window_clear:"
    )
endif()
