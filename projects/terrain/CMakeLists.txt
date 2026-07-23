set(
    CUBEY_TERRAIN_CACHE_ROOT
    "${CMAKE_SOURCE_DIR}/cache/terrain"
    CACHE PATH "Worktree-local terrain cache root"
)
set(
    CUBEY_TERRAIN_DEFAULT_ASSET_DIR
    "${CUBEY_TERRAIN_CACHE_ROOT}/sources/v1/default"
    CACHE PATH "Canonical generated terrain heightfield directory"
)
set(
    CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR
    "${CUBEY_TERRAIN_CACHE_ROOT}/tooling/v1/terrain-diffusion-src"
    CACHE PATH "Worktree-local pinned terrain-diffusion checkout"
)
set(
    CUBEY_TERRAIN_DIFFUSION_ENV_DIR
    "${CUBEY_TERRAIN_CACHE_ROOT}/tooling/v1/terrain-diffusion-venv"
    CACHE PATH "Worktree-local terrain-diffusion Python environment"
)
set(
    CUBEY_TERRAIN_DIFFUSION_DATA_DIR
    "${CUBEY_TERRAIN_CACHE_ROOT}/tooling/v1/terrain-diffusion-data"
    CACHE PATH "Worktree-local terrain-diffusion data cache"
)
set(
    CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR
    "${CUBEY_TERRAIN_CACHE_ROOT}/sources/v1/surface-study"
    CACHE PATH "Generated terrain surface-semantics climate companion directory"
)
set(
    CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSET_DIR
    "${CUBEY_TERRAIN_CACHE_ROOT}/sources/v1/climate-calibration"
    CACHE PATH "Generated cross-climate terrain calibration asset directory"
)

# Migrate only the former build-local defaults; explicit caller overrides remain authoritative.
if(CUBEY_TERRAIN_DEFAULT_ASSET_DIR STREQUAL "${CMAKE_BINARY_DIR}/assets/terrain/default")
    set(CUBEY_TERRAIN_DEFAULT_ASSET_DIR
        "${CUBEY_TERRAIN_CACHE_ROOT}/sources/v1/default"
        CACHE PATH "Canonical generated terrain heightfield directory" FORCE)
endif()
if(CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR STREQUAL
   "${CMAKE_BINARY_DIR}/_deps/terrain-diffusion-src")
    set(CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR
        "${CUBEY_TERRAIN_CACHE_ROOT}/tooling/v1/terrain-diffusion-src"
        CACHE PATH "Worktree-local pinned terrain-diffusion checkout" FORCE)
endif()
if(CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR STREQUAL
   "${CMAKE_BINARY_DIR}/assets/terrain/surface-study")
    set(CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR
        "${CUBEY_TERRAIN_CACHE_ROOT}/sources/v1/surface-study"
        CACHE PATH "Generated terrain surface-semantics climate companion directory" FORCE)
endif()
if(CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSET_DIR STREQUAL
   "${CMAKE_BINARY_DIR}/assets/terrain/climate-calibration")
    set(CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSET_DIR
        "${CUBEY_TERRAIN_CACHE_ROOT}/sources/v1/climate-calibration"
        CACHE PATH "Generated cross-climate terrain calibration asset directory" FORCE)
endif()

