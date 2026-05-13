cubey_add_app_target(
    cubey_project_pbr_furnace
    OUTPUT_NAME pbr_furnace
    SHADER_DEFINE CUBEY_PBR_FURNACE_SHADER_DIR
    WINDOWED_SMOKE_TEST pbr_furnace_reaches_glfw
    WINDOWED_SMOKE_PATTERN "pbr_furnace: .*rendering PBR white furnace"
    SOURCES
        main.cpp
        pbr_furnace_app.cpp
        pbr_furnace_scene.cpp
    SHADERS
        shaders/pbr_furnace.vert
        shaders/pbr_furnace.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_pbr_furnace_tests
        pbr_furnace_scene.cpp
        pbr_furnace_scene_tests.cpp
    )
    target_include_directories(cubey_project_pbr_furnace_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_project_pbr_furnace_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME pbr_furnace_scene_tests COMMAND cubey_project_pbr_furnace_tests)

    set(CUBEY_PBR_FURNACE_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/pbr-furnace-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        pbr_furnace_headless_writes_png
        cubey_project_pbr_furnace
        "${CUBEY_PBR_FURNACE_HEADLESS_SMOKE_OUTPUT}"
        --headless
    )
endif()
