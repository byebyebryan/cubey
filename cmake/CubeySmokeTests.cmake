function(cubey_add_windowed_smoke_test name target success_pattern)
    add_test(
        NAME "${name}"
        COMMAND
            /bin/sh -c
            "target=$1; pattern=$2; shift 2; out=$(\"$target\" \"$@\" --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -eq 0 ]; then printf '%s\n' \"$out\" | grep -q \"$pattern\"; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed\\|GLFW reports Vulkan is not supported'; fi"
            "${name}"
            "$<TARGET_FILE:${target}>"
            "${success_pattern}"
            ${ARGN}
    )
    set_tests_properties("${name}" PROPERTIES TIMEOUT 20)
endfunction()

function(cubey_add_png_smoke_test name target output_path)
    add_test(
        NAME "${name}"
        COMMAND
            /bin/sh -c
            "output=$1; target=$2; shift 2; rm -f \"$output\"; out=$(\"$target\" \"$@\" --width 64 --height 64 --output \"$output\" 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then printf '%s\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; exit; fi; test -s \"$output\"; sig=$(od -An -tx1 -N8 \"$output\" | tr -d ' \\n'); test \"$sig\" = \"89504e470d0a1a0a\""
            "${name}"
            "${output_path}"
            "$<TARGET_FILE:${target}>"
            ${ARGN}
    )
    set_tests_properties("${name}" PROPERTIES TIMEOUT 20)
    set_property(
        TEST "${name}"
        APPEND
        PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
    )
endfunction()

function(cubey_add_png_stats_test smoke_name output_path min_mean_luma min_luma_range)
    set(stats_name "${smoke_name}_stats")
    add_test(
        NAME "${stats_name}"
        COMMAND
            /bin/sh -c
            "output=$1; verifier=$2; min_mean=$3; min_range=$4; if ! test -s \"$output\"; then printf 'png_stats: skipped %s because no PNG was written\\n' \"$output\"; exit 0; fi; \"$verifier\" \"$output\" \"$min_mean\" \"$min_range\""
            "${stats_name}"
            "${output_path}"
            "$<TARGET_FILE:cubey_png_stats>"
            "${min_mean_luma}"
            "${min_luma_range}"
    )
    set_tests_properties("${stats_name}" PROPERTIES TIMEOUT 10 DEPENDS "${smoke_name}")
endfunction()

function(cubey_add_video_smoke_test name target output_path)
    add_test(
        NAME "${name}"
        COMMAND
            /bin/sh -c
            "output=$1; target=$2; shift 2; rm -f \"$output\"; out=$(\"$target\" \"$@\" --headless --capture video --frames 5 --fps 5 --width 64 --height 64 --output \"$output\" 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then printf '%s\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found'; exit; fi; test -s \"$output\"; strings \"$output\" | grep -q ftyp"
            "${name}"
            "${output_path}"
            "$<TARGET_FILE:${target}>"
            ${ARGN}
    )
    set_tests_properties("${name}" PROPERTIES TIMEOUT 20)
    set_property(
        TEST "${name}"
        APPEND
        PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
    )
endfunction()
