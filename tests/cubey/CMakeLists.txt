add_executable(cubey_core_tests
    app_config_tests.cpp
    capture_queue_tests.cpp
    command_pool_tests.cpp
    compute_pipeline_tests.cpp
    descriptor_tests.cpp
    frame_clock_tests.cpp
    frame_stats_tests.cpp
    frame_ticket_tests.cpp
    image_output_tests.cpp
    jobs_tests.cpp
    main.cpp
    orbit_controller_tests.cpp
    pipeline_tests.cpp
    project_runtime_tests.cpp
    rendering_tests.cpp
    render_context_tests.cpp
    resource_tests.cpp
    spirv_file_tests.cpp
    upload_queue_tests.cpp
)

target_link_libraries(
    cubey_core_tests
    PRIVATE
        cubey::cubey
        cubey_project_warnings
)

add_test(NAME cubey_core_tests COMMAND cubey_core_tests)
