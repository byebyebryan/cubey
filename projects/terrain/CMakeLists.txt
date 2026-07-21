set(
    CUBEY_TERRAIN_DEFAULT_ASSET_DIR
    "${CMAKE_BINARY_DIR}/assets/terrain/default"
    CACHE PATH "Canonical generated terrain heightfield directory"
)
set(
    CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR
    "${CMAKE_BINARY_DIR}/_deps/terrain-diffusion-src"
    CACHE PATH "Build-local pinned terrain-diffusion checkout"
)
set(
    CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR
    "${CMAKE_BINARY_DIR}/assets/terrain/surface-study"
    CACHE PATH "Generated terrain surface-semantics climate companion directory"
)

add_custom_target(cubey_terrain_generate_default_asset
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CUBEY_TERRAIN_DIFFUSION_FALLBACK_ROOT=${CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR}"
        "CUBEY_TERRAIN_DIFFUSION_ENV=${CMAKE_BINARY_DIR}/_deps/terrain-diffusion-venv"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_terrain_diffusion_bake.sh"
        --default-asset
        --output-dir "${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}"
        --data-cache "${CMAKE_BINARY_DIR}/_deps/terrain-diffusion-data"
    BYPRODUCTS
        "${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}/heightfield.json"
        "${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}/elevation.f32"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating the pinned seed-0 terrain heightfield"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(cubey_terrain_generate_surface_study_asset
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CUBEY_TERRAIN_DIFFUSION_FALLBACK_ROOT=${CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR}"
        "CUBEY_TERRAIN_DIFFUSION_ENV=${CMAKE_BINARY_DIR}/_deps/terrain-diffusion-venv"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_terrain_diffusion_bake.sh"
        --surface-study-asset
        --output-dir "${CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR}"
        --data-cache "${CMAKE_BINARY_DIR}/_deps/terrain-diffusion-data"
    BYPRODUCTS
        "${CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR}/surface-fields.json"
        "${CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR}/climate.f32"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating the pinned terrain surface-semantics climate companion"
    USES_TERMINAL
    VERBATIM
)

add_library(cubey_project_terrain_runtime STATIC
    terrain_config.cpp
    terrain_height_source.cpp
    terrain_raster_climate_source.cpp
    terrain_raster_height_source.cpp
)
target_include_directories(
    cubey_project_terrain_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_runtime
    PUBLIC
        cubey::core
    PRIVATE
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

add_library(cubey_project_terrain_render_support STATIC
    terrain_backdrop_density.cpp
    terrain_backdrop_material.cpp
    terrain_backdrop_placement.cpp
    terrain_backdrop_product.cpp
    terrain_backdrop_profile.cpp
    terrain_directional_placement.cpp
    terrain_environment_gpu.cpp
    terrain_shadow.cpp
    terrain_surface_model.cpp
)
target_include_directories(
    cubey_project_terrain_render_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_render_support
    PUBLIC
        cubey_project_terrain_runtime
        cubey::render
    PRIVATE
        cubey_project_warnings
)

cubey_atmosphere_shader_depends(CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS)
cubey_cloud_layer_shader_sources(CUBEY_TERRAIN_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_TERRAIN_CLOUD_SHADER_DEPENDS)
cubey_forward_pbr_shader_depends(CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS)

cubey_add_app_target(
    cubey_project_terrain
    OUTPUT_NAME terrain
    SHADER_DEFINE CUBEY_TERRAIN_SHADER_DIR
    SOURCES
        main.cpp
        terrain_app.cpp
    SHADERS
        shaders/terrain_backdrop.vert
        shaders/terrain_backdrop_detail.frag
        shaders/terrain_backdrop_material.comp
        shaders/terrain_shadow_depth.vert
        shaders/terrain_stage_proxy.vert
        shaders/terrain_stage_proxy.frag
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere.frag"
        ${CUBEY_TERRAIN_CLOUD_SHADERS}
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        shaders/terrain_environment.glsl
        shaders/terrain_lighting.glsl
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/atmosphere_common.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
        ${CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS}
        ${CUBEY_TERRAIN_CLOUD_SHADER_DEPENDS}
        ${CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS}
)
target_compile_definitions(
    cubey_project_terrain
    PRIVATE
        CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD="${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}"
)
target_link_libraries(
    cubey_project_terrain
    PRIVATE
        cubey_project_terrain_render_support
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_config_tests terrain_config_tests.cpp)
    target_link_libraries(
        cubey_project_terrain_config_tests
        PRIVATE cubey_project_terrain_runtime cubey_project_warnings
    )
    add_test(NAME terrain_config_tests COMMAND cubey_project_terrain_config_tests)

    add_executable(
        cubey_project_terrain_raster_height_source_tests
        terrain_raster_height_source_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_raster_height_source_tests
        PRIVATE
            cubey_project_terrain_runtime
            cubey_project_warnings
            nlohmann_json::nlohmann_json
    )
    add_test(
        NAME terrain_raster_height_source_tests
        COMMAND cubey_project_terrain_raster_height_source_tests
    )

    add_executable(
        cubey_project_terrain_raster_climate_source_tests
        terrain_raster_climate_source_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_raster_climate_source_tests
        PRIVATE
            cubey_project_terrain_runtime
            cubey_project_warnings
            nlohmann_json::nlohmann_json
    )
    add_test(
        NAME terrain_raster_climate_source_tests
        COMMAND cubey_project_terrain_raster_climate_source_tests
    )

    add_executable(
        cubey_project_terrain_backdrop_product_tests
        terrain_backdrop_product_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_backdrop_product_tests
        PRIVATE cubey_project_terrain_render_support cubey_project_warnings
    )
    add_test(
        NAME terrain_backdrop_product_tests
        COMMAND cubey_project_terrain_backdrop_product_tests
    )

    add_executable(
        cubey_project_terrain_directional_placement_tests
        terrain_directional_placement_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_directional_placement_tests
        PRIVATE cubey_project_terrain_render_support cubey_project_warnings
    )
    add_test(
        NAME terrain_directional_placement_tests
        COMMAND cubey_project_terrain_directional_placement_tests
    )

    add_executable(
        cubey_project_terrain_backdrop_placement_tests
        terrain_backdrop_placement_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_backdrop_placement_tests
        PRIVATE cubey_project_terrain_render_support cubey_project_warnings
    )
    add_test(
        NAME terrain_backdrop_placement_tests
        COMMAND cubey_project_terrain_backdrop_placement_tests
    )

    add_executable(cubey_project_terrain_shadow_tests terrain_shadow_tests.cpp)
    target_link_libraries(
        cubey_project_terrain_shadow_tests
        PRIVATE cubey_project_terrain_render_support cubey_project_warnings
    )
    add_test(NAME terrain_shadow_tests COMMAND cubey_project_terrain_shadow_tests)

    add_executable(cubey_project_terrain_surface_model_tests terrain_surface_model_tests.cpp)
    target_link_libraries(
        cubey_project_terrain_surface_model_tests
        PRIVATE cubey_project_terrain_render_support cubey_project_warnings
    )
    add_test(NAME terrain_surface_model_tests COMMAND cubey_project_terrain_surface_model_tests)

    set_tests_properties(
        terrain_config_tests
        terrain_raster_climate_source_tests
        terrain_raster_height_source_tests
        terrain_backdrop_product_tests
        terrain_backdrop_placement_tests
        terrain_directional_placement_tests
        terrain_shadow_tests
        terrain_surface_model_tests
        PROPERTIES LABELS terrain TIMEOUT 120
    )
endif()
