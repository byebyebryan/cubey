if (BUILD_TESTING)
    add_executable(cubey_project_fluid_2d_tests
        fluid_2d_config_tests.cpp
    )
    target_include_directories(cubey_project_fluid_2d_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_project_fluid_2d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fluid_2d_config_tests COMMAND cubey_project_fluid_2d_tests)
endif()
