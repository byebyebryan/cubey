function(cubey_add_glsl_shaders target)
    set(options)
    set(one_value_args OUTPUT_DIR)
    set(multi_value_args SOURCES)
    cmake_parse_arguments(CUBEY_SHADER "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (NOT CUBEY_SHADER_SOURCES)
        message(FATAL_ERROR "cubey_add_glsl_shaders requires SOURCES")
    endif()

    if (NOT CUBEY_SHADER_OUTPUT_DIR)
        set(CUBEY_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    endif()

    find_program(GLSLANG_VALIDATOR_EXE glslangValidator REQUIRED)

    set(shader_outputs)
    foreach(shader IN LISTS CUBEY_SHADER_SOURCES)
        get_filename_component(shader_path "${shader}" ABSOLUTE)
        get_filename_component(shader_name "${shader}" NAME)
        set(shader_output "${CUBEY_SHADER_OUTPUT_DIR}/${shader_name}.spv")

        add_custom_command(
            OUTPUT "${shader_output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CUBEY_SHADER_OUTPUT_DIR}"
            COMMAND "${GLSLANG_VALIDATOR_EXE}" -V "${shader_path}" -o "${shader_output}"
            DEPENDS "${shader_path}"
            COMMENT "Compiling GLSL shader ${shader_name}"
            VERBATIM
        )
        list(APPEND shader_outputs "${shader_output}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${shader_outputs})
    add_dependencies(${target} ${target}_shaders)
endfunction()
