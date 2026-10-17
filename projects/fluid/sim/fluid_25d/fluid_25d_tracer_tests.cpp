#include "fluid_25d_test_support.h"

namespace cubey::projects::fluid::fluid_25d::test_detail {

void test_dye_zero_path_and_resting_concentration() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(6U, 3U, Fluid25DScenario::SourceOutletDemo);
    config.fixed_delta_seconds = 0.01F;
    config.simulation_substeps = 1U;
    config.flow_damping_per_second = 0.0F;
    validate_fluid_25d_config(config);
    const Fluid25DScenarioData scenario = make_dye_test_scenario();
    Fluid25DFiniteVolumeOracle plain(config, scenario);
    Fluid25DFiniteVolumeOracle opt_in(config, scenario);
    for (int step = 0; step < 8; ++step) {
        const Fluid25DStepLedger plain_ledger = plain.step(1.0F);
        const Fluid25DTracerStepResult opt_in_result = opt_in.step_with_dye(1.0F);
        require(plain.water_depth_m() == opt_in.water_depth_m(),
                "zero-dye opt-in path should preserve existing water values");
        require(plain.momentum_m2_per_s().size() == opt_in.momentum_m2_per_s().size() &&
                    std::equal(plain.momentum_m2_per_s().begin(), plain.momentum_m2_per_s().end(),
                               opt_in.momentum_m2_per_s().begin(),
                               [](const Fluid25DMomentum& left, const Fluid25DMomentum& right) {
                                   return left.x_m2_per_s == right.x_m2_per_s &&
                                          left.y_m2_per_s == right.y_m2_per_s;
                               }),
                "zero-dye opt-in path should preserve existing momentum values");
        require_close(opt_in_result.tracer.source_amount_m3, 0.0, kDepthToleranceM,
                      "unset dye should add no source tracer");
        require_close(opt_in.total_tracer_amount_m3(), 0.0, kDepthToleranceM,
                      "unset dye should retain zero tracer amount");
        require_close(plain_ledger.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "zero-dye comparison water ledger should remain conserved");
    }

    Fluid25DConfig pulse_config = config;
    pulse_config.dye_pulse_start_seconds = 0.0F;
    pulse_config.dye_pulse_duration_seconds = 0.01F;
    validate_fluid_25d_config(pulse_config);
    Fluid25DScenarioData resting = make_dye_test_scenario();
    std::fill(resting.initial_water_depth_m.begin(), resting.initial_water_depth_m.end(), 0.0F);
    std::fill(resting.source_depth_rate_m_per_s.begin(), resting.source_depth_rate_m_per_s.end(),
              0.20F);
    std::fill(resting.sink_depth_rate_m_per_s.begin(), resting.sink_depth_rate_m_per_s.end(), 0.0F);
    Fluid25DFiniteVolumeOracle uniform(pulse_config, resting);
    const Fluid25DTracerStepResult injected = uniform.step_with_dye(1.0F);
    require(injected.tracer.source_amount_m3 > 0.0,
            "uniform source pulse should inject a positive tracer amount");
    for (std::size_t index = 0U; index < uniform.water_depth_m().size(); ++index) {
        require_close(uniform.tracer_concentration()[index], 1.0, kDepthToleranceM,
                      "a uniformly dyed resting source field should retain concentration one");
    }
    const Fluid25DTracerStepResult resting_step = uniform.step_with_dye(0.0F);
    require_close(
        resting_step.tracer.conservation_error_m3(), 0.0, kLedgerToleranceM3,
        "uniform resting tracer should remain conserved with hydraulic source scale zero");
    for (const float concentration : uniform.tracer_concentration()) {
        require_close(concentration, 1.0, kDepthToleranceM,
                      "uniform resting tracer should remain spatially uniform");
    }
}

