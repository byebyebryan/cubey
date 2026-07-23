function(cubey_add_windowed_smoke_test name target success_pattern)
    list(FIND ARGN "--headless" explicit_headless_index)
    if (NOT explicit_headless_index EQUAL -1)
        message(FATAL_ERROR "${name}: windowed smoke arguments must not include --headless")
    endif()
    add_test(
        NAME "${name}"
        COMMAND
            /bin/sh -c
            "if [ \"\${CUBEY_ALLOW_WINDOWED_TESTS:-0}\" != 1 ]; then printf 'windowed smoke disabled; use the dev-windowed test preset or set CUBEY_ALLOW_WINDOWED_TESTS=1\n'; exit 77; fi; target=$1; pattern=$2; shift 2; skip_re='glfwInit failed|GLFW reports Vulkan is not supported|no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; out=$(\"$target\" --frames 1 --width 64 --height 64 \"$@\" 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -eq 0 ]; then printf '%s\n' \"$out\" | grep -q \"$pattern\"; exit $?; fi; if printf '%s\n' \"$out\" | grep -Eq \"$skip_re\"; then exit 77; fi; exit \"$status\""
            "${name}"
            "$<TARGET_FILE:${target}>"
            "${success_pattern}"
            ${ARGN}
    )
    set_tests_properties("${name}" PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
    set_property(TEST "${name}" APPEND PROPERTY LABELS "windowed;gpu;interactive")
endfunction()

function(cubey_label_tests label)
    foreach(test_name ${ARGN})
        if (NOT TEST "${test_name}")
            message(FATAL_ERROR "cubey_label_tests: unknown test '${test_name}'")
        endif()
        set_property(TEST "${test_name}" APPEND PROPERTY LABELS "${label}")
    endforeach()
endfunction()

function(cubey_add_png_smoke_test name target output_path)
    list(FIND ARGN "--headless" explicit_headless_index)
    if (NOT explicit_headless_index EQUAL -1)
        message(
            FATAL_ERROR
            "${name}: cubey_add_png_smoke_test selects --headless automatically"
        )
    endif()
    set(skip_marker "${output_path}.skip")
    add_test(
        NAME "${name}"
        COMMAND
            /bin/sh -c
            "output=$1; skip_marker=$2; target=$3; shift 3; skip_re='no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; rm -f \"$output\" \"$skip_marker\"; out=$(env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u XDG_SESSION_TYPE -u XDG_RUNTIME_DIR -u DBUS_SESSION_BUS_ADDRESS \"$target\" --headless --width 64 --height 64 \"$@\" --output \"$output\" 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then if printf '%s\n' \"$out\" | grep -Eq \"$skip_re\"; then touch \"$skip_marker\"; exit 77; fi; exit \"$status\"; fi; if ! printf '%s\n' \"$out\" | grep -q 'headless_png:'; then printf 'headless PNG marker missing\n'; exit 1; fi; test -s \"$output\"; sig=$(od -An -tx1 -N8 \"$output\" | tr -d ' \\n'); test \"$sig\" = \"89504e470d0a1a0a\""
            "${name}"
            "${output_path}"
            "${skip_marker}"
            "$<TARGET_FILE:${target}>"
            ${ARGN}
    )
    set_tests_properties("${name}" PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
    set_property(TEST "${name}" APPEND PROPERTY LABELS "headless;gpu")
    set_property(
        TEST "${name}"
        APPEND
        PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
    )
endfunction()

function(cubey_add_png_stats_test smoke_name output_path min_mean_luma min_luma_range)
    set(stats_name "${smoke_name}_stats")
    set(skip_marker "${output_path}.skip")
    add_test(
        NAME "${stats_name}"
        COMMAND
            /bin/sh -c
            "output=$1; skip_marker=$2; verifier=$3; min_mean=$4; min_range=$5; if ! test -s \"$output\"; then if test -f \"$skip_marker\"; then printf 'png_stats: skipped %s because smoke test skipped\\n' \"$output\"; exit 77; fi; printf 'png_stats: missing expected PNG %s\\n' \"$output\"; exit 1; fi; \"$verifier\" \"$output\" \"$min_mean\" \"$min_range\""
            "${stats_name}"
            "${output_path}"
            "${skip_marker}"
            "$<TARGET_FILE:cubey_png_stats>"
            "${min_mean_luma}"
            "${min_luma_range}"
    )
    set_tests_properties("${stats_name}" PROPERTIES TIMEOUT 10 DEPENDS "${smoke_name}" SKIP_RETURN_CODE 77)
    set_property(TEST "${stats_name}" APPEND PROPERTY LABELS "headless;artifact")
endfunction()

function(cubey_add_video_smoke_test name target output_path)
    list(FIND ARGN "--headless" explicit_headless_index)
    if (NOT explicit_headless_index EQUAL -1)
        message(
            FATAL_ERROR
            "${name}: cubey_add_video_smoke_test selects --headless automatically"
        )
    endif()
    add_test(
        NAME "${name}"
        COMMAND
            /bin/sh -c
            "output=$1; target=$2; shift 2; skip_re='no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; rm -f \"$output\"; out=$(env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u XDG_SESSION_TYPE -u XDG_RUNTIME_DIR -u DBUS_SESSION_BUS_ADDRESS \"$target\" --headless --capture video --frames 5 --fps 5 --width 64 --height 64 \"$@\" --output \"$output\" 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then if printf '%s\n' \"$out\" | grep -Eq \"$skip_re\"; then exit 77; fi; exit \"$status\"; fi; if ! printf '%s\n' \"$out\" | grep -q 'headless_video:'; then printf 'headless video marker missing\n'; exit 1; fi; test -s \"$output\"; strings \"$output\" | grep -q ftyp"
            "${name}"
            "${output_path}"
            "$<TARGET_FILE:${target}>"
            ${ARGN}
    )
    set_tests_properties("${name}" PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
    set_property(TEST "${name}" APPEND PROPERTY LABELS "headless;gpu")
    set_property(
        TEST "${name}"
        APPEND
        PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
    )
endfunction()
