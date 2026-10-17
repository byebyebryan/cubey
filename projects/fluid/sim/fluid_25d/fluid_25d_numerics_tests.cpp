#include "fluid_25d_test_support.h"

namespace cubey::projects::fluid::fluid_25d::test_detail {

void test_hillside_rain_study_control_and_flat_oracle() {
    using namespace cubey::projects::fluid::fluid_25d;
    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.cell_size_m = 10.0F;
            Fluid25DRainStudyControl control;
            static_cast<void>(control.prepare_fixed_step(invalid));
        },
        "rain control itself should reject non-native spacing before scheduling input");
    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.source_active_duration_seconds = 1.0F;
            Fluid25DRainStudyControl control;
            static_cast<void>(control.total_input_m3_per_s(invalid));
        },
        "rain control observation should reject incompatible timed forcing");
    Fluid25DConfig config = rain_study_test_config(8U, 8U, 36000.0F);
    config.fixed_delta_seconds = 0.05F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(
        Fluid25DScenario::DryBed, config.grid_width, config.grid_height, config.cell_size_m);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    apply_fluid_25d_hillside_rain_study_forcing(config, scenario);
    std::fill(scenario.boundary_outflow_face_mask.begin(),
              scenario.boundary_outflow_face_mask.end(), 0U);

    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    Fluid25DRainStudyControl control;
    const double area = static_cast<double>(config.cell_size_m) * config.cell_size_m;
    const double volume_per_on_step = static_cast<double>(config.rainfall_depth_rate_m_per_s) *
                                      config.fixed_delta_seconds * area *
                                      static_cast<double>(fluid_25d_cell_count(config));
    const auto step = [&](bool expected_enabled) {
        const float scale = control.prepare_fixed_step(config);
        require((scale == 1.0F) == expected_enabled && control.enabled() == expected_enabled,
                "rain On/Off should apply only when the next fixed step is prepared");
        const Fluid25DStepLedger ledger = oracle.step(scale);
        require_close(ledger.conservation_error_m3(), 0.0, 0.00001,
                      "closed flat rain oracle should conserve its source ledger");
    };

    step(true);
    control.queue_enabled(false);
    const double queued_volume = control.state().scheduled_volume_m3;
    require(control.queued_enabled() == false && control.enabled() &&
                control.state().scheduled_volume_m3 == queued_volume,
            "queued rain-off should not change the applied state before a fixed step");
    step(false);
    control.queue_enabled(true);
    step(true);
    step(true);
    require_close(control.state().cumulative_depth_m,
                  3.0 * static_cast<double>(config.rainfall_depth_rate_m_per_s) *
                      config.fixed_delta_seconds,
                  1.0e-12, "cumulative rain depth should include only applied-on steps");
    require_close(control.state().scheduled_volume_m3, 3.0 * volume_per_on_step, 0.00001,
                  "scheduled rain volume should include only applied-on fixed steps");
    require_close(oracle.total_water_volume_m3(), control.state().scheduled_volume_m3, 0.00001,
                  "flat closed rain should add the scheduled volume without lateral movement");
    require(std::all_of(oracle.velocity_m_per_s().begin(), oracle.velocity_m_per_s().end(),
                        [](const Fluid25DVelocity& velocity) {
                            return velocity.x_m_per_s == 0.0F && velocity.y_m_per_s == 0.0F;
                        }),
            "uniform rain on a flat closed bed should create no motion");

    control.queue_enabled(false);
    const double before_paused_advance = control.state().scheduled_volume_m3;
    Fluid25DWindowedPacing paused_pacing(config.fixed_delta_seconds, 1.0F);
    require(paused_pacing.advance(5.0, true).fixed_step_count == 0U &&
                control.queued_enabled() == false && control.enabled() &&
                control.state().scheduled_volume_m3 == before_paused_advance,
            "pause should preserve a queued rain change without consuming rain time");
    require(paused_pacing.advance(config.fixed_delta_seconds, false).fixed_step_count == 1U,
            "unpaused pacing should produce a normal fixed step");
    step(false);
    control.reset();
    require(control.enabled() && !control.queued_enabled().has_value() &&
                control.state().cumulative_depth_m == 0.0 &&
                control.state().scheduled_volume_m3 == 0.0 && control.state().completed_steps == 0U,
            "rain reset should return to dry time zero with rain enabled");
    require_close(control.applied_rate_mm_per_hour(config), 36000.0, 0.1,
                  "rain reset should restore the configured positive rain rate");

    const auto run_at_fps = [&](double fps) {
        Fluid25DConfig pacing_config = config;
        pacing_config.fixed_delta_seconds = 1.0F / 60.0F;
        validate_fluid_25d_config(pacing_config);
        Fluid25DWindowedPacing pacing(pacing_config.fixed_delta_seconds, 1.0F);
        Fluid25DRainStudyControl fps_control;
        const std::uint32_t frame_count = static_cast<std::uint32_t>(2.0 * fps);
        for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
            const std::uint32_t fixed_steps = pacing.advance(1.0 / fps, false).fixed_step_count;
            for (std::uint32_t i = 0U; i < fixed_steps; ++i) {
                if (fps_control.state().completed_steps == 10U)
                    fps_control.queue_enabled(false);
                if (fps_control.state().completed_steps == 20U)
                    fps_control.queue_enabled(true);
                static_cast<void>(fps_control.prepare_fixed_step(pacing_config));
            }
        }
        return fps_control.state();
    };
    const auto thirty_fps = run_at_fps(30.0);
    const auto sixty_fps = run_at_fps(60.0);
    require(thirty_fps.completed_steps == 120U &&
                thirty_fps.completed_steps == sixty_fps.completed_steps &&
                thirty_fps.cumulative_depth_m == sixty_fps.cumulative_depth_m &&
                thirty_fps.scheduled_volume_m3 == sixty_fps.scheduled_volume_m3 &&
                thirty_fps.enabled == sixty_fps.enabled,
            "rain event schedule should be fixed-step deterministic at 30 and 60 render FPS");
}