void test_sustained_headwaters_dye_pulse() {
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
    for (const Resolution& resolution : resolutions) {
        Fluid25DConfig config;
        config.scenario = Fluid25DScenario::SustainedHeadwatersDemo;
        config.solver = Fluid25DSolver::FiniteVolume;
        config.grid_width = resolution.width;
        config.grid_height = resolution.height;
        config.cell_size_m = resolution.cell_size_m;
        config.fixed_delta_seconds = 0.25F;
        config.simulation_substeps = 2U;
        config.dye_pulse_start_seconds = 0.0F;
        config.dye_pulse_duration_seconds = config.fixed_delta_seconds;
        validate_fluid_25d_config(config);
        require(!config.source_active_duration_seconds.has_value(),
                "headwater tracer control should retain continuous hydraulic source scheduling");

        const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
            config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
        Fluid25DFiniteVolumeOracle oracle(config, scenario);
        const Fluid25DTracerStepResult pulse_step = oracle.step_with_dye();
        require_close(pulse_step.water.source_volume_m3, 0.125, kVolumeToleranceM3,
                      "both headwater inputs should inject water during the tracer pulse step");
        require_close(pulse_step.tracer.source_amount_m3, 0.125, kVolumeToleranceM3,
                      "the shared unit-concentration pulse should add tracer at both inputs");
        require_close(pulse_step.water.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "headwater pulse water ledger should conserve");
        require_close(pulse_step.tracer.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "headwater pulse tracer ledger should conserve");

        double source_a_tracer_amount_m3 = 0.0;
        double source_b_tracer_amount_m3 = 0.0;
        const double cell_area_m2 =
            static_cast<double>(config.cell_size_m) * static_cast<double>(config.cell_size_m);
        for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
            for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
                const std::size_t index =
                    fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
                const double tracer_amount_m3 =
                    static_cast<double>(oracle.tracer_mass_per_area_m()[index]) * cell_area_m2;
                if (scenario.source_depth_rate_m_per_s[index] > 0.0F) {
                    const double cell_center_y_m =
                        static_cast<double>(y) * static_cast<double>(config.cell_size_m);
                    if (cell_center_y_m < 64.0) {
                        source_a_tracer_amount_m3 += tracer_amount_m3;
                    } else {
                        source_b_tracer_amount_m3 += tracer_amount_m3;
                    }
                }
            }
        }
        require(source_a_tracer_amount_m3 > 0.0 && source_b_tracer_amount_m3 > 0.0,
                "the shared tracer pulse should appear in both refined source footprints");

        const Fluid25DTracerStepResult post_pulse_step = oracle.step_with_dye();
        require_close(post_pulse_step.water.source_volume_m3, 0.125, kVolumeToleranceM3,
                      "headwater water input should continue after the dye pulse ends");
        require_close(post_pulse_step.tracer.source_amount_m3, 0.0, kDepthToleranceM,
                      "the half-open headwater tracer pulse should be off at its end boundary");
        require_close(post_pulse_step.water.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "post-pulse headwater water ledger should conserve");
        require_close(post_pulse_step.tracer.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "post-pulse headwater tracer ledger should conserve");
        require_close(oracle.total_water_volume_m3(), 0.25, kVolumeToleranceM3,
                      "continuous headwater sources should accumulate after the pulse boundary");
        const auto& cumulative_tracer = oracle.cumulative_tracer_ledger();
        require_close(
            oracle.total_tracer_amount_m3(),
            cumulative_tracer.source_amount_m3 - cumulative_tracer.sink_amount_m3 -
                cumulative_tracer.boundary_outflow_amount_m3,
            kVolumeToleranceM3,
            "headwater tracer storage should reconcile its cumulative source and outlet ledger");
        require_close(
            cumulative_tracer.source_amount_m3, 0.125, kVolumeToleranceM3,
            "cumulative headwater tracer input should stop at the half-open pulse boundary");
    }
}

