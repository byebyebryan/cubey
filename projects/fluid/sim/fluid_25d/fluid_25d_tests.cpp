#include "fluid_25d_test_cases.h"

#include <cstdio>
#include <exception>

#include <string_view>

namespace {
using namespace cubey::projects::fluid::fluid_25d::test_detail;
struct TestCase {
    std::string_view name;
    std::string_view domain;
    void (*run)();
};
constexpr TestCase kTests[] = {
    {"test_backend_selection_preserves_existing_modes", "config",
     test_backend_selection_preserves_existing_modes},
    {"test_external_session_freshness_and_typed_inputs", "config",
     test_external_session_freshness_and_typed_inputs},
    {"test_recording_cli_is_separate_from_physics", "config",
     test_recording_cli_is_separate_from_physics},
    {"test_rain_visual_cli", "presentation", test_rain_visual_cli},
    {"test_whitewater_sources_and_clock", "presentation", test_whitewater_sources_and_clock},
    {"test_bank_edge_material_contract", "presentation", test_bank_edge_material_contract},
    {"test_scenic_material_contract", "presentation", test_scenic_material_contract},
    {"test_bank_comparison_controls", "presentation", test_bank_comparison_controls},
    {"test_catchment_far_plane_geometry", "presentation", test_catchment_far_plane_geometry},
    {"test_config_defaults_and_parsing", "config", test_config_defaults_and_parsing},
    {"test_hillside_rain_study_config_and_cli", "config", test_hillside_rain_study_config_and_cli},
    {"test_hillside_supply_and_depth_cues", "presentation", test_hillside_supply_and_depth_cues},
    {"test_deterministic_scenarios", "scenarios", test_deterministic_scenarios},
    {"test_forcing_cube_footprints_and_shared_faces", "presentation",
     test_forcing_cube_footprints_and_shared_faces},
    {"test_sustained_headwaters_scenario_construction", "scenarios",
     test_sustained_headwaters_scenario_construction},
    {"test_sustained_headwaters_refined_scenario_construction", "scenarios",
     test_sustained_headwaters_refined_scenario_construction},
    {"test_sustained_headwaters_station_diagnostics", "diagnostics",
     test_sustained_headwaters_station_diagnostics},
    {"test_sustained_headwaters_cross_section_diagnostics", "diagnostics",
     test_sustained_headwaters_cross_section_diagnostics},
    {"test_terrain_case_ingestion", "scenarios", test_terrain_case_ingestion},
    {"test_hillside_rain_study_scenario_construction", "scenarios",
     test_hillside_rain_study_scenario_construction},
    {"test_natural_flow_recipe_import_and_validation", "scenarios",
     test_natural_flow_recipe_import_and_validation},
    {"test_hillside_flow_study_contract", "scenarios", test_hillside_flow_study_contract},
    {"test_hillside_spatial_diagnostics", "diagnostics", test_hillside_spatial_diagnostics},
    {"test_natural_flow_profile_diagnostics", "diagnostics", test_natural_flow_profile_diagnostics},
    {"test_terrain_water_protocol_construction", "scenarios",
     test_terrain_water_protocol_construction},
    {"test_mountain_source_outlet_field_construction", "scenarios",
     test_mountain_source_outlet_field_construction},
    {"test_profile_frame_slot_attribution", "diagnostics", test_profile_frame_slot_attribution},
    {"test_profile_diagnostic_metric_math", "diagnostics", test_profile_diagnostic_metric_math},
    {"test_rain_boundary_outflow_attribution", "diagnostics",
     test_rain_boundary_outflow_attribution},
    {"test_hillside_rain_study_control_and_flat_oracle", "numerics",
     test_hillside_rain_study_control_and_flat_oracle},
    {"test_hillside_rain_study_observations", "diagnostics", test_hillside_rain_study_observations},
    {"test_source_outlet_cross_section_diagnostic_math", "diagnostics",
     test_source_outlet_cross_section_diagnostic_math},
    {"test_source_outlet_spatial_diagnostic_math", "diagnostics",
     test_source_outlet_spatial_diagnostic_math},
    {"test_source_outlet_endpoint_shoulders", "scenarios", test_source_outlet_endpoint_shoulders},
    {"test_tracer_profile_diagnostic_metric_math", "diagnostics",
     test_tracer_profile_diagnostic_metric_math},
    {"test_dry_bed_stability", "numerics", test_dry_bed_stability},
    {"test_lake_at_rest", "numerics", test_lake_at_rest},
    {"test_dynamic_closed_domain_conservation", "numerics",
     test_dynamic_closed_domain_conservation},
    {"test_boundary_outflow_contract", "numerics", test_boundary_outflow_contract},
    {"test_boundary_mask_validation_and_helper", "numerics",
     test_boundary_mask_validation_and_helper},
    {"test_source_rate_scale_and_schedule", "numerics", test_source_rate_scale_and_schedule},
    {"test_windowed_pacing", "presentation", test_windowed_pacing},
    {"test_motion_marker_display_clock", "presentation", test_motion_marker_display_clock},
    {"test_hillside_dye_palette_mapping", "presentation", test_hillside_dye_palette_mapping},
    {"test_presentation_cue_contract", "presentation", test_presentation_cue_contract},
    {"test_retained_flux_inertia", "numerics", test_retained_flux_inertia},
    {"test_river_mass_and_positivity", "numerics", test_river_mass_and_positivity},
    {"test_finite_volume_dry_bed_and_uneven_lake_at_rest", "numerics",
     test_finite_volume_dry_bed_and_uneven_lake_at_rest},
    {"test_finite_volume_high_absolute_elevation_shallow_film", "numerics",
     test_finite_volume_high_absolute_elevation_shallow_film},
    {"test_finite_volume_symmetric_dam_break", "numerics", test_finite_volume_symmetric_dam_break},
    {"test_finite_volume_open_boundary_and_source_sink_ledgers", "numerics",
     test_finite_volume_open_boundary_and_source_sink_ledgers},
    {"test_finite_volume_sustained_headwaters_flow_control", "numerics",
     test_finite_volume_sustained_headwaters_flow_control},
    {"test_finite_volume_cfl_fails_closed", "numerics", test_finite_volume_cfl_fails_closed},
    {"test_dye_config_and_fixed_step_schedule", "config", test_dye_config_and_fixed_step_schedule},
    {"test_dye_zero_path_and_resting_concentration", "tracer",
     test_dye_zero_path_and_resting_concentration},
    {"test_sustained_headwaters_dye_pulse", "tracer", test_sustained_headwaters_dye_pulse},
    {"test_dye_advection_bounds_and_ledgers", "tracer", test_dye_advection_bounds_and_ledgers},
    {"test_dye_zero_sink_transfer_and_real_sink", "tracer",
     test_dye_zero_sink_transfer_and_real_sink},
    {"test_dye_open_boundary_ledger", "tracer", test_dye_open_boundary_ledger},
    {"test_dye_cfl_rejection_and_reset", "tracer", test_dye_cfl_rejection_and_reset},
    {"test_finite_volume_reference_conserved_storage", "tracer",
     test_finite_volume_reference_conserved_storage},
    {"test_mass_audit_cli_contract", "config", test_mass_audit_cli_contract},
    {"test_mass_audit_diagnostics", "diagnostics", test_mass_audit_diagnostics},
    {"test_finite_volume_gpu_candidate_commit_shader_contract", "tracer",
     test_finite_volume_gpu_candidate_commit_shader_contract},
};
} // namespace

int main(int argc, char** argv) {
    std::string_view domain;
    if (argc == 3 && std::string_view(argv[1]) == "--domain")
        domain = argv[2];
    else if (argc != 1) {
        std::fprintf(stderr, "usage: fluid_25d_tests [--domain <name>]\n");
        return 2;
    }
    std::size_t selected = 0;
    try {
        for (const auto& test : kTests) {
            if (argc != 1 && test.domain != domain)
                continue;
            ++selected;
            try {
                test.run();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "fluid_25d_tests/%s: %s\n", test.name.data(), error.what());
                return 1;
            }
        }
        if (selected == 0) {
            std::fprintf(stderr, "fluid_25d_tests: unknown or empty domain\n");
            return 2;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_25d_tests: %s\n", error.what());
        return 1;
    }
    std::printf("fluid_25d_tests: %zu cases passed\n", selected);
    return 0;
}
