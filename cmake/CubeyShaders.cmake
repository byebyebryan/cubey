function(cubey_add_glsl_shaders target)
    set(options)
    set(one_value_args OUTPUT_DIR)
    set(multi_value_args DEPENDS INCLUDE_DIRS SOURCES)
    cmake_parse_arguments(CUBEY_SHADER "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (NOT CUBEY_SHADER_SOURCES)
        message(FATAL_ERROR "cubey_add_glsl_shaders requires SOURCES")
    endif()

    if (NOT CUBEY_SHADER_OUTPUT_DIR)
        set(CUBEY_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    endif()

    find_program(GLSLANG_VALIDATOR_EXE glslangValidator REQUIRED)

    set(shader_include_dirs ${CUBEY_SHADER_INCLUDE_DIRS})
    list(APPEND shader_include_dirs "${CMAKE_SOURCE_DIR}/shaders")
    if (CUBEY_FASTNOISE_LITE_GLSL_DIR)
        list(APPEND shader_include_dirs "${CUBEY_FASTNOISE_LITE_GLSL_DIR}")
    endif()
    list(REMOVE_DUPLICATES shader_include_dirs)

    set(include_args)
    foreach(include_dir IN LISTS shader_include_dirs)
        get_filename_component(include_path "${include_dir}" ABSOLUTE)
        list(APPEND include_args "-I${include_path}")
    endforeach()

    set(shader_dependencies)
    foreach(dependency IN LISTS CUBEY_SHADER_DEPENDS)
        get_filename_component(dependency_path "${dependency}" ABSOLUTE)
        list(APPEND shader_dependencies "${dependency_path}")
    endforeach()

    set(shader_outputs)
    foreach(shader IN LISTS CUBEY_SHADER_SOURCES)
        get_filename_component(shader_path "${shader}" ABSOLUTE)
        get_filename_component(shader_name "${shader}" NAME)
        set(shader_output "${CUBEY_SHADER_OUTPUT_DIR}/${shader_name}.spv")

        add_custom_command(
            OUTPUT "${shader_output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CUBEY_SHADER_OUTPUT_DIR}"
            COMMAND
                "${GLSLANG_VALIDATOR_EXE}" ${include_args}
                -V "${shader_path}" -o "${shader_output}"
            DEPENDS "${shader_path}" ${shader_dependencies}
            COMMENT "Compiling GLSL shader ${shader_name}"
            VERBATIM
        )
        list(APPEND shader_outputs "${shader_output}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${shader_outputs})
    add_dependencies(${target} ${target}_shaders)
endfunction()

function(cubey_forward_pbr_shader_sources out_var)
    set(
        forward_pbr_shaders
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_skybox.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_skybox.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere_reflection_prefilter.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_shadow_depth.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_shadow_depth.frag"
    )
    set(${out_var} ${forward_pbr_shaders} PARENT_SCOPE)
endfunction()
