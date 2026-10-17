cubey_atmosphere_shader_depends(CUBEY_FLUID25D_ATMOSPHERE_SHADER_DEPENDS)
# Consume foundation climate and surface semantics, not project-relative sources.
add_library(cubey_fluid25d_terrain_surface STATIC
    ../sim/fluid_25d/fluid_25d_terrain_surface.cpp)
target_link_libraries(cubey_fluid25d_terrain_surface PUBLIC cubey::cubey
    nlohmann_json::nlohmann_json PRIVATE cubey_project_options cubey_project_warnings)

include("${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d/Fluid25DApp.cmake")
cubey_add_fluid25d_app(cubey_project_fluid_25d fluid_25d
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
if(BUILD_TESTING)
    cubey_add_windowed_smoke_test(fluid_25d_reaches_glfw cubey_project_fluid_25d
        "fluid_25d: .*rendering River V0")
endif()

# Explicit read/control diagnostic only; no automatic native launch or CUDA dependency.
add_executable(cubey_project_fluid_25d_export_water_capture EXCLUDE_FROM_ALL
    ../sim/fluid_25d/fluid_25d_recording.cpp
    ../sim/fluid_25d/fluid_25d_export_water_capture.cpp)
target_link_libraries(cubey_project_fluid_25d_export_water_capture PRIVATE
    cubey::cubey nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)

add_executable(cubey_project_fluid_25d_terrain_alignment_probe EXCLUDE_FROM_ALL
    ../sim/fluid_25d/fluid_25d_recording.cpp
    ../sim/fluid_25d/fluid_25d_terrain_alignment_probe.cpp)
target_link_libraries(cubey_project_fluid_25d_terrain_alignment_probe PRIVATE
    cubey::cubey nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)

add_executable(cubey_project_fluid_25d_external_probe EXCLUDE_FROM_ALL
    ../sim/fluid_25d/fluid_25d_backend_contract.cpp
    ../sim/fluid_25d/fluid_25d_external_session.cpp
    ../sim/fluid_25d/fluid_25d_external_session_probe.cpp)
target_link_libraries(cubey_project_fluid_25d_external_probe
    PRIVATE cubey::cubey nlohmann_json::nlohmann_json cubey_project_options cubey_project_warnings)

if (BUILD_TESTING)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../tests/fluid_25d/NativeContracts.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../tests/fluid_25d/StudyContracts.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../tests/fluid_25d/DomainContracts.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../tests/fluid_25d/Benchmarks.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../tests/fluid_25d/GpuContracts.cmake")
endif()
