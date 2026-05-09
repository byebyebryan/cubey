add_executable(cubey_core_tests
    run_config_tests.cpp
    camera_tests.cpp
    capture_queue_tests.cpp
    command_pool_tests.cpp
    compute_pipeline_tests.cpp
    descriptor_tests.cpp
    dynamic_rendering_tests.cpp
    file_io_tests.cpp
    frame_clock_tests.cpp
    frame_stats_tests.cpp
    frame_ticket_tests.cpp
    headless_png_host_tests.cpp
    image_io_tests.cpp
    jobs_tests.cpp
    input_tests.cpp
    main.cpp
    math_tests.cpp
    orbit_controller_tests.cpp
    pipeline_tests.cpp
    project_runtime_tests.cpp
    image_transitions_tests.cpp
    render_context_tests.cpp
    resource_tests.cpp
    spirv_io_tests.cpp
    transform_tests.cpp
    upload_queue_tests.cpp
)

target_link_libraries(
    cubey_core_tests
    PRIVATE
        cubey::cubey
        cubey_project_warnings
)

add_test(NAME cubey_core_tests COMMAND cubey_core_tests)