void test_dry_bed_stability() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(10, 6, Fluid25DScenario::DryBed);
    const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
        config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
    Fluid25DOracle oracle(config, scenario);
    for (int frame = 0; frame < 120; ++frame) {
        const Fluid25DStepLedger ledger = oracle.step();
        require_close(ledger.conservation_error_m3(), 0.0, kDepthToleranceM,
                      "dry-bed ledger should remain conserved");
    }
    require_close(oracle.total_water_volume_m3(), 0.0, kDepthToleranceM,
                  "dry-bed oracle should remain empty");
    require(std::all_of(oracle.wet_mask().begin(), oracle.wet_mask().end(),
                        [](std::uint8_t value) { return value == 0U; }),
            "dry-bed oracle should remain entirely dry");
    for (const Fluid25DFaceFlux& flux : oracle.outgoing_flux_m3_per_s()) {
        require(std::all_of(flux.begin(), flux.end(), [](float value) { return value == 0.0F; }),
                "dry-bed oracle should not generate outgoing flux");
    }
}

void test_lake_at_rest() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(12, 8, Fluid25DScenario::LakeAtRest);
    config.simulation_substeps = 3;
    validate_fluid_25d_config(config);
    const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
        config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
    Fluid25DOracle oracle(config, scenario);
    const std::vector<float> initial_depth = oracle.water_depth_m();
    for (int frame = 0; frame < 120; ++frame) {
        const Fluid25DStepLedger ledger = oracle.step();
        require_close(ledger.source_volume_m3, 0.0, kDepthToleranceM,
                      "lake-at-rest fixture should have no source volume");
        require_close(ledger.sink_volume_m3, 0.0, kDepthToleranceM,
                      "lake-at-rest fixture should have no sink volume");
        require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "lake-at-rest ledger should remain conserved");
    }
    for (std::size_t index = 0; index < initial_depth.size(); ++index) {
        require_close(oracle.water_depth_m()[index], initial_depth[index], kDepthToleranceM,
                      "uneven-terrain lake should remain at rest");
    }
    for (const Fluid25DFaceFlux& flux : oracle.outgoing_flux_m3_per_s()) {
        require(std::all_of(flux.begin(), flux.end(),
                            [](float value) { return std::abs(value) < 0.000001F; }),
                "lake-at-rest fixture should have zero face flux");
    }
}

void test_dynamic_closed_domain_conservation() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(8, 5, Fluid25DScenario::DryBed);
    config.simulation_substeps = 2;
    const Fluid25DScenarioData scenario = [&] {
        Fluid25DScenarioData dynamic = make_fluid_25d_scenario(
            config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
        const std::size_t center =
            fluid_25d_scenario_index(config.grid_width, config.grid_height, 3, 2);
        dynamic.initial_water_depth_m[center] = 0.8F;
        dynamic.initial_water_depth_m[center + 1U] = 0.15F;
        return dynamic;
    }();
    require(std::all_of(scenario.source_depth_rate_m_per_s.begin(),
                        scenario.source_depth_rate_m_per_s.end(),
                        [](float value) { return value == 0.0F; }),
            "closed dynamic fixture should have no source rates");
    require(std::all_of(scenario.sink_depth_rate_m_per_s.begin(),
                        scenario.sink_depth_rate_m_per_s.end(),
                        [](float value) { return value == 0.0F; }),
            "closed dynamic fixture should have no sink rates");
    Fluid25DOracle oracle(config, scenario);
    const double initial_volume_m3 = oracle.total_water_volume_m3();
    bool saw_dynamic_flux = false;
    for (int frame = 0; frame < 90; ++frame) {
        const Fluid25DStepLedger ledger = oracle.step();
        require_close(ledger.source_volume_m3, 0.0, kDepthToleranceM,
                      "closed dynamic fixture should have no source volume");
        require_close(ledger.sink_volume_m3, 0.0, kDepthToleranceM,
                      "closed dynamic fixture should have no sink volume");
        require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "closed dynamic fixture ledger should remain conserved");
        for (const Fluid25DFaceFlux& flux : oracle.outgoing_flux_m3_per_s()) {
            saw_dynamic_flux =
                saw_dynamic_flux || std::any_of(flux.begin(), flux.end(),
                                                [](float value) { return value > 0.000001F; });
        }
    }
    require(saw_dynamic_flux,
            "closed dynamic fixture should exercise nonzero persistent face flux");
    require_close(oracle.total_water_volume_m3(), initial_volume_m3, kVolumeToleranceM3,
                  "closed dynamic fixture should conserve total volume");
}

