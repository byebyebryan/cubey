# River V0 starts with a CPU contract/oracle target.  The windowed and GPU
# targets will be added only after the numerical behavior has evidence.
if (BUILD_TESTING)
    add_executable(cubey_project_fluid_25d_tests
        ../sim/fluid_25d/fluid_25d_oracle.cpp
        ../sim/fluid_25d/fluid_25d_tests.cpp
    )
    target_include_directories(
        cubey_project_fluid_25d_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
    )
    target_link_libraries(
        cubey_project_fluid_25d_tests
        PRIVATE
            cubey::cubey
            cubey::host
            cubey_project_fluid_common
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fluid_25d_tests COMMAND cubey_project_fluid_25d_tests)
endif()
