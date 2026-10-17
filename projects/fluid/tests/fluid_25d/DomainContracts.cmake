# Included from the Fluid frontend directory; source/binary paths stay frontend-owned.
add_executable(cubey_project_fluid_25d_tests
    ../sim/fluid_25d/fluid_25d_bank_comparison.cpp
    ../sim/fluid_25d/fluid_25d_diagnostics.cpp
    ../sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp
    ../sim/fluid_25d/fluid_25d_oracle.cpp
    ../sim/fluid_25d/fluid_25d_mass_audit.cpp
    ../sim/fluid_25d/fluid_25d_natural_flow_recipe.cpp
    ../sim/fluid_25d/fluid_25d_tests.cpp
    ../sim/fluid_25d/fluid_25d_config_tests.cpp
    ../sim/fluid_25d/fluid_25d_scenarios_tests.cpp
    ../sim/fluid_25d/fluid_25d_diagnostics_tests.cpp
    ../sim/fluid_25d/fluid_25d_numerics_tests.cpp
    ../sim/fluid_25d/fluid_25d_tracer_tests.cpp
    ../sim/fluid_25d/fluid_25d_presentation_tests.cpp
    ../sim/fluid_25d/fluid_25d_scenic_material.cpp
)
target_include_directories(
    cubey_project_fluid_25d_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)
target_link_libraries(
    cubey_project_fluid_25d_tests
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_fluid_common
        cubey_project_options
        cubey_project_warnings
)
add_test(NAME fluid_25d_tests COMMAND cubey_project_fluid_25d_tests)
# Alternative selectors over the same 64 cases; default dev runs the aggregate once.
foreach(domain config scenarios diagnostics numerics tracer presentation)
    add_test(NAME fluid_25d_${domain}_domain
        COMMAND cubey_project_fluid_25d_tests --domain ${domain})
    set_tests_properties(fluid_25d_${domain}_domain PROPERTIES LABELS "unit;fluid;fluid_domain")
endforeach()