void test_boundary_outflow_contract() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(3U, 2U, Fluid25DScenario::DryBed);
    config.fixed_delta_seconds = 0.05F;
    config.simulation_substeps = 1U;
    config.minimum_wet_depth_m = 0.000001F;
    validate_fluid_25d_config(config);

    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    const std::size_t draining_cell = fluid_25d_scenario_index(3U, 2U, 2U, 1U);
    scenario.initial_water_depth_m[draining_cell] = 0.50F;
    scenario.boundary_outflow_face_mask[draining_cell] = kFluid25DBoundaryOutflowRight;

    Fluid25DOracle oracle(config, scenario);
    double previous_volume_m3 = oracle.total_water_volume_m3();
    double accumulated_boundary_m3 = 0.0;
    for (int frame = 0; frame < 40; ++frame) {
        const Fluid25DStepLedger ledger = oracle.step();
        require_close(ledger.source_volume_m3, 0.0, kDepthToleranceM,
                      "boundary-drain fixture should have no source volume");
        require_close(ledger.sink_volume_m3, 0.0, kDepthToleranceM,
                      "boundary-drain fixture should have no explicit sink volume");
        require(ledger.boundary_outflow_volume_m3 >= 0.0,
                "boundary outflow ledger must never record external inflow");
        require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "boundary outflow ledger should reconcile the removed water");
        const double current_volume_m3 = oracle.total_water_volume_m3();
        require(current_volume_m3 <= previous_volume_m3 + kVolumeToleranceM3,
                "an open boundary cell should drain monotonically");
        require(std::all_of(oracle.water_depth_m().begin(), oracle.water_depth_m().end(),
                            [](float value) { return std::isfinite(value) && value >= 0.0F; }),
                "open boundary drainage should keep depths finite and nonnegative");
        accumulated_boundary_m3 += ledger.boundary_outflow_volume_m3;
        previous_volume_m3 = current_volume_m3;
    }
    require(accumulated_boundary_m3 > 0.0,
            "a marked boundary face should record post-limiter outflow volume");

    Fluid25DScenarioData empty = scenario;
    std::fill(empty.initial_water_depth_m.begin(), empty.initial_water_depth_m.end(), 0.0F);
    Fluid25DOracle empty_oracle(config, empty);
    const Fluid25DStepLedger empty_ledger = empty_oracle.step();
    require_close(empty_ledger.boundary_outflow_volume_m3, 0.0, kDepthToleranceM,
                  "an open dry exterior must not introduce water");
    require_close(empty_oracle.total_water_volume_m3(), 0.0, kDepthToleranceM,
                  "an open dry exterior must retain an empty domain");
    for (const Fluid25DFaceFlux& flux : empty_oracle.outgoing_flux_m3_per_s()) {
        require(std::all_of(flux.begin(), flux.end(), [](float value) { return value == 0.0F; }),
                "a dry open boundary must not generate incoming or outgoing pipes");
    }
}

void test_boundary_mask_validation_and_helper() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(4U, 3U, Fluid25DScenario::DryBed);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    open_fluid_25d_all_outward_boundary_faces(scenario);
    const std::size_t lower_left = fluid_25d_scenario_index(4U, 3U, 0U, 0U);
    const std::size_t upper_right = fluid_25d_scenario_index(4U, 3U, 3U, 2U);
    const std::size_t interior = fluid_25d_scenario_index(4U, 3U, 1U, 1U);
    require(scenario.boundary_outflow_face_mask[lower_left] ==
                    (kFluid25DBoundaryOutflowLeft | kFluid25DBoundaryOutflowDown) &&
                scenario.boundary_outflow_face_mask[upper_right] ==
                    (kFluid25DBoundaryOutflowRight | kFluid25DBoundaryOutflowUp) &&
                scenario.boundary_outflow_face_mask[interior] == 0U,
            "all-outward helper should open only actual perimeter faces deterministically");
    static_cast<void>(Fluid25DOracle(config, scenario));

    Fluid25DScenarioData interior_bit = scenario;
    interior_bit.boundary_outflow_face_mask[interior] = kFluid25DBoundaryOutflowLeft;
    require_throws([&] { static_cast<void>(Fluid25DOracle(config, interior_bit)); },
                   "boundary masks should reject interior faces");
    Fluid25DScenarioData inward_bit = scenario;
    inward_bit.boundary_outflow_face_mask[lower_left] |= kFluid25DBoundaryOutflowRight;
    require_throws([&] { static_cast<void>(Fluid25DOracle(config, inward_bit)); },
                   "boundary masks should reject a non-outward edge face");
    Fluid25DScenarioData unknown_bit = scenario;
    unknown_bit.boundary_outflow_face_mask[lower_left] |= 1U << 12U;
    require_throws([&] { static_cast<void>(Fluid25DOracle(config, unknown_bit)); },
                   "boundary masks should reject unknown bits");
}

