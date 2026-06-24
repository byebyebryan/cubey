add_library(cubey_project_fluid_common INTERFACE)
target_include_directories(
    cubey_project_fluid_common
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/sim/common"
)
target_link_libraries(
    cubey_project_fluid_common
    INTERFACE
        cubey::core
)

if (BUILD_TESTING)
    add_executable(cubey_project_fluid_common_tests
        sim/common/water_common_tests.cpp
    )
    target_link_libraries(
        cubey_project_fluid_common_tests
        PRIVATE
            cubey_project_fluid_common
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fluid_common_tests COMMAND cubey_project_fluid_common_tests)
endif()

add_subdirectory(smoke_2d)
add_subdirectory(water_2d)
add_subdirectory(water_3d)

set(CUBEY_PYRO_3D_SOURCES
    ../sim/pyro_3d/pyro_3d_app.cpp
    ../sim/pyro_3d/pyro_3d_commands.cpp
    ../sim/pyro_3d/pyro_3d_gpu_resources.cpp
    ../sim/pyro_3d/pyro_3d_sources.cpp
)

set(CUBEY_PYRO_3D_SHADERS
    ../sim/pyro_3d/shaders/pyro_3d.vert
    ../sim/pyro_3d/shaders/pyro_3d_raymarch.frag
    ../../../shaders/cubey/atmosphere/atmosphere.vert
    ../../../shaders/cubey/atmosphere/atmosphere.frag
    ../../../shaders/cubey/sky/celestial_body.vert
    ../../../shaders/cubey/sky/celestial_body.frag
    ../sim/pyro_3d/shaders/pyro_3d_reset.comp
    ../sim/pyro_3d/shaders/pyro_3d_advect.comp
    ../sim/pyro_3d/shaders/pyro_3d_advect_correct.comp
    ../sim/pyro_3d/shaders/pyro_3d_combust.comp
    ../sim/pyro_3d/shaders/pyro_3d_divergence.comp
    ../sim/pyro_3d/shaders/pyro_3d_pressure.comp
    ../sim/pyro_3d/shaders/pyro_3d_projection.comp
    ../sim/pyro_3d/shaders/pyro_3d_shadow.comp
)

add_subdirectory(fire_3d)
add_subdirectory(explosion_3d)

if (BUILD_TESTING)
    add_executable(cubey_project_pyro_3d_tests
        sim/pyro_3d/pyro_3d_sources.cpp
        sim/pyro_3d/pyro_3d_config_tests.cpp
    )
    target_include_directories(
        cubey_project_pyro_3d_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/sim/pyro_3d"
    )
    target_compile_definitions(cubey_project_pyro_3d_tests
        PRIVATE
            CUBEY_PYRO_3D_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sim/pyro_3d"
    )
    target_link_libraries(
        cubey_project_pyro_3d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME pyro_3d_config_tests COMMAND cubey_project_pyro_3d_tests)
endif()
