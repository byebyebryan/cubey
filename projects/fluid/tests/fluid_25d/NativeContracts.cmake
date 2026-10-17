# Included from the Fluid frontend directory; source/binary paths stay frontend-owned.
add_executable(cubey_project_fluid_25d_terrain_alignment_tests
    ../sim/fluid_25d/fluid_25d_terrain_alignment_tests.cpp)
target_link_libraries(cubey_project_fluid_25d_terrain_alignment_tests PRIVATE
    cubey::cubey cubey_project_options cubey_project_warnings)
add_test(NAME fluid_25d_terrain_alignment COMMAND cubey_project_fluid_25d_terrain_alignment_tests)
set_tests_properties(fluid_25d_terrain_alignment PROPERTIES LABELS "unit;terrain;fluid")
add_executable(cubey_project_fluid_25d_terrain_demo_tests
    ../sim/fluid_25d/fluid_25d_terrain_demo.cpp
    ../sim/fluid_25d/fluid_25d_terrain_demo_tests.cpp)
target_link_libraries(cubey_project_fluid_25d_terrain_demo_tests PRIVATE
    cubey::cubey nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)
add_test(NAME fluid_25d_terrain_demo_contract COMMAND cubey_project_fluid_25d_terrain_demo_tests)
set_tests_properties(fluid_25d_terrain_demo_contract PROPERTIES LABELS "unit;fluid;backend")
add_executable(cubey_project_fluid_25d_daylight_gpu_tests
    ../sim/fluid_25d/fluid_25d_daylight_gpu_tests.cpp)
target_link_libraries(cubey_project_fluid_25d_daylight_gpu_tests PRIVATE
    cubey::cubey cubey_project_options cubey_project_warnings)
target_compile_definitions(cubey_project_fluid_25d_daylight_gpu_tests PRIVATE
    CUBEY_FLUID25D_DAYLIGHT_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-shaders")
cubey_add_glsl_shaders(cubey_project_fluid_25d_daylight_gpu_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-shaders"
    SOURCES "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_daylight.comp"
            ../sim/fluid_25d/shaders/fluid_25d_daylight_controls.comp
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_daylight.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere/sky_background_direction.glsl")
add_test(NAME fluid_25d_daylight_gpu COMMAND cubey_project_fluid_25d_daylight_gpu_tests)
set_tests_properties(fluid_25d_daylight_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
add_executable(cubey_project_fluid_25d_terrain_surface_tests
    ../sim/fluid_25d/fluid_25d_terrain_surface_tests.cpp)
target_link_libraries(cubey_project_fluid_25d_terrain_surface_tests PRIVATE
    cubey_fluid25d_terrain_surface cubey_project_options cubey_project_warnings)
add_test(NAME fluid_25d_terrain_surface COMMAND cubey_project_fluid_25d_terrain_surface_tests)
set_tests_properties(fluid_25d_terrain_surface PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
add_executable(cubey_project_fluid_25d_surface_gradient_gpu_tests
    ../sim/fluid_25d/fluid_25d_surface_gradient_gpu_tests.cpp)
target_link_libraries(cubey_project_fluid_25d_surface_gradient_gpu_tests PRIVATE
    cubey::cubey cubey_project_options cubey_project_warnings)
target_compile_definitions(cubey_project_fluid_25d_surface_gradient_gpu_tests PRIVATE
    CUBEY_FLUID25D_NORMAL_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-shaders")
cubey_add_glsl_shaders(cubey_project_fluid_25d_surface_gradient_gpu_tests
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-shaders"
    SOURCES ../sim/fluid_25d/shaders/fluid_25d_surface_gradient_test.comp
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_surface_gradient.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_water_detail.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_water_agitation.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_water_rapids.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_bank_edge.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_bspline_surface.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_terrain_wetness.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_surface_sampling.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_whitewater_motion.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/random.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/operators.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/cubey/terrain/surface_patch/fluid_25d_water_film.glsl")
add_test(NAME fluid_25d_surface_gradient_gpu COMMAND cubey_project_fluid_25d_surface_gradient_gpu_tests)
set_tests_properties(fluid_25d_surface_gradient_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
add_executable(cubey_project_fluid_25d_external_session_tests
    ../sim/fluid_25d/fluid_25d_backend_contract.cpp
    ../sim/fluid_25d/fluid_25d_external_session.cpp
    ../sim/fluid_25d/fluid_25d_external_session_tests.cpp)
target_link_libraries(cubey_project_fluid_25d_external_session_tests
    PRIVATE cubey::cubey nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)
add_test(NAME fluid_25d_external_session
    COMMAND cubey_project_fluid_25d_external_session_tests)
set_tests_properties(fluid_25d_external_session PROPERTIES LABELS "unit;fluid;backend")
add_executable(cubey_project_fluid_25d_recording_tests
    ../sim/fluid_25d/fluid_25d_recording.cpp
    ../sim/fluid_25d/fluid_25d_recording_tests.cpp)
target_link_libraries(cubey_project_fluid_25d_recording_tests
    PRIVATE cubey::cubey cubey::host cubey_project_options cubey_project_warnings)
add_test(NAME fluid_25d_recording_contract
    COMMAND cubey_project_fluid_25d_recording_tests)
set_tests_properties(fluid_25d_recording_contract PROPERTIES LABELS "unit;fluid;recording")
add_executable(cubey_project_fluid_25d_backend_contract_tests
    ../sim/fluid_25d/fluid_25d_backend_contract.cpp
    ../sim/fluid_25d/fluid_25d_backend_contract_tests.cpp)
target_include_directories(
    cubey_project_fluid_25d_backend_contract_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)
target_link_libraries(cubey_project_fluid_25d_backend_contract_tests
    PRIVATE nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)
add_test(NAME fluid_25d_backend_contract
    COMMAND cubey_project_fluid_25d_backend_contract_tests)
set_tests_properties(fluid_25d_backend_contract PROPERTIES LABELS "unit;fluid;backend")
add_executable(cubey_project_fluid_25d_backend_adapters_tests
    ../sim/fluid_25d/fluid_25d_backend_contract.cpp
    ../sim/fluid_25d/fluid_25d_backend_adapters.cpp
    ../sim/fluid_25d/fluid_25d_recording.cpp
    ../sim/fluid_25d/fluid_25d_backend_adapters_tests.cpp)
target_include_directories(
    cubey_project_fluid_25d_backend_adapters_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)
target_link_libraries(cubey_project_fluid_25d_backend_adapters_tests
    PRIVATE
        cubey::cubey
        cubey::host
        nlohmann_json::nlohmann_json
        cubey_project_options
        cubey_project_warnings)
add_test(NAME fluid_25d_backend_adapters
    COMMAND cubey_project_fluid_25d_backend_adapters_tests)
set_tests_properties(fluid_25d_backend_adapters PROPERTIES LABELS "unit;fluid;backend")
