cubey_atmosphere_shader_depends(CUBEY_PLANET_ATMOSPHERE_SHADER_DEPENDS)

cubey_add_app_target(
    cubey_project_planet
    OUTPUT_NAME planet
    SHADER_DEFINE CUBEY_PLANET_SHADER_DIR
    WINDOWED_SMOKE_TEST planet_reaches_glfw
    WINDOWED_SMOKE_PATTERN "planet: .*rendering orbital planet project"
    SOURCES
        main.cpp
        planet_config.cpp
        planet_app.cpp
        planet_surface_product.cpp
    SHADERS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        shaders/planet_orbit.vert
        shaders/planet_orbit.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        ${CUBEY_PLANET_ATMOSPHERE_SHADER_DEPENDS}
)

if (BUILD_TESTING)
    add_executable(cubey_project_planet_tests
        planet_config.cpp
        planet_config_tests.cpp
        planet_surface_product.cpp
        planet_surface_product_tests.cpp
    )
    target_include_directories(cubey_project_planet_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_project_planet_tests
        PRIVATE
            cubey::cubey
            cubey::host
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME planet_surface_product_tests COMMAND cubey_project_planet_tests)

    set(CUBEY_PLANET_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/planet-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        planet_headless_writes_png
        cubey_project_planet
        "${CUBEY_PLANET_HEADLESS_SMOKE_OUTPUT}"
    )
endif()