add_custom_target(cubey_terrain_generate_default_asset
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CUBEY_TERRAIN_DIFFUSION_FALLBACK_ROOT=${CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR}"
        "CUBEY_TERRAIN_DIFFUSION_ENV=${CUBEY_TERRAIN_DIFFUSION_ENV_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_terrain_diffusion_bake.sh"
        --default-asset
        --output-dir "${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}"
        --data-cache "${CUBEY_TERRAIN_DIFFUSION_DATA_DIR}"
    BYPRODUCTS
        "${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}/heightfield.json"
        "${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}/elevation.f32"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating the pinned seed-0 terrain heightfield"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(cubey_terrain_regenerate_default_asset
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CUBEY_TERRAIN_DIFFUSION_FALLBACK_ROOT=${CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR}"
        "CUBEY_TERRAIN_DIFFUSION_ENV=${CUBEY_TERRAIN_DIFFUSION_ENV_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_terrain_diffusion_bake.sh"
        --default-asset
        --force
        --output-dir "${CUBEY_TERRAIN_DEFAULT_ASSET_DIR}"
        --data-cache "${CUBEY_TERRAIN_DIFFUSION_DATA_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Regenerating the pinned seed-0 terrain heightfield"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(cubey_terrain_generate_surface_study_asset
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CUBEY_TERRAIN_DIFFUSION_FALLBACK_ROOT=${CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR}"
        "CUBEY_TERRAIN_DIFFUSION_ENV=${CUBEY_TERRAIN_DIFFUSION_ENV_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_terrain_diffusion_bake.sh"
        --surface-study-asset
        --output-dir "${CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR}"
        --data-cache "${CUBEY_TERRAIN_DIFFUSION_DATA_DIR}"
    BYPRODUCTS
        "${CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR}/surface-fields.json"
        "${CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR}/climate.f32"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating the pinned terrain surface-semantics climate companion"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(cubey_terrain_generate_climate_calibration_assets
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CUBEY_TERRAIN_DIFFUSION_FALLBACK_ROOT=${CUBEY_TERRAIN_DIFFUSION_SOURCE_DIR}"
        "CUBEY_TERRAIN_DIFFUSION_ENV=${CUBEY_TERRAIN_DIFFUSION_ENV_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_terrain_diffusion_bake.sh"
        --climate-calibration-assets
        --output-dir "${CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSET_DIR}"
        --data-cache "${CUBEY_TERRAIN_DIFFUSION_DATA_DIR}"
    BYPRODUCTS
        "${CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSET_DIR}/calibration-index.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating five pinned terrain climate calibration regions"
    USES_TERMINAL
    VERBATIM
)

add_library(cubey_project_terrain_runtime STATIC
    terrain_config.cpp
    terrain_raster_climate_source.cpp
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
        cubey::asset
        cubey::terrain
    PRIVATE
        cubey_project_warnings
        nlohmann_json::nlohmann_json
)

add_library(cubey_project_terrain_surface_support STATIC
    terrain_product_adapter.cpp
    terrain_surface_model.cpp
)
target_include_directories(
    cubey_project_terrain_surface_support
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_surface_support
    PUBLIC
        cubey_project_terrain_runtime
        cubey::terrain
    PRIVATE
        cubey_project_warnings
)

cubey_atmosphere_shader_depends(CUBEY_TERRAIN_ATMOSPHERE_SHADER_DEPENDS)
cubey_cloud_layer_shader_sources(CUBEY_TERRAIN_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_TERRAIN_CLOUD_SHADER_DEPENDS)
cubey_forward_pbr_shader_depends(CUBEY_TERRAIN_FORWARD_PBR_SHADER_DEPENDS)
cubey_terrain_backdrop_shader_sources(CUBEY_TERRAIN_BACKDROP_SHADERS)
cubey_terrain_backdrop_shader_depends(CUBEY_TERRAIN_BACKDROP_SHADER_DEPENDS)

cubey_add_app_target(
    cubey_project_terrain
    OUTPUT_NAME terrain
    SHADER_DEFINE CUBEY_TERRAIN_SHADER_DIR
    SOURCES
        main.cpp
        terrain_app.cpp
    SHADERS
        ${CUBEY_TERRAIN_BACKDROP_SHADERS}
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
        ${CUBEY_TERRAIN_BACKDROP_SHADER_DEPENDS}
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
        CUBEY_TERRAIN_DEFAULT_SURFACE_FIELDS="${CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR}"
        CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSETS="${CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSET_DIR}"
)
target_link_libraries(
    cubey_project_terrain
    PRIVATE
        cubey_project_terrain_surface_support
)

if (BUILD_TESTING)
    set(
        CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET
        "${CMAKE_SOURCE_DIR}/tests/assets/terrain/backdrop-smoke"
    )

    add_executable(cubey_project_terrain_config_tests terrain_config_tests.cpp)
    target_link_libraries(
        cubey_project_terrain_config_tests
        PRIVATE cubey_project_terrain_runtime cubey_project_warnings
    )
    add_test(NAME terrain_config_tests COMMAND cubey_project_terrain_config_tests)

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

    add_executable(cubey_project_terrain_product_adapter_tests terrain_product_adapter_tests.cpp)
    target_link_libraries(
        cubey_project_terrain_product_adapter_tests
        PRIVATE cubey_project_terrain_surface_support cubey_project_warnings
    )
    add_test(
        NAME terrain_product_adapter_tests
        COMMAND cubey_project_terrain_product_adapter_tests
    )

    add_executable(cubey_project_terrain_shadow_tests terrain_shadow_tests.cpp)
    target_link_libraries(
        cubey_project_terrain_shadow_tests
        PRIVATE
            cubey_project_terrain_surface_support
            cubey::render
            cubey_project_warnings
    )
    add_test(NAME terrain_shadow_tests COMMAND cubey_project_terrain_shadow_tests)

    add_executable(cubey_project_terrain_surface_model_tests terrain_surface_model_tests.cpp)
    target_link_libraries(
        cubey_project_terrain_surface_model_tests
        PRIVATE cubey_project_terrain_surface_support cubey_project_warnings
    )
    add_test(NAME terrain_surface_model_tests COMMAND cubey_project_terrain_surface_model_tests)

    set(
        CUBEY_TERRAIN_BACKDROP_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-backdrop-smoke.png"
    )
    cubey_add_png_smoke_test(
        terrain_backdrop_headless_writes_png
        cubey_project_terrain
        "${CUBEY_TERRAIN_BACKDROP_SMOKE_OUTPUT}"
        --headless
        --terrain-heightfield
        "${CUBEY_TERRAIN_BACKDROP_SMOKE_ASSET}"
        --terrain-placement
        selected
        --terrain-foreground-height
        200
    )
    cubey_add_png_stats_test(
        terrain_backdrop_headless_writes_png
        "${CUBEY_TERRAIN_BACKDROP_SMOKE_OUTPUT}"
        0.02
        0.05
    )

    set_tests_properties(
        terrain_config_tests
        terrain_raster_climate_source_tests
        terrain_product_adapter_tests
        terrain_shadow_tests
        terrain_surface_model_tests
        terrain_backdrop_headless_writes_png
        terrain_backdrop_headless_writes_png_stats
        PROPERTIES LABELS terrain TIMEOUT 120
    )
endif()
