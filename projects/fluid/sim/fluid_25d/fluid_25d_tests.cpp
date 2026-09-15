#include "../../fluid_25d/fluid_25d_project_config.h"
#include "fluid_25d_oracle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// These tolerances cover float field updates and double-precision volume
// accumulation on the small reference grids; they are expressed in the
// contract's physical units.
constexpr double kDepthToleranceM = 0.000001;
constexpr double kLedgerToleranceM3 = 0.00001;
constexpr double kVolumeToleranceM3 = 0.0005;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(double actual, double expected, double tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

template <typename Callable> void require_throws(Callable&& callable, const char* message) {
    bool threw = false;
    try {
        callable();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

[[nodiscard]] cubey::projects::fluid::fluid_25d::Fluid25DProjectConfig
parse_project(std::vector<std::string> arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    return cubey::projects::fluid::fluid_25d::parse_fluid_25d_project_config(
        static_cast<int>(argv.size()), argv.data());
}

[[nodiscard]] cubey::projects::fluid::fluid_25d::Fluid25DConfig
test_config(std::uint32_t width, std::uint32_t height,
            cubey::projects::fluid::fluid_25d::Fluid25DScenario scenario) {
    cubey::projects::fluid::fluid_25d::Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.scenario = scenario;
    config.simulation_substeps = 2;
    cubey::projects::fluid::fluid_25d::validate_fluid_25d_config(config);
    return config;
}

void test_config_defaults_and_parsing() {
    using namespace cubey::projects::fluid::fluid_25d;

    const Fluid25DConfig defaults;
    validate_fluid_25d_config(defaults);
    require(defaults.grid_width == kDefaultFluid25DGridWidth,
            "fluid 2.5D should expose the River V0 default grid width");
    require(defaults.grid_height == kDefaultFluid25DGridHeight,
            "fluid 2.5D should expose the River V0 default grid height");
    require(defaults.cell_size_m == 1.0F, "fluid 2.5D should default to one metre cells");
    require(defaults.simulation_substeps == 2,
            "fluid 2.5D should default to two fixed solver substeps");
    require(defaults.scenario == Fluid25DScenario::RiverCatchment,
            "fluid 2.5D should default to the river catchment scenario");
    require(std::string(fluid_25d_scenario_name(Fluid25DScenario::LakeAtRest)) == "lake-at-rest",
            "fluid 2.5D scenario names should be stable");
    require(fluid_25d_scenario_from_name("dry") == Fluid25DScenario::DryBed,
            "fluid 2.5D should retain the short dry scenario alias");
    require_throws([] { static_cast<void>(fluid_25d_scenario_from_name("unknown")); },
                   "fluid 2.5D should reject unknown scenario names");
    require(fluid_25d_debug_view_from_name("flow") == Fluid25DDebugView::FlowMagnitude,
            "fluid 2.5D should parse the flow diagnostic view");
    require_throws([] { static_cast<void>(fluid_25d_debug_view_from_name("unknown")); },
                   "fluid 2.5D should reject unknown diagnostic views");

    const Fluid25DProjectConfig parsed = parse_project(
        {"fluid_25d", "--grid-width", "10", "--grid-height", "6", "--fluid25d-scenario",
         "lake-at-rest", "--fluid25d-cell-size-m", "2.5", "--fluid25d-fixed-delta-seconds", "0.02",
         "--fluid25d-substeps", "3", "--fluid25d-gravity-m-per-s2", "9.5",
         "--fluid25d-flow-damping-per-second", "0.8", "--fluid25d-minimum-wet-depth-m", "0.002"});
    require(parsed.simulation.grid_width == 10 && parsed.simulation.grid_height == 6,
            "fluid 2.5D parser should bind shared grid dimensions");
    require(parsed.simulation.scenario == Fluid25DScenario::LakeAtRest,
            "fluid 2.5D parser should bind the scenario");
    require_close(parsed.simulation.cell_size_m, 2.5, kDepthToleranceM,
                  "fluid 2.5D parser should bind cell size in metres");
    require_close(parsed.simulation.fixed_delta_seconds, 0.02, kDepthToleranceM,
                  "fluid 2.5D parser should bind fixed delta");
    require(parsed.simulation.simulation_substeps == 3,
            "fluid 2.5D parser should bind fixed substeps");
    require_close(parsed.simulation.gravity_m_per_s2, 9.5, kDepthToleranceM,
                  "fluid 2.5D parser should bind gravity");
    require_close(parsed.simulation.flow_damping_per_second, 0.8, kDepthToleranceM,
                  "fluid 2.5D parser should bind flow damping");
    require_close(parsed.simulation.minimum_wet_depth_m, 0.002, kDepthToleranceM,
                  "fluid 2.5D parser should bind wet depth threshold");

    const Fluid25DProjectConfig validation = parse_project(
        {"fluid_25d", "--headless", "--debug-view", "wet-dry", "--fluid25d-gpu-oracle-validation"});
    require(validation.gpu_oracle_validation,
            "fluid 2.5D parser should retain the explicit GPU oracle switch");
    require(validation.debug_view == "wet-dry",
            "fluid 2.5D parser should retain the selected diagnostic view");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-gpu-oracle-validation"})); },
        "fluid 2.5D should reject GPU oracle validation outside headless operation");

    const Fluid25DProjectConfig deferred =
        parse_project({"fluid_25d", "--set", "grid.size=7", "--set", "fluid25d.scenario=dry-bed",
                       "--set", "fluid25d.simulation_substeps=4"});
    require(deferred.simulation.grid_width == 7 && deferred.simulation.grid_height == 7,
            "fluid 2.5D should support the shared grid-size override");
    require(deferred.simulation.scenario == Fluid25DScenario::DryBed,
            "fluid 2.5D should support deferred scenario overrides");
    require(deferred.simulation.simulation_substeps == 4,
            "fluid 2.5D should support deferred substep overrides");

    Fluid25DConfig invalid = defaults;
    invalid.fixed_delta_seconds = 0.0F;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject a zero fixed delta");
    invalid = defaults;
    invalid.simulation_substeps = kMaxFluid25DSubsteps + 1U;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject excessive substeps");
    invalid = defaults;
    invalid.scenario = static_cast<Fluid25DScenario>(99U);
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject an invalid scenario enum");
}

void test_deterministic_scenarios() {
    using namespace cubey::projects::fluid::fluid_25d;
    require(fluid_25d_scenario_index(4, 3, 3, 2) == 11U,
            "scenario indexing should use checked row-major coordinates");
    require_throws([] { static_cast<void>(fluid_25d_scenario_index(4, 3, 4, 0)); },
                   "scenario indexing should reject an out-of-bounds x coordinate");
    require_throws([] { static_cast<void>(fluid_25d_scenario_index(4, 3, 0, 3)); },
                   "scenario indexing should reject an out-of-bounds y coordinate");
    const Fluid25DScenarioData dry_a =
        make_fluid_25d_scenario(Fluid25DScenario::DryBed, 10, 6, 1.0F);
    const Fluid25DScenarioData dry_b =
        make_fluid_25d_scenario(Fluid25DScenario::DryBed, 10, 6, 1.0F);
    require(dry_a.terrain_height_m == dry_b.terrain_height_m,
            "dry-bed terrain generation should be deterministic");
    require(dry_a.initial_water_depth_m == dry_b.initial_water_depth_m,
            "dry-bed water generation should be deterministic");
    require(std::all_of(dry_a.initial_water_depth_m.begin(), dry_a.initial_water_depth_m.end(),
                        [](float value) { return value == 0.0F; }),
            "dry-bed scenario should start without water");

    const Fluid25DScenarioData lake =
        make_fluid_25d_scenario(Fluid25DScenario::LakeAtRest, 10, 6, 1.0F);
    const auto [terrain_min, terrain_max] =
        std::minmax_element(lake.terrain_height_m.begin(), lake.terrain_height_m.end());
    require(*terrain_max - *terrain_min > 0.1F,
            "lake-at-rest fixture should contain uneven terrain");
    const float reference_surface = lake.terrain_height_m[0] + lake.initial_water_depth_m[0];
    for (std::size_t index = 0; index < lake.terrain_height_m.size(); ++index) {
        require_close(
            static_cast<double>(lake.terrain_height_m[index] + lake.initial_water_depth_m[index]),
            reference_surface, kDepthToleranceM,
            "lake-at-rest fixture should have a constant free surface");
    }

    const Fluid25DScenarioData river =
        make_fluid_25d_scenario(Fluid25DScenario::RiverCatchment, 20, 8, 1.0F);
    require(river.source_cell != kFluid25DNoCell && river.sink_cell != kFluid25DNoCell,
            "river fixture should identify one source and one sink");
    require(river.source_cell != river.sink_cell,
            "river fixture source and sink should be distinct cells");
    require(river.source_depth_rate_m_per_s[river.source_cell] > 0.0F,
            "river fixture should carry one positive source rate");
    require(river.sink_depth_rate_m_per_s[river.sink_cell] > 0.0F,
            "river fixture should carry one positive sink rate");
    require(river.initial_water_depth_m[river.sink_cell] == 0.0F,
            "river fixture sink should start dry");
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

void test_retained_flux_inertia() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(2, 1, Fluid25DScenario::DryBed);
    config.fixed_delta_seconds = 0.5F;
    config.simulation_substeps = 1;
    config.gravity_m_per_s2 = 2.5F;
    config.flow_damping_per_second = 0.2F;
    validate_fluid_25d_config(config);
    Fluid25DScenarioData scenario = make_fluid_25d_scenario(config.scenario, config.grid_width,
                                                            config.grid_height, config.cell_size_m);
    scenario.terrain_height_m[0] = 0.0F;
    scenario.terrain_height_m[1] = 0.0F;
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
    // can prove that the initially dry sink removes no water before arrival.
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
            "river sink must be dry at the start of the run");
    const Fluid25DStepLedger first_ledger = oracle.step();
    require_close(first_ledger.sink_volume_m3, 0.0, kDepthToleranceM,
                  "dry river sink must remove no water before arrival");
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

} // namespace

int main() {
    try {
        test_config_defaults_and_parsing();
        test_deterministic_scenarios();
        test_dry_bed_stability();
        test_lake_at_rest();
        test_dynamic_closed_domain_conservation();
        test_retained_flux_inertia();
        test_river_mass_and_positivity();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_25d_tests: %s\n", error.what());
        return 1;
    }
    return 0;
}