void test_source_rate_scale_and_schedule() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(2U, 2U, Fluid25DScenario::DryBed);
    config.fixed_delta_seconds = 0.25F;
    config.simulation_substeps = 1U;
    config.gravity_m_per_s2 = 1.0F;
    config.source_active_duration_seconds = 0.50F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    scenario.source_depth_rate_m_per_s[0] = 0.40F;
    Fluid25DOracle oracle(config, scenario);
    const Fluid25DStepLedger active = oracle.step(1.0F);
    require_close(active.source_volume_m3, 0.10, kLedgerToleranceM3,
                  "source scale one should record the full source volume");
    const double after_active_m3 = oracle.total_water_volume_m3();
    const Fluid25DStepLedger inactive = oracle.step(0.0F);
    require_close(inactive.source_volume_m3, 0.0, kDepthToleranceM,
                  "source scale zero should stop source volume exactly");
    require_close(oracle.total_water_volume_m3(), after_active_m3, kVolumeToleranceM3,
                  "a closed source-scale-zero step should not add water");
    require_throws([&] { static_cast<void>(oracle.step(-0.01F)); },
                   "source scale should reject negative values");
    require_throws([&] { static_cast<void>(oracle.step(std::numeric_limits<float>::infinity())); },
                   "source scale should reject nonfinite values");

    Fluid25DSourceRateSchedule schedule;
    require(schedule.source_rate_scale(config) == 1.0F && schedule.completed_steps() == 0U,
            "a fresh finite source schedule should begin active");
    require_close(schedule.elapsed_seconds(config), 0.0, kDepthToleranceM,
                  "a fresh source schedule should report zero elapsed time");
    schedule.advance_fixed_step();
    require(schedule.source_rate_scale(config) == 1.0F && schedule.completed_steps() == 1U,
            "the source should stay active strictly before the configured duration");
    require_close(schedule.elapsed_seconds(config), 0.25, kDepthToleranceM,
                  "elapsed time should advance by one configured fixed step");
    schedule.advance_fixed_step();
    require(schedule.source_rate_scale(config) == 0.0F,
            "the source should switch off at the configured fixed-step duration");
    require_close(schedule.elapsed_seconds(config), 0.50, kDepthToleranceM,
                  "elapsed time should describe the completed fixed-step count");
    schedule.reset();
    require(schedule.source_rate_scale(config) == 1.0F && schedule.completed_steps() == 0U,
            "reset should restart the source schedule without advancing simulated time");
    require_close(schedule.elapsed_seconds(config), 0.0, kDepthToleranceM,
                  "reset should return the elapsed time to zero");

    std::array<float, 3U> batch_source_scales{};
    for (float& source_rate_scale : batch_source_scales) {
        source_rate_scale = schedule.source_rate_scale(config);
        schedule.advance_fixed_step();
    }
    require(batch_source_scales == std::array<float, 3U>{1.0F, 1.0F, 0.0F},
            "batched fixed steps should evaluate source scale at each step boundary");

    Fluid25DConfig unrepresentable = config;
    unrepresentable.fixed_delta_seconds = std::numeric_limits<float>::max();
    require_throws(
        [&] { static_cast<void>(fluid_25d_elapsed_seconds(unrepresentable, 2U)); },
        "elapsed time should reject values that cannot be represented by the render timestamp");
}

void test_retained_flux_inertia() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(2, 2, Fluid25DScenario::DryBed);
    config.fixed_delta_seconds = 0.5F;
    config.simulation_substeps = 1;
    config.gravity_m_per_s2 = 2.5F;
    config.flow_damping_per_second = 0.2F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    scenario.terrain_height_m[0] = 0.0F;
    scenario.terrain_height_m[1] = 0.0F;
    scenario.terrain_height_m[2] = 10.0F;
    scenario.terrain_height_m[3] = 10.0F;
    scenario.initial_water_depth_m[0] = 1.0F;
    scenario.initial_water_depth_m[1] = 0.0F;
    Fluid25DOracle oracle(config, scenario);

    const std::size_t right_face = static_cast<std::size_t>(Fluid25DFace::Right);
    const std::size_t source_cell = 0;
    static_cast<void>(oracle.step());
    const float first_flux = oracle.outgoing_flux_m3_per_s()[source_cell][right_face];
    require(first_flux > 0.0F, "inertia fixture should start with downstream flux");
    require(oracle.water_depth_m()[1] > oracle.water_depth_m()[0],
            "inertia fixture should reverse the free-surface gradient");
    static_cast<void>(oracle.step());
    const float second_flux = oracle.outgoing_flux_m3_per_s()[source_cell][right_face];
    require(second_flux > 0.0F && second_flux < first_flux,
            "retained flux should decelerate but persist against a reversed gradient");
}

void test_river_mass_and_positivity() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(24, 9, Fluid25DScenario::RiverCatchment);
    // Keep one public step equal to one solver step here so the first ledger
    // can prove that River V0's initially dry sink removes no water before
    // the narrow seeded path reaches it.
    config.simulation_substeps = 1;
    const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
        config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
    Fluid25DOracle oracle(config, scenario);
    const double initial_volume_m3 = oracle.total_water_volume_m3();
    double source_volume_m3 = 0.0;
    double sink_volume_m3 = 0.0;
    double maximum_ledger_error_m3 = 0.0;
    bool saw_positive_downstream_flux = false;
    require(scenario.initial_water_depth_m[scenario.sink_cell] == 0.0F,
            "River V0 sink must remain dry at the start of the run");
    const Fluid25DStepLedger first_ledger = oracle.step();
    require_close(first_ledger.sink_volume_m3, 0.0, kDepthToleranceM,
                  "dry River V0 sink must remove no water before arrival");
    source_volume_m3 += first_ledger.source_volume_m3;
    sink_volume_m3 += first_ledger.sink_volume_m3;
    maximum_ledger_error_m3 =
        std::max(maximum_ledger_error_m3, std::abs(first_ledger.conservation_error_m3()));
    for (int frame = 1; frame < 2400; ++frame) {
        const Fluid25DStepLedger ledger = oracle.step();
        source_volume_m3 += ledger.source_volume_m3;
        sink_volume_m3 += ledger.sink_volume_m3;
        maximum_ledger_error_m3 =
            std::max(maximum_ledger_error_m3, std::abs(ledger.conservation_error_m3()));
        require(std::all_of(oracle.water_depth_m().begin(), oracle.water_depth_m().end(),
                            [](float value) { return std::isfinite(value) && value >= 0.0F; }),
                "river oracle should keep every depth finite and nonnegative");
        require(std::all_of(oracle.velocity_m_per_s().begin(), oracle.velocity_m_per_s().end(),
                            [](const Fluid25DVelocity& value) {
                                return std::isfinite(value.x_m_per_s) &&
                                       std::isfinite(value.y_m_per_s);
                            }),
                "river oracle should keep derived velocity finite");
        for (const Fluid25DFaceFlux& flux : oracle.outgoing_flux_m3_per_s()) {
            require(std::all_of(flux.begin(), flux.end(),
                                [](float value) { return std::isfinite(value) && value >= 0.0F; }),
                    "river oracle should keep every outgoing face flux finite and nonnegative");
        }
        const std::size_t source_cell = scenario.source_cell;
        if (oracle.outgoing_flux_m3_per_s()[source_cell]
                                           [static_cast<std::size_t>(Fluid25DFace::Right)] > 0.0F) {
            saw_positive_downstream_flux = true;
        }
    }
    require(saw_positive_downstream_flux,
            "river oracle should send a positive flux downstream from its source");
    require(sink_volume_m3 > 0.0,
            "river oracle should deliver water to the explicit downstream sink");
    require_close(oracle.total_water_volume_m3(),
                  initial_volume_m3 + source_volume_m3 - sink_volume_m3, kVolumeToleranceM3,
                  "river source/sink ledger should reconcile total stored volume");
    require(maximum_ledger_error_m3 < kLedgerToleranceM3,
            "river per-frame ledger error should stay below the explicit tolerance");
}

