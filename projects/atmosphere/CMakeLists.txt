cubey_add_app_target(
    cubey_project_atmosphere
    OUTPUT_NAME atmosphere
    SHADER_DEFINE CUBEY_ATMOSPHERE_SHADER_DIR
    WINDOWED_SMOKE_TEST atmosphere_reaches_glfw
    WINDOWED_SMOKE_PATTERN "atmosphere: .*rendering atmosphere project"
    SOURCES
        atmosphere_app.cpp
        lunar_atlas.cpp
        night_sky_atlas.cpp
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
        lunar_atlas.cpp
        night_sky_atlas.cpp
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

    set(CUBEY_ATMOSPHERE_SOLAR_NOON_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-solar-noon.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_solar_noon_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_SOLAR_NOON_SMOKE_OUTPUT}"
        --headless
        --time-of-day-mode
        solar
        --time-hours
        12
        --day-of-year
        80
        --latitude-degrees
        30
    )

    set(CUBEY_ATMOSPHERE_TWILIGHT_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-twilight.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_twilight_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_TWILIGHT_SMOKE_OUTPUT}"
        --headless
        --time-of-day-mode
        solar
        --time-hours
        17.8
        --day-of-year
        80
        --latitude-degrees
        30
    )

    set(CUBEY_ATMOSPHERE_NIGHT_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-night.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_night_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_NIGHT_SMOKE_OUTPUT}"
        --headless
        --atmosphere-preset
        night
    )

    set(CUBEY_ATMOSPHERE_MOONLIT_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-moonlit.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_moonlit_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_MOONLIT_SMOKE_OUTPUT}"
        --headless
        --atmosphere-preset
        moonlit-night
    )

    set(CUBEY_ATMOSPHERE_MOON_SURFACE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-moon-surface.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_moon_surface_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_MOON_SURFACE_SMOKE_OUTPUT}"
        --headless
        --debug-view
        moon-surface
    )

    set(CUBEY_ATMOSPHERE_MILKY_WAY_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-milky-way.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_milky_way_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_MILKY_WAY_SMOKE_OUTPUT}"
        --headless
        --debug-view
        milky-way
    )

    set(CUBEY_ATMOSPHERE_MILKY_WAY_DUST_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-milky-way-dust.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_milky_way_dust_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_MILKY_WAY_DUST_SMOKE_OUTPUT}"
        --headless
        --debug-view
        milky-way
        --milky-way-layer
        dust-tau
    )

    set(CUBEY_ATMOSPHERE_MILKY_WAY_HII_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/atmosphere-milky-way-hii.png"
    )
    cubey_add_png_smoke_test(
        atmosphere_milky_way_hii_writes_png
        cubey_project_atmosphere
        "${CUBEY_ATMOSPHERE_MILKY_WAY_HII_SMOKE_OUTPUT}"
        --headless
        --debug-view
        milky-way
        --milky-way-layer
        hii-emission
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
                "output=$1; target=$2; rm -f \"$output\"; out=$(\"$target\" --headless --capture video --frames 60 --fps 60 --width 64 --height 64 --time-of-day-mode solar --time-speed-hours-per-second 2 --output \"$output\" 2>&1); status=$?; printf '%s\n' \"$out\"; if printf '%s\n' \"$out\" | grep -q 'vulkan validation error'; then exit 1; fi; if [ \"$status\" -ne 0 ]; then printf '%s\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found|vkCreateInstance'; exit; fi; test -s \"$output\"; strings \"$output\" | grep -q ftyp"
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