void test_dye_advection_bounds_and_ledgers() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(6U, 3U, Fluid25DScenario::SourceOutletDemo);
    config.fixed_delta_seconds = 0.01F;
    config.simulation_substeps = 1U;
    config.flow_damping_per_second = 0.0F;
    config.minimum_wet_depth_m = 0.000001F;
    config.dye_pulse_start_seconds = 0.0F;
    config.dye_pulse_duration_seconds = 0.01F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_dye_test_scenario();
    const std::size_t source = scenario.source_cell;
    const std::size_t sink = scenario.sink_cell;
    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    double cumulative_source = 0.0;
    double cumulative_sink = 0.0;
    double cumulative_boundary = 0.0;
    bool saw_downstream_dye = false;
    for (int step = 0; step < 120; ++step) {
        const Fluid25DTracerStepResult result = oracle.step_with_dye(1.0F);
        cumulative_source += result.tracer.source_amount_m3;
        cumulative_sink += result.tracer.sink_amount_m3;
        cumulative_boundary += result.tracer.boundary_outflow_amount_m3;
        require_close(result.tracer.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                      "dye source, sink, and outflow ledger should reconcile each step");
        for (std::size_t index = 0U; index < oracle.water_depth_m().size(); ++index) {
            const float depth = oracle.water_depth_m()[index];
            const float q = oracle.tracer_mass_per_area_m()[index];
            const float concentration = oracle.tracer_concentration()[index];
            require(std::isfinite(q) && q >= 0.0F && q <= depth + 0.000001F,
                    "dye q must remain bounded by nonnegative water depth");
            require(std::isfinite(concentration) && concentration >= 0.0F && concentration <= 1.0F,
                    "dye concentration must remain in the normalized unit interval");
            if (depth == 0.0F) {
                require(q == 0.0F,
                        "dry cells must not originate or retain a nonzero tracer amount");
            }
            saw_downstream_dye =
                saw_downstream_dye || (index != source && index != sink && q > 0.000001F);
        }
    }
    require(saw_downstream_dye, "a finite-volume dye pulse should advect beyond its source region");
    require(cumulative_source > 0.0,
            "dye advection fixture should record a positive pulse source amount");
    require(cumulative_sink > 0.0,
            "dye advection fixture should deliver a positive amount to its proportional sink");
    require_close(oracle.total_tracer_amount_m3(),
                  cumulative_source - cumulative_sink - cumulative_boundary, kVolumeToleranceM3,
                  "cumulative dye amount should reconcile source, sink, and boundary ledgers");
    require_close(oracle.cumulative_tracer_ledger().source_amount_m3, cumulative_source,
                  kVolumeToleranceM3, "cumulative dye source ledger should match step totals");
}

void test_dye_zero_sink_transfer_and_real_sink() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(6U, 3U, Fluid25DScenario::SourceOutletDemo);
    config.fixed_delta_seconds = 0.01F;
    config.simulation_substeps = 1U;
    config.dye_pulse_start_seconds = 0.0F;
    config.dye_pulse_duration_seconds = config.fixed_delta_seconds;
    auto scenario = make_dye_test_scenario();
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              0.01234567F);
    std::fill(scenario.sink_depth_rate_m_per_s.begin(), scenario.sink_depth_rate_m_per_s.end(),
              0.0F);
    Fluid25DFiniteVolumeOracle zero_sink(config, scenario);
    for (std::size_t step = 0U; step < 1200U; ++step) {
        const auto result = zero_sink.step_with_dye();
        require(result.tracer.sink_amount_m3 == 0.0 &&
                    zero_sink.cumulative_tracer_ledger().sink_amount_m3 == 0.0,
                "zero actual sink transfer must preserve an exactly zero tracer sink ledger");
    }
    std::fill(scenario.sink_depth_rate_m_per_s.begin(), scenario.sink_depth_rate_m_per_s.end(),
              0.05F);
    Fluid25DFiniteVolumeOracle real_sink(config, scenario);
    const auto removed = real_sink.step_with_dye();
    require(removed.water.sink_volume_m3 > 0.0 && removed.tracer.sink_amount_m3 > 0.0,
            "the zero-sink fix must not disable actual water or proportional tracer removal");
    const float source_delta = 0.01234567F * config.fixed_delta_seconds;
    const float sourced_depth = 0.20F + source_delta;
    const double donor_concentration = static_cast<double>(source_delta) / sourced_depth;
    require_close(removed.tracer.sink_amount_m3, removed.water.sink_volume_m3 * donor_concentration,
                  kLedgerToleranceM3,
                  "a real sink must transfer the source-mixed donor tracer concentration");
    // Complement the executable CTest GPU regression with a source-structure
    // guard against reintroducing the unconditional h/h.
    const auto path =
        std::filesystem::path(__FILE__).parent_path() / "shaders/fluid_25d_fv_update.comp";
    const auto shared = std::filesystem::path(__FILE__)
                            .parent_path()
                            .parent_path()
                            .parent_path()
                            .parent_path()
                            .parent_path() /
                        "shaders/cubey/terrain/surface_patch" / path.filename();
    std::ifstream stream(
        path.parent_path().filename() == "shaders" && std::filesystem::exists(shared) ? shared
                                                                                      : path);
    const std::string shader{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    require(stream.good() || stream.eof(), "zero-sink shader control must read the shipped source");
    require(shader.find("if (removed_h > 0.0)") != std::string::npos &&
                shader.find("float removed_tracer_q = 0.0;") != std::string::npos,
            "GPU tracer sink arithmetic must be conditional on actual removed water");
}