void test_finite_volume_dry_bed_and_uneven_lake_at_rest() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig dry_config = finite_volume_test_config(6U, 4U, Fluid25DScenario::DryBed);
    dry_config.fixed_delta_seconds = 0.01F;
    dry_config.simulation_substeps = 1U;
    dry_config.flow_damping_per_second = 0.0F;
    validate_fluid_25d_config(dry_config);
    const Fluid25DScenarioData dry_scenario = make_fluid_25d_scenario(
        dry_config.scenario, dry_config.grid_width, dry_config.grid_height, dry_config.cell_size_m);
    Fluid25DFiniteVolumeOracle dry_oracle(dry_config, dry_scenario);
    for (int step = 0; step < 40; ++step) {
        const Fluid25DStepLedger ledger = dry_oracle.step();
        require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "finite-volume dry uneven bed should remain conserved");
    }
    require_close(dry_oracle.total_water_volume_m3(), 0.0, kDepthToleranceM,
                  "finite-volume dry uneven bed should remain dry");
    require(std::all_of(dry_oracle.momentum_m2_per_s().begin(),
                        dry_oracle.momentum_m2_per_s().end(),
                        [](const Fluid25DMomentum& momentum) {
                            return momentum.x_m2_per_s == 0.0F && momentum.y_m2_per_s == 0.0F;
                        }),
            "finite-volume dry uneven bed should retain zero momentum");

    Fluid25DConfig lake_config = finite_volume_test_config(5U, 5U, Fluid25DScenario::DryBed);
    lake_config.fixed_delta_seconds = 0.01F;
    lake_config.simulation_substeps = 1U;
    lake_config.flow_damping_per_second = 0.0F;
    lake_config.minimum_wet_depth_m = 0.000001F;
    validate_fluid_25d_config(lake_config);
    Fluid25DScenarioData lake =
        make_fluid_25d_scenario(lake_config.scenario, lake_config.grid_width,
                                lake_config.grid_height, lake_config.cell_size_m);
    constexpr float surface_height_m = 0.25F;
    for (std::uint32_t y = 0U; y < lake.height; ++y) {
        for (std::uint32_t x = 0U; x < lake.width; ++x) {
            const float distance = static_cast<float>(std::abs(static_cast<int>(x) - 2) +
                                                      std::abs(static_cast<int>(y) - 2));
            const std::size_t index = fluid_25d_scenario_index(lake.width, lake.height, x, y);
            lake.terrain_height_m[index] = 0.15F * distance;
            lake.initial_water_depth_m[index] =
                std::max(0.0F, surface_height_m - lake.terrain_height_m[index]);
        }
    }
    Fluid25DFiniteVolumeOracle lake_oracle(lake_config, lake);
    const std::vector<float> initial_depth = lake_oracle.water_depth_m();
    for (int step = 0; step < 80; ++step) {
        const Fluid25DStepLedger ledger = lake_oracle.step();
        require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "finite-volume closed lake ledger should remain conserved");
    }
    for (std::size_t index = 0; index < initial_depth.size(); ++index) {
        require_close(lake_oracle.water_depth_m()[index], initial_depth[index], kDepthToleranceM,
                      "finite-volume hydrostatic reconstruction should preserve wet/dry lake rest");
        require_close(lake_oracle.momentum_m2_per_s()[index].x_m2_per_s, 0.0, kDepthToleranceM,
                      "finite-volume lake at rest should retain zero x momentum");
        require_close(lake_oracle.momentum_m2_per_s()[index].y_m2_per_s, 0.0, kDepthToleranceM,
                      "finite-volume lake at rest should retain zero y momentum");
    }
}

void test_finite_volume_high_absolute_elevation_shallow_film() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(3U, 3U, Fluid25DScenario::DryBed);
    config.fixed_delta_seconds = 0.01F;
    config.simulation_substeps = 1U;
    config.flow_damping_per_second = 0.0F;
    config.minimum_wet_depth_m = 0.000001F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    // At this elevation an f32 `h + z - z` loses a 1 mm film completely.
    // Difference-first hydrostatic reconstruction must still expose the wet
    // center to its dry same-bed neighbours.
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 100000.0F);
    const std::size_t center = fluid_25d_scenario_index(3U, 3U, 1U, 1U);
    scenario.initial_water_depth_m[center] = 0.001F;
    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    const double initial_volume_m3 = oracle.total_water_volume_m3();
    const Fluid25DStepLedger ledger = oracle.step();
    require(oracle.water_depth_m()[center] < scenario.initial_water_depth_m[center],
            "finite-volume shallow film must not disappear from high absolute terrain elevation");
    bool spread_to_neighbor = false;
    for (std::size_t index = 0; index < oracle.water_depth_m().size(); ++index) {
        spread_to_neighbor =
            spread_to_neighbor || (index != center && oracle.water_depth_m()[index] > 0.0F);
    }
    require(spread_to_neighbor,
            "finite-volume high-elevation shallow film must transport to a dry same-bed neighbour");
    require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                  "finite-volume high-elevation shallow-film ledger should remain conserved");
    require_close(oracle.total_water_volume_m3(), initial_volume_m3, kVolumeToleranceM3,
                  "finite-volume high-elevation shallow film should conserve closed-domain volume");
}

