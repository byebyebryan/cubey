add_executable(cubey_example_headless_render
    headless_render_app.cpp
    main.cpp
)
set_target_properties(cubey_example_headless_render PROPERTIES OUTPUT_NAME headless_render)

target_link_libraries(
    cubey_example_headless_render
    PRIVATE
        cubey::cubey
        cubey_project_warnings
)

if (BUILD_TESTING)
    set(CUBEY_HEADLESS_RENDER_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/headless-render-smoke.png"
    )
    add_test(
        NAME headless_render_writes_png
        COMMAND
            /bin/sh -c
            "rm -f \"$1\"; out=$(\"$2\" --width 64 --height 64 --output \"$1\" 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -ne 0 ]; then printf '%s\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found|vkCreateInstance'; exit; fi; test -s \"$1\"; sig=$(od -An -tx1 -N8 \"$1\" | tr -d ' \\n'); test \"$sig\" = \"89504e470d0a1a0a\""
            headless_render_writes_png
            "${CUBEY_HEADLESS_RENDER_SMOKE_OUTPUT}"
            "$<TARGET_FILE:cubey_example_headless_render>"
    )
    set_tests_properties(headless_render_writes_png PROPERTIES TIMEOUT 10)
endif()
