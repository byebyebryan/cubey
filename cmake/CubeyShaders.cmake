function(cubey_add_glsl_shaders target)
    set(options)
    set(one_value_args OUTPUT_DIR)
    set(multi_value_args DEFINES DEPENDS INCLUDE_DIRS SOURCES)
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

    set(define_args)
    foreach(define IN LISTS CUBEY_SHADER_DEFINES)
        list(APPEND define_args "-D${define}")
    endforeach()

    set(shader_dependencies)
    foreach(dependency IN LISTS CUBEY_SHADER_DEPENDS)
        get_filename_component(dependency_path "${dependency}" ABSOLUTE)
        list(APPEND shader_dependencies "${dependency_path}")
    endforeach()
    list(REMOVE_DUPLICATES shader_dependencies)

    set(shader_outputs)
    foreach(shader IN LISTS CUBEY_SHADER_SOURCES)
        get_filename_component(shader_path "${shader}" ABSOLUTE)
        get_filename_component(shader_name "${shader}" NAME)
        set(shader_output "${CUBEY_SHADER_OUTPUT_DIR}/${shader_name}.spv")

        add_custom_command(
            OUTPUT "${shader_output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CUBEY_SHADER_OUTPUT_DIR}"
            COMMAND
                "${GLSLANG_VALIDATOR_EXE}" ${include_args} ${define_args}
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

function(cubey_shader_package_depends out_var)
    set(shader_package_depends ${ARGN})
    list(REMOVE_DUPLICATES shader_package_depends)
    set(${out_var} ${shader_package_depends} PARENT_SCOPE)
endfunction()

function(cubey_shared_shader_depends out_var)
    set(
        shared_shader_depends
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/debug.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/environment_lighting.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/lighting.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/reflection_prefilter.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/operators.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/random.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/view.glsl"
    )
    list(REMOVE_DUPLICATES shared_shader_depends)
    set(${out_var} ${shared_shader_depends} PARENT_SCOPE)
endfunction()

function(cubey_atmosphere_shader_depends out_var)
    cubey_shared_shader_depends(atmosphere_shared_shader_depends)
    set(
        atmosphere_shader_depends
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_night_sky.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_stars.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_sun.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_ground.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_debug.glsl"
        ${atmosphere_shared_shader_depends}
    )
    list(REMOVE_DUPLICATES atmosphere_shader_depends)
    set(${out_var} ${atmosphere_shader_depends} PARENT_SCOPE)
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

function(cubey_forward_pbr_shader_depends out_var)
    cubey_shared_shader_depends(forward_pbr_shared_shader_depends)
    cubey_atmosphere_shader_depends(forward_pbr_atmosphere_shader_depends)
    set(
        forward_pbr_shader_depends
        ${forward_pbr_shared_shader_depends}
        ${forward_pbr_atmosphere_shader_depends}
    )
    list(REMOVE_DUPLICATES forward_pbr_shader_depends)
    set(${out_var} ${forward_pbr_shader_depends} PARENT_SCOPE)
endfunction()

function(cubey_terrain_backdrop_shader_sources out_var)
    set(
        terrain_backdrop_shaders
        "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/terrain_backdrop.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/terrain_backdrop_detail.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/terrain_backdrop_material.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/terrain_shadow_depth.vert"
    )
    set(${out_var} ${terrain_backdrop_shaders} PARENT_SCOPE)
endfunction()

function(cubey_terrain_backdrop_shader_depends out_var)
    cubey_shared_shader_depends(terrain_backdrop_shared_shader_depends)
    cubey_atmosphere_shader_depends(terrain_backdrop_atmosphere_shader_depends)
    set(
        terrain_backdrop_shader_depends
        "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/terrain_environment.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/terrain_lighting.glsl"
        ${terrain_backdrop_shared_shader_depends}
        ${terrain_backdrop_atmosphere_shader_depends}
    )
    list(REMOVE_DUPLICATES terrain_backdrop_shader_depends)
    set(${out_var} ${terrain_backdrop_shader_depends} PARENT_SCOPE)
endfunction()

function(cubey_cloud_layer_shader_sources out_var)
    set(options)
    set(one_value_args COMPOSITE)
    set(multi_value_args)
    cmake_parse_arguments(CUBEY_CLOUD "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (NOT CUBEY_CLOUD_COMPOSITE)
        set(CUBEY_CLOUD_COMPOSITE "background")
    endif()

    if (CUBEY_CLOUD_COMPOSITE STREQUAL "background")
        set(cloud_composite_fragment
            "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_composite_background.frag"
        )
    elseif(CUBEY_CLOUD_COMPOSITE STREQUAL "background-depth")
        set(cloud_composite_fragment
            "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_composite_background_depth.frag"
        )
    else()
        message(FATAL_ERROR "unknown cloud composite shader mode: ${CUBEY_CLOUD_COMPOSITE}")
    endif()

    set(
        cloud_layer_shaders
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud.vert"
        "${cloud_composite_fragment}"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_blue_noise.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_march.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_environment_prefilter.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_planar_filter.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_shadow.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/surface_cloud_march.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_perlin_worley.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_temporal.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_weather.comp"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_worley.comp"
    )
    set(${out_var} ${cloud_layer_shaders} PARENT_SCOPE)
endfunction()

function(cubey_cloud_layer_shader_depends out_var)
    cubey_shared_shader_depends(cloud_layer_shared_shader_depends)
    set(
        cloud_layer_shader_depends
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_composite_post.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_noise_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_resolve_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_surface_density.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/cloud/cloud_weather_common.glsl"
        ${cloud_layer_shared_shader_depends}
    )
    list(REMOVE_DUPLICATES cloud_layer_shader_depends)
    set(${out_var} ${cloud_layer_shader_depends} PARENT_SCOPE)
endfunction()
