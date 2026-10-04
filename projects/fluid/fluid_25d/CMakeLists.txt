cubey_add_app_target(
    cubey_project_fluid_25d
    OUTPUT_NAME fluid_25d
    SHADER_DEFINE CUBEY_FLUID_25D_SHADER_DIR
    WINDOWED_SMOKE_TEST fluid_25d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fluid_25d: .*rendering River V0"
    SOURCES
        ../sim/fluid_25d/fluid_25d_app.cpp
        ../sim/fluid_25d/fluid_25d_backend_adapters.cpp
        ../sim/fluid_25d/fluid_25d_backend_contract.cpp
        ../sim/fluid_25d/fluid_25d_external_session.cpp
        ../sim/fluid_25d/fluid_25d_commands.cpp
        ../sim/fluid_25d/fluid_25d_diagnostics.cpp
        ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
        ../sim/fluid_25d/fluid_25d_gpu_resources.cpp
        ../sim/fluid_25d/fluid_25d_mass_audit.cpp
        ../sim/fluid_25d/fluid_25d_motion_markers.cpp
        ../sim/fluid_25d/fluid_25d_natural_flow_recipe.cpp
        ../sim/fluid_25d/fluid_25d_oracle.cpp
        ../sim/fluid_25d/fluid_25d_recording.cpp
        ../sim/fluid_25d/fluid_25d_recording_app.cpp
        ../sim/fluid_25d/fluid_25d_ui.cpp
        main.cpp
    SHADERS
        ../sim/fluid_25d/shaders/fluid_25d.vert
        ../sim/fluid_25d/shaders/fluid_25d_render.frag
        ../sim/fluid_25d/shaders/fluid_25d_terrain.vert
        ../sim/fluid_25d/shaders/fluid_25d_terrain.frag
        ../sim/fluid_25d/shaders/fluid_25d_water.vert
        ../sim/fluid_25d/shaders/fluid_25d_water.frag
        ../sim/fluid_25d/shaders/fluid_25d_forcing_cubes.vert
        ../sim/fluid_25d/shaders/fluid_25d_forcing_cubes.frag
        ../sim/fluid_25d/shaders/fluid_25d_quiver.vert
        ../sim/fluid_25d/shaders/fluid_25d_quiver.frag
        ../sim/fluid_25d/shaders/fluid_25d_reset.comp
        ../sim/fluid_25d/shaders/fluid_25d_flux.comp
        ../sim/fluid_25d/shaders/fluid_25d_depth.comp
        ../sim/fluid_25d/shaders/fluid_25d_fv_reset.comp
        ../sim/fluid_25d/shaders/fluid_25d_fv_cfl.comp
        ../sim/fluid_25d/shaders/fluid_25d_fv_cfl_finalize.comp
        ../sim/fluid_25d/shaders/fluid_25d_fv_update.comp
        ../sim/fluid_25d/shaders/fluid_25d_fv_commit.comp
        ../sim/fluid_25d/shaders/fluid_25d_backend_progress.comp
        ../sim/fluid_25d/shaders/fluid_25d_presentation_cue_reset.comp
        ../sim/fluid_25d/shaders/fluid_25d_presentation_cue_advect.comp
        ../sim/fluid_25d/shaders/fluid_25d_quiver_reset.comp
        ../sim/fluid_25d/shaders/fluid_25d_quiver_update.comp
        ../sim/fluid_25d/shaders/fluid_25d_motion_markers.comp
        ../sim/fluid_25d/shaders/fluid_25d_motion_markers.vert
        ../sim/fluid_25d/shaders/fluid_25d_motion_markers.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d/shaders/fluid_25d_mass_audit.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d/shaders/fluid_25d_conserved_arithmetic.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d/shaders/fluid_25d_motion_markers.glsl"
)

target_include_directories(
    cubey_project_fluid_25d
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)

# Explicit read/control diagnostic only; no automatic native launch or CUDA dependency.
add_executable(cubey_project_fluid_25d_external_probe EXCLUDE_FROM_ALL
    ../sim/fluid_25d/fluid_25d_backend_contract.cpp
    ../sim/fluid_25d/fluid_25d_external_session.cpp
    ../sim/fluid_25d/fluid_25d_external_session_probe.cpp)
target_link_libraries(cubey_project_fluid_25d_external_probe
    PRIVATE cubey::cubey nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)