void test_finite_volume_symmetric_dam_break() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(9U, 9U, Fluid25DScenario::DryBed);
    config.fixed_delta_seconds = 0.002F;
    config.simulation_substeps = 1U;
    config.flow_damping_per_second = 0.0F;
    config.minimum_wet_depth_m = 0.000001F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    scenario.initial_water_depth_m[fluid_25d_scenario_index(9U, 9U, 4U, 4U)] = 1.0F;
    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    const double initial_volume_m3 = oracle.total_water_volume_m3();
    for (int step = 0; step < 120; ++step) {
        const Fluid25DStepLedger ledger = oracle.step();
        require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "finite-volume closed dam-break ledger should remain conserved");
        require(oracle.last_cfl_number() <= Fluid25DFiniteVolumeOracle::kTargetCfl,
                "finite-volume accepted dam-break substeps should satisfy the CFL target");
        require(
            std::all_of(oracle.water_depth_m().begin(), oracle.water_depth_m().end(),
                        [](float depth_m) { return std::isfinite(depth_m) && depth_m >= 0.0F; }),
            "finite-volume dam break should keep depth finite and nonnegative");
        for (std::size_t index = 0; index < oracle.water_depth_m().size(); ++index) {
            const float depth_m = oracle.water_depth_m()[index];
            if (depth_m <= config.minimum_wet_depth_m) {
                continue;
            }
            require_close(oracle.velocity_m_per_s()[index].x_m_per_s,
                          oracle.momentum_m2_per_s()[index].x_m2_per_s / depth_m, kDepthToleranceM,
                          "finite-volume velocity should derive from x momentum and depth");
            require_close(oracle.velocity_m_per_s()[index].y_m_per_s,
                          oracle.momentum_m2_per_s()[index].y_m2_per_s / depth_m, kDepthToleranceM,
                          "finite-volume velocity should derive from y momentum and depth");
        }
    }
    require_close(oracle.total_water_volume_m3(), initial_volume_m3, kVolumeToleranceM3,
                  "finite-volume closed dam break should conserve total volume");
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
            const std::size_t mirror_x = fluid_25d_scenario_index(
                config.grid_width, config.grid_height, config.grid_width - 1U - x, y);
            const std::size_t mirror_y = fluid_25d_scenario_index(
                config.grid_width, config.grid_height, x, config.grid_height - 1U - y);
            require_close(oracle.water_depth_m()[index], oracle.water_depth_m()[mirror_x],
                          kDepthToleranceM,
                          "finite-volume centered dam break should remain left-right symmetric");
            require_close(oracle.water_depth_m()[index], oracle.water_depth_m()[mirror_y],
                          kDepthToleranceM,
                          "finite-volume centered dam break should remain down-up symmetric");
        }
    }
}

