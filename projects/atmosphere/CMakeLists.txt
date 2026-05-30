cubey_add_app_target(
    cubey_project_atmosphere
    OUTPUT_NAME atmosphere
    SHADER_DEFINE CUBEY_ATMOSPHERE_SHADER_DIR
    WINDOWED_SMOKE_TEST atmosphere_reaches_glfw
    WINDOWED_SMOKE_PATTERN "atmosphere: .*rendering atmosphere project"
    SOURCES
        atmosphere_app.cpp
        atmosphere_ui.cpp
        main.cpp
    SHADERS
        shaders/atmosphere.vert
        shaders/atmosphere.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_atmosphere_tests
        atmosphere_config_tests.cpp
    )
    target_include_directories(
        cubey_project_atmosphere_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(
        cubey_project_atmosphere_tests
        PRIVATE
            CUBEY_ATMOSPHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_atmosphere_tests
        PRIVATE
            cubey::core
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME atmosphere_config_tests COMMAND cubey_project_atmosphere_tests)

    set(CUBEY_ATMOSPHERE_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-headless.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_headless_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_HEADLESS_SMOKE_OUTPUT}"
        --headless
    )

    set(CUBEY_ATMOSPHERE_TRANSMITTANCE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-transmittance.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_transmittance_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_TRANSMITTANCE_SMOKE_OUTPUT}"
        --headless
        --debug-view
        transmittance
    )

    if (CUBEY_HAS_LIBAV)
        set(CUBEY_ATMOSPHERE_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-headless.mp4"
        )
        add_test(
            NAME atmosphere_headless_writes_video
            COMMAND
                /bin/sh -c
                "output=$1; target=$2; rm -f \"$output\"; out=$(\"$target\" --headless --capture video --frames 60 --fps 60 --width 64 --height 64 --output \"$output\" 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then printf '%s\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found|vkCreateInstance'; exit; fi; test -s \"$output\"; strings \"$output\" | grep -q ftyp"
                "atmosphere_headless_writes_video"
                "${CUBEY_ATMOSPHERE_HEADLESS_VIDEO_SMOKE_OUTPUT}"
                "$<TARGET_FILE:cubey_project_atmosphere>"
        )
        set_tests_properties(
            atmosphere_headless_writes_video
            PROPERTIES TIMEOUT 30
        )
        set_property(
            TEST atmosphere_headless_writes_video
            APPEND
            PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp"
        )
    endif()
endif()
