add_subdirectory(smoke_2d)
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