void test_finite_volume_open_boundary_and_source_sink_ledgers() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig drain_config = finite_volume_test_config(3U, 3U, Fluid25DScenario::DryBed);
    drain_config.fixed_delta_seconds = 0.01F;
    drain_config.simulation_substeps = 1U;
    drain_config.flow_damping_per_second = 0.0F;
    drain_config.minimum_wet_depth_m = 0.000001F;
    validate_fluid_25d_config(drain_config);
    Fluid25DScenarioData drain =
        make_fluid_25d_scenario(drain_config.scenario, drain_config.grid_width,
                                drain_config.grid_height, drain_config.cell_size_m);
    std::fill(drain.terrain_height_m.begin(), drain.terrain_height_m.end(), 0.0F);
    const std::size_t edge = fluid_25d_scenario_index(3U, 3U, 2U, 1U);
    drain.initial_water_depth_m[edge] = 0.50F;
    drain.boundary_outflow_face_mask[edge] = kFluid25DBoundaryOutflowRight;
    Fluid25DFiniteVolumeOracle drain_oracle(drain_config, drain);
    double previous_volume_m3 = drain_oracle.total_water_volume_m3();
    double boundary_outflow_m3 = 0.0;
    for (int step = 0; step < 60; ++step) {
        const Fluid25DStepLedger ledger = drain_oracle.step();
        require(ledger.boundary_outflow_volume_m3 >= 0.0,
                "finite-volume open boundary must never report external inflow");
        require_close(ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "finite-volume open boundary ledger should reconcile removed water");
        const double volume_m3 = drain_oracle.total_water_volume_m3();
        require(volume_m3 <= previous_volume_m3 + kVolumeToleranceM3,
                "finite-volume dry exterior should drain monotonically");
        boundary_outflow_m3 += ledger.boundary_outflow_volume_m3;
        previous_volume_m3 = volume_m3;
    }
    require(boundary_outflow_m3 > 0.0,
            "finite-volume marked open face should record positive outflow");

    Fluid25DScenarioData empty = drain;
    std::fill(empty.initial_water_depth_m.begin(), empty.initial_water_depth_m.end(), 0.0F);
    Fluid25DFiniteVolumeOracle empty_oracle(drain_config, empty);
    const Fluid25DStepLedger empty_ledger = empty_oracle.step();
    require_close(empty_ledger.boundary_outflow_volume_m3, 0.0, kDepthToleranceM,
                  "finite-volume dry exterior must not introduce water");
    require_close(empty_oracle.total_water_volume_m3(), 0.0, kDepthToleranceM,
                  "finite-volume empty open domain should remain empty");

    Fluid25DConfig source_sink_config = finite_volume_test_config(2U, 2U, Fluid25DScenario::DryBed);
    source_sink_config.fixed_delta_seconds = 0.10F;
    source_sink_config.simulation_substeps = 1U;
    source_sink_config.gravity_m_per_s2 = 0.00000001F;
    source_sink_config.flow_damping_per_second = 0.0F;
    source_sink_config.minimum_wet_depth_m = 0.000000001F;
    validate_fluid_25d_config(source_sink_config);
    Fluid25DScenarioData source_sink =
        make_fluid_25d_scenario(source_sink_config.scenario, source_sink_config.grid_width,
                                source_sink_config.grid_height, source_sink_config.cell_size_m);
    std::fill(source_sink.terrain_height_m.begin(), source_sink.terrain_height_m.end(), 0.0F);
    source_sink.source_depth_rate_m_per_s[0] = 0.50F;
    source_sink.sink_depth_rate_m_per_s[0] = 0.20F;
    Fluid25DFiniteVolumeOracle source_sink_oracle(source_sink_config, source_sink);
    const Fluid25DStepLedger source_sink_ledger = source_sink_oracle.step();
    require_close(source_sink_ledger.source_volume_m3, 0.05, kLedgerToleranceM3,
                  "finite-volume source ledger should use source depth rate and cell area");
    require_close(source_sink_ledger.sink_volume_m3, 0.02, kLedgerToleranceM3,
                  "finite-volume sink ledger should use actual removed depth and cell area");
    require_close(source_sink_oracle.total_water_volume_m3(), 0.03, kLedgerToleranceM3,
                  "finite-volume source and sink ledger should reconcile stored water");
    require_close(source_sink_ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                  "finite-volume source and sink ledger should remain conserved");
}

