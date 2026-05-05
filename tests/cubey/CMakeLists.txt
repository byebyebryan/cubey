add_executable(cubey_core_tests
    command_pool_tests.cpp
    frame_clock_tests.cpp
    frame_stats_tests.cpp
    orbit_controller_tests.cpp
    pipeline_tests.cpp
    rendering_tests.cpp
    render_context_tests.cpp
    resource_tests.cpp
)

target_link_libraries(
    cubey_core_tests
    PRIVATE
        cubey::cubey
        cubey_project_warnings
)

add_test(NAME cubey_core_tests COMMAND cubey_core_tests)
