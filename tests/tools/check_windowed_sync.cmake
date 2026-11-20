unset(ENV{WAYLAND_DISPLAY})
set(ENV{XDG_SESSION_TYPE} x11)
string(RANDOM LENGTH 16 run_id)
set(profile_dir "${CMAKE_CURRENT_BINARY_DIR}/windowed-sync-${run_id}")
file(MAKE_DIRECTORY "${profile_dir}")
execute_process(
    COMMAND "${XVFB_RUN}" -a "${APP}" --width 160 --height 96 --frames "${FRAMES}"
        --require-validation --profile-output "${profile_dir}/frame"
    RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log
)
message("${log}")
if (EXISTS "${profile_dir}/frame.summary.txt")
    file(STRINGS "${profile_dir}/frame.summary.txt" frame_count LIMIT_COUNT 1)
endif()
if (NOT result STREQUAL "0" OR NOT log MATCHES "cubey_window_backend: x11"
    OR NOT frame_count STREQUAL "frames: ${FRAMES}"
    OR log MATCHES "vulkan validation (warning|error):")
    message(FATAL_ERROR "Expected ${FRAMES} validation-clean X11 frames from ${APP} (exit ${result})")
endif()
file(REMOVE_RECURSE "${profile_dir}")