void test_dye_open_boundary_ledger() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(6U, 3U, Fluid25DScenario::SourceOutletDemo);
    config.fixed_delta_seconds = 0.01F;
    config.simulation_substeps = 1U;
    config.flow_damping_per_second = 0.0F;
    config.dye_pulse_start_seconds = 0.0F;
    config.dye_pulse_duration_seconds = 0.01F;
    validate_fluid_25d_config(config);

    Fluid25DScenarioData scenario = make_dye_test_scenario();
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              0.0F);
    std::fill(scenario.sink_depth_rate_m_per_s.begin(), scenario.sink_depth_rate_m_per_s.end(),
              0.0F);
    scenario.source_cell = fluid_25d_scenario_index(6U, 3U, 0U, 1U);
    scenario.sink_cell = kFluid25DNoCell;
    scenario.source_depth_rate_m_per_s[scenario.source_cell] = 0.20F;
    scenario.boundary_outflow_face_mask[scenario.source_cell] = kFluid25DBoundaryOutflowLeft;
    const double source_delta_m =
        static_cast<double>(scenario.source_depth_rate_m_per_s[scenario.source_cell]) *
        static_cast<double>(config.fixed_delta_seconds);
    const double source_depth_m =
        static_cast<double>(scenario.initial_water_depth_m[scenario.source_cell]) + source_delta_m;
    const double expected_donor_concentration = source_delta_m / source_depth_m;

    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    const Fluid25DTracerStepResult result = oracle.step_with_dye(1.0F);
    require(result.tracer.source_amount_m3 > 0.0,
            "open-boundary fixture should inject a positive tracer amount");
    require(result.water.boundary_outflow_volume_m3 > 0.0,
            "open-boundary fixture should remove a positive water amount");
    require(result.tracer.boundary_outflow_amount_m3 > 0.0,
            "open-boundary fixture should remove a positive tracer amount");
    require_close(result.tracer.boundary_outflow_amount_m3,
                  result.water.boundary_outflow_volume_m3 * expected_donor_concentration,
                  kLedgerToleranceM3,
                  "open-boundary tracer outflow should use the interior donor concentration");
    require_close(result.tracer.conservation_error_m3(), 0.0, kLedgerToleranceM3,
                  "open-boundary tracer ledger should reconcile its source and removal");
    require_close(oracle.total_tracer_amount_m3(),
                  result.tracer.source_amount_m3 - result.tracer.boundary_outflow_amount_m3,
                  kVolumeToleranceM3,
                  "open-boundary tracer amount should reconcile its positive source and outflow");
    require_close(oracle.cumulative_tracer_ledger().boundary_outflow_amount_m3,
                  result.tracer.boundary_outflow_amount_m3, kLedgerToleranceM3,
                  "cumulative tracer accounting should retain the open-boundary removal");
}

