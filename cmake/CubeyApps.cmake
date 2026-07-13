function(cubey_add_app_target target)
    set(options)
    set(one_value_args OUTPUT_NAME SHADER_DEFINE WINDOWED_SMOKE_TEST WINDOWED_SMOKE_PATTERN)
    set(multi_value_args
        SOURCES SHADERS SHADER_DEFINES SHADER_INCLUDE_DIRS SHADER_DEPENDS WINDOWED_SMOKE_ARGS
    )
    cmake_parse_arguments(CUBEY_APP "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (NOT CUBEY_APP_SOURCES)
        message(FATAL_ERROR "cubey_add_app_target requires SOURCES")
    endif()

    add_executable(${target} ${CUBEY_APP_SOURCES})
    if (CUBEY_APP_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${CUBEY_APP_OUTPUT_NAME}")
    endif()

    target_link_libraries(
        ${target}
        PRIVATE
            cubey::cubey
            cubey::host
            cubey_project_warnings
    )

    if (CUBEY_APP_SHADER_DEFINE)
        set(shader_output_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        target_compile_definitions(
            ${target}
            PRIVATE
                ${CUBEY_APP_SHADER_DEFINE}="${shader_output_dir}"
        )
    endif()

    if (CUBEY_APP_SHADERS)
        if (NOT CUBEY_APP_SHADER_DEFINE)
            set(shader_output_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        endif()
        cubey_add_glsl_shaders(
            ${target}
            OUTPUT_DIR "${shader_output_dir}"
            DEFINES ${CUBEY_APP_SHADER_DEFINES}
            INCLUDE_DIRS ${CUBEY_APP_SHADER_INCLUDE_DIRS}
            DEPENDS ${CUBEY_APP_SHADER_DEPENDS}
            SOURCES ${CUBEY_APP_SHADERS}
        )
    endif()

    if (BUILD_TESTING AND CUBEY_APP_WINDOWED_SMOKE_TEST)
        if (NOT CUBEY_APP_WINDOWED_SMOKE_PATTERN)
            message(FATAL_ERROR "cubey_add_app_target windowed smoke tests require a pattern")
        endif()
        cubey_add_windowed_smoke_test(
            "${CUBEY_APP_WINDOWED_SMOKE_TEST}"
            ${target}
            "${CUBEY_APP_WINDOWED_SMOKE_PATTERN}"
            ${CUBEY_APP_WINDOWED_SMOKE_ARGS}
        )
    endif()
endfunction()
