# Included from the Fluid frontend directory; source/binary paths stay frontend-owned.
add_executable(cubey_project_fluid_25d_transport_benchmark
    ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
    ../sim/fluid_25d/fluid_25d_transport_benchmark.cpp
)
target_include_directories(
    cubey_project_fluid_25d_transport_benchmark
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)
target_link_libraries(
    cubey_project_fluid_25d_transport_benchmark
    PRIVATE
        nlohmann_json::nlohmann_json
        cubey_project_fluid_common
        cubey_project_options
        cubey_project_warnings
)
add_test(
    NAME fluid_25d_transport_benchmark_self_test
    COMMAND cubey_project_fluid_25d_transport_benchmark --self-test
)

add_executable(cubey_project_fluid_25d_transport_study_benchmark
    ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
    ../sim/fluid_25d/fluid_25d_transport_study.cpp
    ../sim/fluid_25d/fluid_25d_transport_study_benchmark.cpp
)
target_include_directories(
    cubey_project_fluid_25d_transport_study_benchmark
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)
target_link_libraries(
    cubey_project_fluid_25d_transport_study_benchmark
    PRIVATE
        nlohmann_json::nlohmann_json
        cubey_project_fluid_common
        cubey_project_options
        cubey_project_warnings
)
add_test(
    NAME fluid_25d_transport_study_benchmark_self_test
    COMMAND cubey_project_fluid_25d_transport_study_benchmark --self-test
)

add_executable(cubey_project_fluid_25d_geometry_study_benchmark
    ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
    ../sim/fluid_25d/fluid_25d_geometry_study.cpp
    ../sim/fluid_25d/fluid_25d_geometry_study_benchmark.cpp
)
target_include_directories(
    cubey_project_fluid_25d_geometry_study_benchmark
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)
target_link_libraries(
    cubey_project_fluid_25d_geometry_study_benchmark
    PRIVATE
        nlohmann_json::nlohmann_json
        cubey_project_fluid_common
        cubey_project_options
        cubey_project_warnings
)
add_test(
    NAME fluid_25d_geometry_study_benchmark_self_test
    COMMAND cubey_project_fluid_25d_geometry_study_benchmark --self-test
)