if (BUILD_TESTING)
    add_executable(cubey_project_fluid_25d_external_session_tests
        ../sim/fluid_25d/fluid_25d_backend_contract.cpp
        ../sim/fluid_25d/fluid_25d_external_session.cpp
        ../sim/fluid_25d/fluid_25d_external_session_tests.cpp)
    target_link_libraries(cubey_project_fluid_25d_external_session_tests
        PRIVATE cubey::cubey nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)
    add_test(NAME fluid_25d_external_session
        COMMAND cubey_project_fluid_25d_external_session_tests)
    set_tests_properties(fluid_25d_external_session PROPERTIES LABELS "unit;fluid;backend")
    add_executable(cubey_project_fluid_25d_recording_tests
        ../sim/fluid_25d/fluid_25d_recording.cpp
        ../sim/fluid_25d/fluid_25d_recording_tests.cpp)
    target_link_libraries(cubey_project_fluid_25d_recording_tests
        PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
    add_test(NAME fluid_25d_recording_contract
        COMMAND cubey_project_fluid_25d_recording_tests)
    set_tests_properties(fluid_25d_recording_contract PROPERTIES LABELS "unit;fluid;recording")
    add_executable(cubey_project_fluid_25d_backend_contract_tests
        ../sim/fluid_25d/fluid_25d_backend_contract.cpp
        ../sim/fluid_25d/fluid_25d_backend_contract_tests.cpp)
    target_include_directories(
        cubey_project_fluid_25d_backend_contract_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
    )
    target_link_libraries(cubey_project_fluid_25d_backend_contract_tests
        PRIVATE nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)
    add_test(NAME fluid_25d_backend_contract
        COMMAND cubey_project_fluid_25d_backend_contract_tests)
    set_tests_properties(fluid_25d_backend_contract PROPERTIES LABELS "unit;fluid;backend")
    add_executable(cubey_project_fluid_25d_backend_adapters_tests
        ../sim/fluid_25d/fluid_25d_backend_contract.cpp
        ../sim/fluid_25d/fluid_25d_backend_adapters.cpp
        ../sim/fluid_25d/fluid_25d_recording.cpp
        ../sim/fluid_25d/fluid_25d_backend_adapters_tests.cpp)
    target_include_directories(
        cubey_project_fluid_25d_backend_adapters_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
    )
    target_link_libraries(cubey_project_fluid_25d_backend_adapters_tests
        PRIVATE
            cubey::cubey
            cubey::host
            nlohmann_json::nlohmann_json
            cubey_project_options
            cubey_project_warnings)
    add_test(NAME fluid_25d_backend_adapters
        COMMAND cubey_project_fluid_25d_backend_adapters_tests)
    set_tests_properties(fluid_25d_backend_adapters PROPERTIES LABELS "unit;fluid;backend")
    find_program(CUBEY_FLUID_RECORDING_PYTHON NAMES python3)
    if (CUBEY_FLUID_RECORDING_PYTHON)
        add_test(NAME fluid_25d_external_worker_host
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}/external_synxflow" -p test_session_worker.py)
        add_test(NAME fluid_25d_external_worker_build_helpers
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}/external_synxflow" -p test_build_synxflow_service_v1.py)
        set_tests_properties(fluid_25d_external_worker_host fluid_25d_external_worker_build_helpers
            PROPERTIES LABELS "unit;fluid;backend" TIMEOUT 30)
        add_test(NAME fluid_25d_source_build_pin
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_build_synxflow_source_v1.py)
        set_tests_properties(fluid_25d_source_build_pin PROPERTIES LABELS "unit;fluid;dependency" TIMEOUT 30)
        set(CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS)
        if (CUBEY_HAS_LIBAV)
            list(APPEND CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS --video)
        endif()
        add_test(NAME fluid_25d_recording_converter
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_convert_synxflow_recording_v1.py)
        add_test(NAME fluid_25d_live_session_publisher
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_live_synxflow_session_v1.py)
        set_tests_properties(fluid_25d_live_session_publisher PROPERTIES LABELS "unit;fluid;stream" TIMEOUT 60)
        add_test(NAME fluid_25d_live_view_acceptance_helpers
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_live_view_acceptance_v1.py)
        set_tests_properties(fluid_25d_live_view_acceptance_helpers PROPERTIES LABELS "unit;fluid;stream" TIMEOUT 60)
        add_test(NAME fluid_25d_recording_gpu_immutability
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_recording_viewer_gpu_v1.py"
                --target "$<TARGET_FILE:cubey_project_fluid_25d>"
                ${CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS})
        set_tests_properties(fluid_25d_recording_gpu_immutability
            PROPERTIES LABELS "headless;gpu;recording" SKIP_RETURN_CODE 77 TIMEOUT 100)
        add_test(NAME fluid_25d_backend_startup_gpu
            COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_backend_startup_gpu_v1.py"
                --target "$<TARGET_FILE:cubey_project_fluid_25d>")
        set_tests_properties(fluid_25d_backend_startup_gpu
            PROPERTIES LABELS "headless;gpu;backend" SKIP_RETURN_CODE 77 TIMEOUT 180)
    endif()
    add_executable(cubey_project_fluid_25d_tests
        ../sim/fluid_25d/fluid_25d_diagnostics.cpp
        ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
        ../sim/fluid_25d/fluid_25d_oracle.cpp
        ../sim/fluid_25d/fluid_25d_mass_audit.cpp
        ../sim/fluid_25d/fluid_25d_natural_flow_recipe.cpp
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

    add_executable(cubey_project_fluid_25d_transport_benchmark
        ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
        ../sim/fluid_25d/fluid_25d_transport_benchmark.cpp
    )
    target_include_directories(
        cubey_project_fluid_25d_transport_benchmark
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
    )
    target_link_libraries(
        cubey_project_fluid_25d_transport_benchmark
        PRIVATE
            nlohmann_json::nlohmann_json
            cubey_project_fluid_common
            cubey_project_options
            cubey_project_warnings
    )
    add_test(
        NAME fluid_25d_transport_benchmark_self_test
        COMMAND cubey_project_fluid_25d_transport_benchmark --self-test
    )

    add_executable(cubey_project_fluid_25d_transport_study_benchmark
        ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
        ../sim/fluid_25d/fluid_25d_transport_study.cpp
        ../sim/fluid_25d/fluid_25d_transport_study_benchmark.cpp
    )
    target_include_directories(
        cubey_project_fluid_25d_transport_study_benchmark
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
    )
    target_link_libraries(
        cubey_project_fluid_25d_transport_study_benchmark
        PRIVATE
            nlohmann_json::nlohmann_json
            cubey_project_fluid_common
            cubey_project_options
            cubey_project_warnings
    )
    add_test(
        NAME fluid_25d_transport_study_benchmark_self_test
        COMMAND cubey_project_fluid_25d_transport_study_benchmark --self-test
    )

    add_executable(cubey_project_fluid_25d_geometry_study_benchmark
        ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
        ../sim/fluid_25d/fluid_25d_geometry_study.cpp
        ../sim/fluid_25d/fluid_25d_geometry_study_benchmark.cpp
    )
    target_include_directories(
        cubey_project_fluid_25d_geometry_study_benchmark
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
    )
    target_link_libraries(
        cubey_project_fluid_25d_geometry_study_benchmark
        PRIVATE
            nlohmann_json::nlohmann_json
            cubey_project_fluid_common
            cubey_project_options
            cubey_project_warnings
    )
    add_test(
        NAME fluid_25d_geometry_study_benchmark_self_test
        COMMAND cubey_project_fluid_25d_geometry_study_benchmark --self-test
    )

    cubey_add_png_smoke_test(
        fluid_25d_hillside_supply_gpu_controls
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-supply-controls.png"
        --frames 4 --grid-width 8 --grid-height 8
        --fluid25d-scenario dry-bed --fluid25d-solver finite-volume
        --fluid25d-fixed-delta-seconds 0.05 --fluid25d-substeps 4
        --fluid25d-gpu-oracle-validation --fluid25d-hillside-supply-gpu-controls
    )

    cubey_add_png_smoke_test(
        fluid_25d_hillside_rain_gpu_controls
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-rain-controls.png"
        --frames 4 --grid-width 8 --grid-height 8
        --fluid25d-scenario hillside-rain-study --fluid25d-solver finite-volume
        --fluid25d-cell-size-m 30 --fluid25d-fixed-delta-seconds 0.05 --fluid25d-substeps 4
        --fluid25d-rainfall-rate-mm-per-hour 100
        --fluid25d-gpu-oracle-validation --fluid25d-rain-study-gpu-controls
    )

    cubey_add_png_smoke_test(
        fluid_25d_motion_marker_gpu_controls
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-motion-marker-controls.png"
        --frames 1
        --grid-width 4
        --grid-height 4
        --fluid25d-scenario dry-bed
        --fluid25d-solver finite-volume
        --fluid25d-motion-marker-gpu-controls
    )

    set(CUBEY_FLUID_25D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-headless.png"
    )
    cubey_add_png_smoke_test(
        fluid_25d_headless_writes_png
        cubey_project_fluid_25d
        "${CUBEY_FLUID_25D_HEADLESS_SMOKE_OUTPUT}"
        --frames 8
        --width 512
        --height 256
    )
    cubey_add_png_stats_test(
        fluid_25d_headless_writes_png
        "${CUBEY_FLUID_25D_HEADLESS_SMOKE_OUTPUT}"
        0.18
        0.12
    )
    set(CUBEY_FLUID_25D_DIAGNOSTIC_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-diagnostics-headless.png"
    )
    cubey_add_png_smoke_test(
        fluid_25d_diagnostics_writes_png
        cubey_project_fluid_25d
        "${CUBEY_FLUID_25D_DIAGNOSTIC_HEADLESS_OUTPUT}"
        --frames 8
        --width 512
        --height 256
        --fluid25d-view diagnostics
        --debug-view terrain
    )
    cubey_add_png_stats_test(
        fluid_25d_diagnostics_writes_png
        "${CUBEY_FLUID_25D_DIAGNOSTIC_HEADLESS_OUTPUT}"
        0.20
        0.20
    )
    set(CUBEY_FLUID_25D_PROFILE_DIAGNOSTIC_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-profile-diagnostics-headless.png"
    )
    set(CUBEY_FLUID_25D_PROFILE_DIAGNOSTIC_SMOKE_PREFIX
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-profile-diagnostics"
    )
    add_test(
        NAME fluid_25d_profile_diagnostics_writes_metrics
        COMMAND
            /bin/sh -c
            "output=$1; prefix=$2; target=$3; rm -f \"$output\" \"$prefix\".frames.csv \"$prefix\".passes.csv \"$prefix\".metrics.csv \"$prefix\".trace.json \"$prefix\".summary.txt; out=$(env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u XDG_SESSION_TYPE -u XDG_RUNTIME_DIR -u DBUS_SESSION_BUS_ADDRESS \"$target\" --headless --frames 4 --width 64 --height 64 --profile-output \"$prefix\" --profile-diagnostics --profile-diagnostic-interval 1 --output \"$output\" 2>&1); status=$?; printf '%s\\n' \"$out\"; if printf '%s\\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then if printf '%s\\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; then exit 77; fi; exit \"$status\"; fi; test -s \"$output\"; test -s \"$prefix.metrics.csv\"; awk -F, 'NR > 1 && $2 == \"fluid_25d.water\" && $3 == \"total_water_volume_m3\" { seen = 1 } END { exit seen ? 0 : 1 }' \"$prefix.metrics.csv\""
            fluid_25d_profile_diagnostics_writes_metrics
            "${CUBEY_FLUID_25D_PROFILE_DIAGNOSTIC_SMOKE_OUTPUT}"
            "${CUBEY_FLUID_25D_PROFILE_DIAGNOSTIC_SMOKE_PREFIX}"
            "$<TARGET_FILE:cubey_project_fluid_25d>"
    )
    set_tests_properties(
        fluid_25d_profile_diagnostics_writes_metrics
        PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77
    )
    set_property(
        TEST fluid_25d_profile_diagnostics_writes_metrics
        APPEND PROPERTY LABELS "headless;gpu;diagnostics"
    )
    set_property(
        TEST fluid_25d_profile_diagnostics_writes_metrics
        APPEND
        PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
    )
    set(CUBEY_FLUID_25D_FINITE_VOLUME_PROFILE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-finite-volume-profile-headless.png"
    )
    set(CUBEY_FLUID_25D_FINITE_VOLUME_PROFILE_SMOKE_PREFIX
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-finite-volume-profile"
    )
    add_test(
        NAME fluid_25d_finite_volume_profile_diagnostics_writes_status
        COMMAND
            /bin/sh -c
            "output=$1; prefix=$2; target=$3; rm -f \"$output\" \"$prefix\".frames.csv \"$prefix\".passes.csv \"$prefix\".metrics.csv \"$prefix\".trace.json \"$prefix\".summary.txt; out=$(env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u XDG_SESSION_TYPE -u XDG_RUNTIME_DIR -u DBUS_SESSION_BUS_ADDRESS \"$target\" --headless --frames 4 --width 64 --height 64 --grid-width 256 --grid-height 128 --fluid25d-solver finite-volume --fluid25d-scenario river-catchment --profile-output \"$prefix\" --profile-diagnostics --profile-diagnostic-interval 1 --output \"$output\" 2>&1); status=$?; printf '%s\\n' \"$out\"; if printf '%s\\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then if printf '%s\\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; then exit 77; fi; exit \"$status\"; fi; test -s \"$output\"; test -s \"$prefix.metrics.csv\"; awk -F, 'NR > 1 && $2 == \"fluid_25d.solver\" && $3 == \"finite_volume_status_flags\" && $4 == \"0.000000\" { seen = 1 } END { exit seen ? 0 : 1 }' \"$prefix.metrics.csv\""
            fluid_25d_finite_volume_profile_diagnostics_writes_status
            "${CUBEY_FLUID_25D_FINITE_VOLUME_PROFILE_SMOKE_OUTPUT}"
            "${CUBEY_FLUID_25D_FINITE_VOLUME_PROFILE_SMOKE_PREFIX}"
            "$<TARGET_FILE:cubey_project_fluid_25d>"
    )
    set_tests_properties(
        fluid_25d_finite_volume_profile_diagnostics_writes_status
        PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77
    )
    set_property(
        TEST fluid_25d_finite_volume_profile_diagnostics_writes_status
        APPEND PROPERTY LABELS "headless;gpu;diagnostics;finite-volume"
    )
    set_property(
        TEST fluid_25d_finite_volume_profile_diagnostics_writes_status
        APPEND
        PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
    )
    set(CUBEY_FLUID_25D_FINITE_VOLUME_CFL_REJECTION_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-finite-volume-cfl-rejection.png"
    )
    add_test(
        NAME fluid_25d_finite_volume_cfl_rejection_fails_closed
        COMMAND
            /bin/sh -c
            "output=$1; target=$2; rm -f \"$output\"; out=$(env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u XDG_SESSION_TYPE -u XDG_RUNTIME_DIR -u DBUS_SESSION_BUS_ADDRESS \"$target\" --headless --frames 2 --width 64 --height 64 --grid-width 2 --grid-height 2 --fluid25d-solver finite-volume --fluid25d-scenario lake-at-rest --fluid25d-fixed-delta-seconds 1 --output \"$output\" 2>&1); status=$?; printf '%s\\n' \"$out\"; if printf '%s\\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -eq 0 ]; then exit 1; fi; if printf '%s\\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; then exit 77; fi; printf '%s\\n' \"$out\" | grep -q 'finite-volume GPU status is nonzero: flags=1'"
            fluid_25d_finite_volume_cfl_rejection_fails_closed
            "${CUBEY_FLUID_25D_FINITE_VOLUME_CFL_REJECTION_OUTPUT}"
            "$<TARGET_FILE:cubey_project_fluid_25d>"
    )
    set_tests_properties(
        fluid_25d_finite_volume_cfl_rejection_fails_closed
        PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77
    )
    set_property(
        TEST fluid_25d_finite_volume_cfl_rejection_fails_closed
        APPEND PROPERTY LABELS "headless;gpu;finite-volume;transaction"
    )
    set_property(
        TEST fluid_25d_finite_volume_cfl_rejection_fails_closed
        APPEND
        PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
    )

    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_lake_at_rest
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-lake-oracle.png"
        --frames 12
        --grid-width 12
        --grid-height 8
        --fluid25d-scenario lake-at-rest
        --fluid25d-gpu-oracle-validation
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_river
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-river-oracle.png"
        --frames 24
        --grid-width 24
        --grid-height 9
        --fluid25d-scenario river-catchment
        --fluid25d-gpu-oracle-validation
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_boundary_drain
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-boundary-drain-oracle.png"
        --frames 24
        --grid-width 8
        --grid-height 4
        --fluid25d-scenario boundary-drain-fixture
        --fluid25d-source-active-duration-seconds 0.05
        --fluid25d-gpu-oracle-validation
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_finite_volume_lake_at_rest
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-finite-volume-lake-oracle.png"
        --frames 12
        --grid-width 12
        --grid-height 8
        --fluid25d-solver finite-volume
        --fluid25d-scenario lake-at-rest
        --fluid25d-gpu-oracle-validation
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_finite_volume_source_sink
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-finite-volume-source-sink-oracle.png"
        --frames 24
        --grid-width 24
        --grid-height 9
        --fluid25d-solver finite-volume
        --fluid25d-scenario river-catchment
        --fluid25d-gpu-oracle-validation
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_finite_volume_dye_transport
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-finite-volume-dye-transport-oracle.png"
        --frames 20
        --width 640
        --height 360
        --grid-width 128
        --grid-height 64
        --fluid25d-solver finite-volume
        --fluid25d-scenario source-outlet-demo
        --fluid25d-fixed-delta-seconds 0.01
        --fluid25d-substeps 2
        --fluid25d-dye-pulse-start-seconds 0
        --fluid25d-dye-pulse-duration-seconds 0.1
        --fluid25d-gpu-oracle-validation
    )
    set(CUBEY_FLUID_25D_ZERO_SINK_TRACER_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-zero-sink-tracer.png"
    )
    set(CUBEY_FLUID_25D_ZERO_SINK_TRACER_PREFIX
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-zero-sink-tracer"
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_zero_sink_tracer
        cubey_project_fluid_25d
        "${CUBEY_FLUID_25D_ZERO_SINK_TRACER_OUTPUT}"
        --frames 120
        --grid-width 65
        --grid-height 33
        --fluid25d-cell-size-m 4
        --fluid25d-solver finite-volume
        --fluid25d-scenario sustained-headwaters-demo
        --fluid25d-fixed-delta-seconds 0.25
        --fluid25d-substeps 16
        --fluid25d-dye-pulse-start-seconds 0
        --fluid25d-dye-pulse-duration-seconds 1
        --fluid25d-gpu-oracle-validation
        --profile-output "${CUBEY_FLUID_25D_ZERO_SINK_TRACER_PREFIX}"
        --profile-diagnostics
        --profile-diagnostic-interval 1
    )
    add_test(
        NAME fluid_25d_gpu_zero_sink_tracer_ledger
        COMMAND
            /bin/sh -c
            "output=$1; prefix=$2; if ! test -s \"$output\"; then if test -f \"$output.skip\"; then exit 77; fi; exit 1; fi; test -s \"$prefix.metrics.csv\" || exit 1; awk -F, 'NR > 1 && $2 == \"fluid_25d.tracer\" && $3 == \"cumulative_sink_amount_m3\" { count++; if ($4 != 0) bad = 1 } NR > 1 && $2 == \"fluid_25d.tracer\" && $3 == \"cumulative_source_amount_m3\" && $4 > 0 { source = 1 } END { exit count == 120 && source && !bad ? 0 : 1 }' \"$prefix.metrics.csv\""
            fluid_25d_gpu_zero_sink_tracer_ledger
            "${CUBEY_FLUID_25D_ZERO_SINK_TRACER_OUTPUT}"
            "${CUBEY_FLUID_25D_ZERO_SINK_TRACER_PREFIX}"
    )
    set_tests_properties(
        fluid_25d_gpu_zero_sink_tracer_ledger
        PROPERTIES TIMEOUT 10 DEPENDS fluid_25d_gpu_zero_sink_tracer SKIP_RETURN_CODE 77
    )
    set_property(
        TEST fluid_25d_gpu_zero_sink_tracer_ledger
        APPEND PROPERTY LABELS "headless;gpu;diagnostics;finite-volume;tracer"
    )
    set(CUBEY_FLUID_25D_TRANSPORT_INSPECTION_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-transport-inspection-headless.png"
    )
    cubey_add_png_smoke_test(
        fluid_25d_transport_inspection_writes_png
        cubey_project_fluid_25d
        "${CUBEY_FLUID_25D_TRANSPORT_INSPECTION_OUTPUT}"
        --frames 80
        --width 640
        --height 360
        --grid-width 128
        --grid-height 64
        --fluid25d-solver finite-volume
        --fluid25d-scenario source-outlet-demo
        --fluid25d-catchment-view transport-inspection
        --fluid25d-fixed-delta-seconds 0.25
        --fluid25d-substeps 16
        --fluid25d-dye-pulse-start-seconds 0
        --fluid25d-dye-pulse-duration-seconds 5
    )
    cubey_add_png_stats_test(
        fluid_25d_transport_inspection_writes_png
        "${CUBEY_FLUID_25D_TRANSPORT_INSPECTION_OUTPUT}"
        0.18
        0.12
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_finite_volume_boundary_drain
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-finite-volume-boundary-drain-oracle.png"
        --frames 24
        --grid-width 8
        --grid-height 4
        --fluid25d-solver finite-volume
        --fluid25d-scenario boundary-drain-fixture
        --fluid25d-source-active-duration-seconds 0.05
        --fluid25d-gpu-oracle-validation
    )
endif()