void test_dye_cfl_rejection_and_reset() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(6U, 3U, Fluid25DScenario::SourceOutletDemo);
    config.fixed_delta_seconds = 0.25F;
    config.simulation_substeps = 1U;
    config.flow_damping_per_second = 0.0F;
    config.dye_pulse_start_seconds = 0.0F;
    config.dye_pulse_duration_seconds = 1.0F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_dye_test_scenario();
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.0F);
    scenario.initial_water_depth_m[scenario.source_cell] = 1.0F;
    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    const std::vector<float> initial_depth = oracle.water_depth_m();
    const std::vector<float> initial_q = oracle.tracer_mass_per_area_m();
    const Fluid25DTracerStepLedger initial_last = oracle.last_tracer_step_ledger();
    const Fluid25DTracerStepLedger initial_cumulative = oracle.cumulative_tracer_ledger();
    require_throws([&] { static_cast<void>(oracle.step_with_dye(1.0F)); },
                   "dye CFL rejection should fail closed");
    require(oracle.water_depth_m() == initial_depth &&
                oracle.tracer_mass_per_area_m() == initial_q &&
                oracle.last_tracer_step_ledger().amount_after_m3 == initial_last.amount_after_m3 &&
                oracle.cumulative_tracer_ledger().amount_after_m3 ==
                    initial_cumulative.amount_after_m3 &&
                oracle.completed_steps() == 0U,
            "rejected dye candidate should leave water, tracer, ledgers, and clock unchanged");

    config.fixed_delta_seconds = 0.01F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData valid = make_dye_test_scenario();
    Fluid25DFiniteVolumeOracle resettable(config, valid);
    static_cast<void>(resettable.step_with_dye(1.0F));
    require(resettable.total_tracer_amount_m3() > 0.0,
            "reset fixture should first accumulate a positive dye amount");
    resettable.reset();
    require(resettable.total_tracer_amount_m3() == 0.0 && resettable.completed_steps() == 0U &&
                std::all_of(resettable.tracer_mass_per_area_m().begin(),
                            resettable.tracer_mass_per_area_m().end(),
                            [](float q) { return q == 0.0F; }) &&
                std::all_of(resettable.tracer_concentration().begin(),
                            resettable.tracer_concentration().end(),
                            [](float c) { return c == 0.0F; }) &&
                resettable.last_tracer_step_ledger().amount_after_m3 == 0.0 &&
                resettable.cumulative_tracer_ledger().amount_after_m3 == 0.0,
            "dye reset should restore zero tracer state, accounting, and schedule clock");
}

void test_finite_volume_reference_conserved_storage() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config;
    config.solver = Fluid25DSolver::FiniteVolume;
    config.scenario = Fluid25DScenario::DryBed;
    config.grid_width = config.grid_height = 4U;
    config.cell_size_m = 30.0F;
    config.fixed_delta_seconds = 2.0F;
    config.simulation_substeps = 16U;
    auto scenario = make_fluid_25d_scenario(config.scenario, 4U, 4U, config.cell_size_m);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 1.0F);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              20.0F / 900.0F);
    Fluid25DFiniteVolumeOracle oracle(config, scenario);
    double sources = 0.0;
    for (unsigned step = 0U; step < 3600U; ++step) {
        const auto ledger = oracle.step();
        sources += ledger.source_volume_m3;
        require(ledger.sink_volume_m3 == 0.0 && ledger.boundary_outflow_volume_m3 == 0.0,
                "closed reference source basin must not acquire a sink or outlet");
    }
    const double nominal_sources =
        static_cast<double>(scenario.source_depth_rate_m_per_s.front()) * 7200.0 * 900.0 * 16.0;
    require_close(
        sources, nominal_sources, 1.0e-6,
        "reference source ledger must measure configured addition, not float h subtraction");
    const float h = oracle.water_depth_m().front();
    const double maximum_float_publication_error =
        0.5 * static_cast<double>(std::nextafter(h, std::numeric_limits<float>::infinity()) - h) *
        900.0 * 16.0;
    require(std::abs(oracle.total_water_volume_m3() - 14400.0 - sources) <=
                maximum_float_publication_error,
            "local double remainders must bound two-hour storage error by final float publication");
    oracle.reset();
    Fluid25DFiniteVolumeOracle fresh(config, scenario);
    static_cast<void>(oracle.step());
    static_cast<void>(fresh.step());
    require(oracle.water_depth_m() == fresh.water_depth_m(),
            "reference reset must clear the accumulated conserved-depth remainder");
}