void test_finite_volume_sustained_headwaters_flow_control() {
    using namespace cubey::projects::fluid::fluid_25d;
    struct Resolution {
        std::uint32_t width;
        std::uint32_t height;
        float cell_size_m;
    };
    constexpr std::array<Resolution, 3U> resolutions{{
        {kFluid25DSustainedHeadwatersGridWidth, kFluid25DSustainedHeadwatersGridHeight,
         kFluid25DSustainedHeadwatersCellSizeM},
        {kFluid25DSustainedHeadwatersGridWidth2m, kFluid25DSustainedHeadwatersGridHeight2m,
         kFluid25DSustainedHeadwatersCellSize2m},
        {kFluid25DSustainedHeadwatersGridWidth1m, kFluid25DSustainedHeadwatersGridHeight1m,
         kFluid25DSustainedHeadwatersCellSize1m},
    }};
    constexpr float kMaterialFrontDepthM = 0.001F;
    constexpr std::uint64_t kMaximumControlSteps = 2400U; // 600 seconds at 0.25 s/fixed step.
    const auto reference_cell = [](std::uint32_t value, float cell_size_m) {
        return static_cast<std::uint32_t>(
            std::lround(static_cast<float>(value) * 4.0F / cell_size_m));
    };
    for (const Resolution& resolution : resolutions) {
        Fluid25DConfig config;
        config.scenario = Fluid25DScenario::SustainedHeadwatersDemo;
        config.solver = Fluid25DSolver::FiniteVolume;
        config.grid_width = resolution.width;
        config.grid_height = resolution.height;
        config.cell_size_m = resolution.cell_size_m;
        config.fixed_delta_seconds = 0.25F;
        config.simulation_substeps = 8U;
        validate_fluid_25d_config(config);

        const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
            config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
        Fluid25DFiniteVolumeOracle oracle(config, scenario);
        require_close(oracle.total_water_volume_m3(), 0.0, kDepthToleranceM,
                      "sustained-headwaters evolution should begin from a dry state");

        const std::size_t branch_a_probe = fluid_25d_scenario_index(
            config.grid_width, config.grid_height, reference_cell(32U, config.cell_size_m),
            reference_cell(14U, config.cell_size_m));
        const std::size_t branch_b_probe = fluid_25d_scenario_index(
            config.grid_width, config.grid_height, reference_cell(32U, config.cell_size_m),
            reference_cell(18U, config.cell_size_m));
        const std::size_t source_a_probe = fluid_25d_scenario_index(
            config.grid_width, config.grid_height,
            reference_cell(kFluid25DSustainedHeadwatersSourceAX, config.cell_size_m),
            reference_cell(kFluid25DSustainedHeadwatersSourceAY, config.cell_size_m));
        const std::size_t source_b_probe = fluid_25d_scenario_index(
            config.grid_width, config.grid_height,
            reference_cell(kFluid25DSustainedHeadwatersSourceBX, config.cell_size_m),
            reference_cell(kFluid25DSustainedHeadwatersSourceBY, config.cell_size_m));
        const std::size_t confluence_probe = fluid_25d_scenario_index(
            config.grid_width, config.grid_height,
            reference_cell(kFluid25DSustainedHeadwatersConfluenceX, config.cell_size_m),
            reference_cell(kFluid25DSustainedHeadwatersConfluenceY, config.cell_size_m));
        const std::size_t downstream_probe = fluid_25d_scenario_index(
            config.grid_width, config.grid_height, reference_cell(54U, config.cell_size_m),
            reference_cell(kFluid25DSustainedHeadwatersConfluenceY, config.cell_size_m));
        bool branch_a_wetted = false;
        bool branch_b_wetted = false;
        bool confluence_wetted = false;
        bool trunk_wetted = false;
        double cumulative_source_volume_m3 = 0.0;
        double cumulative_outflow_volume_m3 = 0.0;
        // Retain the 600 s reach/outlet regression at the exact 4 m control.
        // Refined CPU cases are 30 s CFL/positivity/ledger sanity checks; the
        // long 900/1800 s refined evidence is collected through the GPU runner.
        const std::uint64_t control_steps =
            config.cell_size_m == kFluid25DSustainedHeadwatersCellSizeM ? kMaximumControlSteps
                                                                        : 120U;

        for (std::uint64_t step = 0U; step < control_steps; ++step) {
            const Fluid25DStepLedger ledger = oracle.step();
            cumulative_source_volume_m3 += ledger.source_volume_m3;
            cumulative_outflow_volume_m3 += ledger.boundary_outflow_volume_m3;
            const double step_ledger_tolerance_m3 =
                std::max(kLedgerToleranceM3, ledger.volume_after_m3 * 2.0e-6);
            require_close(
                ledger.conservation_error_m3(), 0.0, step_ledger_tolerance_m3,
                "sustained-headwaters fixed step should conserve source and boundary volume");
            require(ledger.sink_volume_m3 == 0.0,
                    "sustained-headwaters control should not remove water through a sink");
            require(oracle.last_cfl_number() <= Fluid25DFiniteVolumeOracle::kTargetCfl,
                    "sustained-headwaters accepted steps should remain within the CFL target");

            const std::vector<float>& depth_m = oracle.water_depth_m();
            require(std::all_of(depth_m.begin(), depth_m.end(),
                                [](float depth) { return std::isfinite(depth) && depth >= 0.0F; }),
                    "sustained-headwaters evolution should keep all depths finite and nonnegative");
            branch_a_wetted = branch_a_wetted || depth_m[branch_a_probe] > kMaterialFrontDepthM;
            branch_b_wetted = branch_b_wetted || depth_m[branch_b_probe] > kMaterialFrontDepthM;
            if (control_steps < kMaximumControlSteps) {
                require(depth_m[source_a_probe] > 0.0F && depth_m[source_b_probe] > 0.0F,
                        "refined headwater CPU steps should apply forcing at both source centers");
            }
            confluence_wetted =
                confluence_wetted || depth_m[confluence_probe] > kMaterialFrontDepthM;
            trunk_wetted = trunk_wetted || depth_m[downstream_probe] > kMaterialFrontDepthM;
        }

        if (control_steps == kMaximumControlSteps) {
            require(branch_a_wetted && branch_b_wetted,
                    "both continuously forced tributaries should carry a material water front");
            require(confluence_wetted,
                    "the two headwater fronts should reach their shared confluence");
            require(trunk_wetted, "confluence flow should continue down the shared trunk");
            require(
                cumulative_outflow_volume_m3 > 0.01,
                "sustained headwater flow should eventually discharge through the east aperture");
        }
        const double expected_source_volume_m3 = 0.50 *
                                                 static_cast<double>(config.fixed_delta_seconds) *
                                                 static_cast<double>(control_steps);
        // The solver adds each source as a float depth increment; at nonzero depth the
        // represented delta is quantized when accumulated into water_depth_m_. Keep this
        // integrated-ledger tolerance relative and tight, while the static source-field
        // test above independently verifies the requested 0.50 m^3/s normalization.
        const double integrated_source_tolerance_m3 =
            std::max(kVolumeToleranceM3, expected_source_volume_m3 * 5.0e-5);
        require_close(
            cumulative_source_volume_m3, expected_source_volume_m3, integrated_source_tolerance_m3,
            "two constant headwaters should contribute the configured total source volume");
        require_close(
            oracle.total_water_volume_m3() + cumulative_outflow_volume_m3,
            cumulative_source_volume_m3, 0.05,
            "stored water plus open-boundary discharge should reconcile all headwater input");
    }
}

void test_finite_volume_cfl_fails_closed() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(2U, 2U, Fluid25DScenario::DryBed);
    config.fixed_delta_seconds = 0.25F;
    config.simulation_substeps = 1U;
    config.flow_damping_per_second = 0.0F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    scenario.initial_water_depth_m[0] = 1.0F;
    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    const std::vector<float> initial_depth = oracle.water_depth_m();
    require_throws([&] { static_cast<void>(oracle.step()); },
                   "finite-volume CFL above the conservative target should fail closed");
    require(oracle.water_depth_m() == initial_depth && oracle.last_cfl_number() == 0.0F,
            "a rejected finite-volume CFL step should not mutate persistent state");

    Fluid25DConfig virtual_pipe_config = config;
    virtual_pipe_config.solver = Fluid25DSolver::VirtualPipes;
    validate_fluid_25d_config(virtual_pipe_config);
    require_throws([&] { static_cast<void>(Fluid25DOracle(config, scenario)); },
                   "virtual-pipes oracle construction should reject the finite-volume comparison "
                   "configuration");
    require_throws(
        [&] { static_cast<void>(Fluid25DFiniteVolumeOracle(virtual_pipe_config, scenario)); },
        "finite-volume oracle construction should reject the virtual-pipes product configuration");
}

} // namespace cubey::projects::fluid::fluid_25d::test_detail
