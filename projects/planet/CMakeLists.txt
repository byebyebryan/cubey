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
    SHADERS
        ${CUBEY_PLANET_SHADERS}
)

if(BUILD_TESTING)
    set(CUBEY_PLANET_HEADLESS_SMOKE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/planet-headless-smoke.png")
    cubey_add_png_smoke_test(
        planet_headless_writes_png
        cubey_project_planet
        "${CUBEY_PLANET_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 2
    )
endif()
