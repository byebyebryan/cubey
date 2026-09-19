cubey_add_app_target(
    cubey_project_fluid_25d
    OUTPUT_NAME fluid_25d
    SHADER_DEFINE CUBEY_FLUID_25D_SHADER_DIR
    WINDOWED_SMOKE_TEST fluid_25d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fluid_25d: .*rendering River V0"
    SOURCES
        ../sim/fluid_25d/fluid_25d_app.cpp
        ../sim/fluid_25d/fluid_25d_commands.cpp
        ../sim/fluid_25d/fluid_25d_diagnostics.cpp
        ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
        ../sim/fluid_25d/fluid_25d_gpu_resources.cpp
        ../sim/fluid_25d/fluid_25d_oracle.cpp
        ../sim/fluid_25d/fluid_25d_ui.cpp
        main.cpp
    SHADERS
        ../sim/fluid_25d/shaders/fluid_25d.vert
        ../sim/fluid_25d/shaders/fluid_25d_render.frag
        ../sim/fluid_25d/shaders/fluid_25d_terrain.vert
        ../sim/fluid_25d/shaders/fluid_25d_terrain.frag
        ../sim/fluid_25d/shaders/fluid_25d_water.vert
        ../sim/fluid_25d/shaders/fluid_25d_water.frag
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
        ../sim/fluid_25d/shaders/fluid_25d_presentation_cue_reset.comp
        ../sim/fluid_25d/shaders/fluid_25d_presentation_cue_advect.comp
        ../sim/fluid_25d/shaders/fluid_25d_quiver_reset.comp
        ../sim/fluid_25d/shaders/fluid_25d_quiver_update.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)

target_include_directories(
    cubey_project_fluid_25d
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)

if (BUILD_TESTING)
    add_executable(cubey_project_fluid_25d_tests
        ../sim/fluid_25d/fluid_25d_diagnostics.cpp
        ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
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