void test_finite_volume_gpu_candidate_commit_shader_contract() {
    // Invalid GPU candidate-state injection is deliberately not a user-facing
    // scenario knob. Keep a small structural test on the shipped shaders in
    // addition to the direct GPU CFL rejection lane: candidate must zero its
    // isolated ledger delta and validate the prospective cumulative ledger;
    // commit must be the sole writer of the shared ledger/velocity state.
    const std::filesystem::path shader_directory =
        std::filesystem::path(__FILE__).parent_path() / "shaders";
    const auto read_shader = [](const std::filesystem::path& path) {
        const auto shared = std::filesystem::path(__FILE__)
                                .parent_path()
                                .parent_path()
                                .parent_path()
                                .parent_path()
                                .parent_path() /
                            "shaders/cubey/terrain/surface_patch" / path.filename();
        std::ifstream stream(
            path.parent_path().filename() == "shaders" && std::filesystem::exists(shared) ? shared
                                                                                          : path);
        if (!stream) {
            throw std::runtime_error("failed to read finite-volume transaction shader");
        }
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    };
    const std::string candidate = read_shader(shader_directory / "fluid_25d_fv_update.comp");
    const std::string commit = read_shader(shader_directory / "fluid_25d_fv_commit.comp");
    const std::string reset = read_shader(shader_directory / "fluid_25d_fv_reset.comp");
    const std::string commands =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_commands.cpp");
    require(commands.find("render_options.native_surface_highlights ? 0.0F : 256.0F") !=
                    std::string::npos &&
                commands.find("water_constants.terrain_palette.w") != std::string::npos,
            "highlight policy must use water-only palette bit 8 without extending push constants");
    const std::string app =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_builtin_render.cpp");
    const std::string resources =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_gpu_resources.cpp");
    require(
        candidate.find("candidate_ledger_delta.values[index] = vec4(0.0);") != std::string::npos &&
            candidate.find("candidate_tracer_q.values[index] = tracer_q.values[index];") !=
                std::string::npos &&
            candidate.find("candidate_tracer_ledger_delta.values[index] = vec4(0.0);") !=
                std::string::npos &&
            candidate.find("conserved_add(sourced_tracer_pair, -retained_tracer_pair).x") !=
                std::string::npos &&
            candidate.find("vec4 prospective_ledger = cumulative_ledger.values[index] + "
                           "ledger_delta;") != std::string::npos &&
            candidate.find("prospective_tracer_ledger") != std::string::npos &&
            candidate.find("face_flux.homogeneous.h") != std::string::npos &&
            candidate.find("!finite_nonnegative_ledger(prospective_ledger)") != std::string::npos &&
            candidate.find("ledger.values[index] +=") == std::string::npos,
        "finite-volume candidate shader must isolate q, normalize only sink-rounding residue, and "
        "validate both ledger deltas before commit");
    require(commit.find("if (status.flags != 0u)") != std::string::npos &&
                commit.find("next_depth.values[index] = source_depth.values[index];") !=
                    std::string::npos &&
                commit.find("next_momentum.values[index] = source_momentum.values[index];") !=
                    std::string::npos &&
                commit.find("next_tracer_q.values[index] = source_tracer_q.values[index];") !=
                    std::string::npos &&
                commit.find("previous_residual.water_ledger[component]") != std::string::npos &&
                commit.find("previous_residual.tracer_ledger[component]") != std::string::npos &&
                commit.find("conservation_residual.values[index] = "
                            "candidate_conservation_residual.values[index];") != std::string::npos,
            "finite-volume commit shader must copy rejected q and publish both ledgers once");
    require(
        reset.find("tracer_q_a.values[index] = 0.0;") != std::string::npos &&
            reset.find("tracer_q_b.values[index] = 0.0;") != std::string::npos &&
            reset.find("tracer_ledger.values[index] = vec4(0.0);") != std::string::npos &&
            reset.find("conservation_residual.values[index] = ConservationResidual") !=
                std::string::npos &&
            commands.find("Fluid25DStepForcing") != std::string::npos &&
            commands.find("dye_source_concentration") != std::string::npos &&
            app.find("dye_source_schedule_.source_concentration") != std::string::npos &&
            app.find("step_with_dye(") != std::string::npos &&
            resources.find("storage_set_info(9U, VK_SHADER_STAGE_VERTEX_BIT") !=
                std::string::npos &&
            resources.find(".storage_buffer(set, 6, tracer_q.handle(), tracer_q.size())") !=
                std::string::npos,
        "GPU tracer runtime must reset with depth parity, sample fixed-step forcing, and bind q "
        "at append-only render binding 6");
}

} // namespace cubey::projects::fluid::fluid_25d::test_detail
