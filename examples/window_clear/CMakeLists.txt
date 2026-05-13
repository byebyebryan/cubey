cubey_add_app_target(
    cubey_example_window_clear
    OUTPUT_NAME window_clear
    WINDOWED_SMOKE_TEST window_clear_reaches_glfw
    WINDOWED_SMOKE_PATTERN "window_clear:"
    SOURCES
        main.cpp
        window_clear_app.cpp
)
