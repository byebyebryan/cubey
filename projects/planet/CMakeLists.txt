set(CUBEY_PLANET_SHADERS
    shaders/planet_surface.vert
    shaders/planet_surface.frag
)

cubey_add_app_target(
    cubey_project_planet
    OUTPUT_NAME planet
    SHADER_DEFINE CUBEY_PLANET_SHADER_DIR
    WINDOWED_SMOKE_TEST planet_reaches_glfw
    WINDOWED_SMOKE_PATTERN "planet: .*rendering planet project"
    SOURCES
        main.cpp
        planet_app.cpp
        planet_config.cpp
        planet_frame.cpp
        planet_surface.cpp
    SHADERS
        ${CUBEY_PLANET_SHADERS}
)

if(BUILD_TESTING)
    add_executable(
        cubey_project_planet_tests
        planet_config.cpp
        planet_frame.cpp
        planet_frame_tests.cpp
    )
    target_link_libraries(
        cubey_project_planet_tests
        PRIVATE
            cubey::cubey
            cubey_project_warnings
    )
    add_test(NAME planet_frame_tests COMMAND cubey_project_planet_tests)

    add_executable(
        cubey_project_planet_surface_tests
        planet_config.cpp
        planet_surface.cpp
        planet_surface_tests.cpp
    )
    target_link_libraries(
        cubey_project_planet_surface_tests
        PRIVATE
            cubey::cubey
            cubey_project_warnings
    )
    add_test(NAME planet_surface_tests COMMAND cubey_project_planet_surface_tests)

    set(CUBEY_PLANET_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/planet-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        planet_headless_writes_png
        cubey_project_planet
        "${CUBEY_PLANET_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 2
    )
endif()
