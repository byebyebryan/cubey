#include "../../fluid_25d/fluid_25d_project_config.h"
#include "fluid_25d_commands.h"
#include "fluid_25d_diagnostics.h"
#include "fluid_25d_finite_volume_oracle.h"
#include "fluid_25d_oracle.h"
#include "fluid_25d_presentation.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <span>
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

struct TerrainFixture {
    TerrainFixture() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("cubey-fluid-25d-terrain-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
        elevation.resize(5U * 4U);
        for (std::uint32_t z = 0U; z < 4U; ++z) {
            for (std::uint32_t x = 0U; x < 5U; ++x) {
                elevation[static_cast<std::size_t>(z) * 5U + x] = static_cast<float>(z * 5U + x);
            }
        }
        const std::string hash = cubey::asset::sha256_hex(std::as_bytes(std::span{elevation}));
        manifest = {
            {"schema", "cubey.terrain.heightfield.v1"},
            {"source", {{"id", "fluid-25d-test-terrain"}}},
            {"seed", 731U},
            {"grid",
             {{"width", 5U},
              {"height", 4U},
              {"sample_spacing_m", 3.5F},
              {"sample_origin_x_m", -10.0F},
              {"sample_origin_z_m", 25.0F}}},
            {"height", {{"offset_m", 10.0F}, {"scale", 1.5F}, {"relief_scale_m", 100.0F}}},
            {"files",
             {{"elevation",
               {{"path", "elevation.f32"},
                {"dtype", "float32-le"},
                {"layout", "row-major-zx"},
                {"shape", {4U, 5U}},
                {"byte_count", elevation.size() * sizeof(float)},
                {"sha256", hash}}}}},
        };
        write();
    }

    ~TerrainFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write() const {
        std::ofstream elevation_stream(root / "elevation.f32", std::ios::binary);
        elevation_stream.write(reinterpret_cast<const char*>(elevation.data()),
                               static_cast<std::streamsize>(elevation.size() * sizeof(float)));
        if (!elevation_stream) {
            throw std::runtime_error("failed to write fluid 2.5D terrain fixture payload");
        }
        std::ofstream manifest_stream(root / "heightfield.json");
        manifest_stream << manifest.dump(2) << '\n';
        if (!manifest_stream) {
            throw std::runtime_error("failed to write fluid 2.5D terrain fixture manifest");
        }
    }

    std::filesystem::path root{};
    std::vector<float> elevation{};
    nlohmann::json manifest{};
};

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

[[nodiscard]] cubey::projects::fluid::fluid_25d::Fluid25DConfig
finite_volume_test_config(std::uint32_t width, std::uint32_t height,
                          cubey::projects::fluid::fluid_25d::Fluid25DScenario scenario) {
    cubey::projects::fluid::fluid_25d::Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.scenario = scenario;
    config.simulation_substeps = 2;
    config.solver = cubey::projects::fluid::fluid_25d::Fluid25DSolver::FiniteVolume;
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
    require(defaults.grid_width == 256U && defaults.grid_height == 128U,
            "fluid 2.5D should retain the bounded 256 by 128 product grid");
    require(defaults.cell_size_m == 1.0F, "fluid 2.5D should default to one metre cells");
    require(defaults.simulation_substeps == 2,
            "fluid 2.5D should default to two fixed solver substeps");
    require(defaults.scenario == Fluid25DScenario::RiverCatchment,
            "fluid 2.5D should default to the river catchment scenario");
    require(defaults.solver == Fluid25DSolver::VirtualPipes,
            "fluid 2.5D should retain virtual-pipes as the product default");
    require(std::string(fluid_25d_solver_name(Fluid25DSolver::FiniteVolume)) == "finite-volume" &&
                fluid_25d_solver_from_name("finite-volume") == Fluid25DSolver::FiniteVolume &&
                fluid_25d_solver_from_name("") == Fluid25DSolver::VirtualPipes,
            "fluid 2.5D finite-volume comparison solver names should be stable");
    require_throws([] { static_cast<void>(fluid_25d_solver_from_name("unknown")); },
                   "fluid 2.5D should reject unknown solver names");
    Fluid25DConfig invalid_solver = defaults;
    invalid_solver.solver = static_cast<Fluid25DSolver>(42U);
    require_throws([&] { validate_fluid_25d_config(invalid_solver); },
                   "fluid 2.5D should reject invalid solver enum values");
    require(std::string(fluid_25d_scenario_name(Fluid25DScenario::LakeAtRest)) == "lake-at-rest",
            "fluid 2.5D scenario names should be stable");
    require(fluid_25d_scenario_from_name("dry") == Fluid25DScenario::DryBed,
            "fluid 2.5D should retain the short dry scenario alias");
    require(fluid_25d_scenario_from_name("terrain-case") == Fluid25DScenario::TerrainCase,
            "fluid 2.5D should parse the explicit terrain case scenario");
    require(fluid_25d_scenario_from_name("boundary-drain-fixture") ==
                Fluid25DScenario::BoundaryDrainFixture,
            "fluid 2.5D should parse the numerical boundary drain fixture");
    require(fluid_25d_scenario_from_name("source-outlet-demo") ==
                Fluid25DScenario::SourceOutletDemo &&
                std::string(fluid_25d_scenario_name(Fluid25DScenario::SourceOutletDemo)) ==
                    "source-outlet-demo",
            "fluid 2.5D should expose the opt-in source-outlet demo distinctly from River V0");
    require(fluid_25d_scenario_from_name("mountain-source-outlet-demo") ==
                    Fluid25DScenario::MountainSourceOutletDemo &&
                std::string(fluid_25d_scenario_name(Fluid25DScenario::MountainSourceOutletDemo)) ==
                    "mountain-source-outlet-demo",
            "fluid 2.5D should expose the pinned mountain source/outlet demo distinctly");
    require(fluid_25d_is_source_outlet_demo(Fluid25DScenario::SourceOutletDemo) &&
                fluid_25d_is_source_outlet_demo(Fluid25DScenario::MountainSourceOutletDemo) &&
                !fluid_25d_is_source_outlet_demo(Fluid25DScenario::RiverCatchment),
            "fluid 2.5D should share endpoint language only between the explicit demos");
    Fluid25DConfig virtual_pipes_demo = defaults;
    virtual_pipes_demo.scenario = Fluid25DScenario::SourceOutletDemo;
    require_throws([&] { validate_fluid_25d_config(virtual_pipes_demo); },
                   "source-outlet demo should require the explicit finite-volume solver");
    virtual_pipes_demo.solver = Fluid25DSolver::FiniteVolume;
    validate_fluid_25d_config(virtual_pipes_demo);
    Fluid25DConfig mountain_demo = defaults;
    mountain_demo.scenario = Fluid25DScenario::MountainSourceOutletDemo;
    mountain_demo.solver = Fluid25DSolver::FiniteVolume;
    mountain_demo.cell_size_m = kFluid25DMountainSourceOutletCellSizeM;
    validate_fluid_25d_config(mountain_demo);
    mountain_demo.grid_width -= 1U;
    require_throws([&] { validate_fluid_25d_config(mountain_demo); },
                   "mountain source/outlet demo should require its pinned width");
    mountain_demo = defaults;
    mountain_demo.scenario = Fluid25DScenario::MountainSourceOutletDemo;
    mountain_demo.solver = Fluid25DSolver::FiniteVolume;
    require_throws([&] { validate_fluid_25d_config(mountain_demo); },
                   "mountain source/outlet demo should require native 30 metre cells");
    require(fluid_25d_terrain_water_protocol_from_name("rain-pulse") ==
                Fluid25DTerrainWaterProtocol::RainPulse,
            "fluid 2.5D should parse the rain-pulse terrain-water protocol");
    require(fluid_25d_terrain_water_protocol_from_name("sheet-release") ==
                Fluid25DTerrainWaterProtocol::SheetRelease,
            "fluid 2.5D should parse the sheet-release terrain-water protocol");
    require_throws([] { static_cast<void>(fluid_25d_terrain_water_protocol_from_name("inflow")); },
                   "fluid 2.5D should reject unsupported terrain-water protocols");
    require_close(fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(3600.0F), 0.001,
                  kDepthToleranceM, "fluid 2.5D should convert rainfall mm/hour to depth rate m/s");
    require_throws(
        [] { static_cast<void>(fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(-1.0F)); },
        "fluid 2.5D should reject negative rainfall conversion inputs");
    require_throws([] { static_cast<void>(fluid_25d_scenario_from_name("unknown")); },
                   "fluid 2.5D should reject unknown scenario names");
    require(fluid_25d_debug_view_from_name("flow") == Fluid25DDebugView::FlowMagnitude,
            "fluid 2.5D should parse the flow diagnostic view");
    require_throws([] { static_cast<void>(fluid_25d_debug_view_from_name("unknown")); },
                   "fluid 2.5D should reject unknown diagnostic views");
    require(fluid_25d_presentation_view_from_name("catchment") ==
                Fluid25DPresentationView::Catchment,
            "fluid 2.5D should select the product catchment view explicitly");
    require(fluid_25d_catchment_view_from_name("composite") ==
                Fluid25DCatchmentView::Composite,
            "fluid 2.5D should retain Composite as the default catchment presentation");
    require_throws(
        [] { static_cast<void>(fluid_25d_presentation_view_from_name("composite")); },
        "fluid 2.5D surface selector should keep catchment modes in their own enum");
    require(fluid_25d_catchment_view_from_name("water-isolation") ==
                Fluid25DCatchmentView::WaterIsolation,
            "fluid 2.5D should parse the water-isolation presentation mode");
    require(fluid_25d_catchment_view_from_name("flow-inspection") ==
                Fluid25DCatchmentView::FlowInspection,
            "fluid 2.5D should parse the flow-inspection presentation mode");
    require(fluid_25d_presentation_view_from_name("diagnostics") ==
                Fluid25DPresentationView::Diagnostics,
            "fluid 2.5D should retain an explicit diagnostic presentation mode");
    require(std::string(fluid_25d_presentation_view_name(Fluid25DPresentationView::Catchment)) ==
                "Catchment" &&
                std::string(fluid_25d_catchment_view_name(Fluid25DCatchmentView::Composite)) ==
                    "Composite",
            "fluid 2.5D surface and catchment presentation names should stay distinct");
    require_throws(
        [] { static_cast<void>(fluid_25d_catchment_view_from_name("diagnostics")); },
        "fluid 2.5D catchment presentation should reject diagnostics");
    require_throws([] { static_cast<void>(fluid_25d_presentation_view_from_name("unknown")); },
                   "fluid 2.5D should reject unknown presentation views");
    require(fluid_25d_mesh_vertex_count(Fluid25DConfig{
                .grid_width = 2U,
                .grid_height = 2U,
            }) == 6U,
            "fluid 2.5D should generate six procedural vertices per grid quad");
    require_throws(
        [] {
            static_cast<void>(fluid_25d_mesh_vertex_count(Fluid25DConfig{
                .grid_width = 1U,
                .grid_height = 2U,
            }));
        },
        "fluid 2.5D should reject product meshes with fewer than two cells per axis");

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
    require(parsed.presentation_time_scale == kFluid25DDefaultWindowedPresentationTimeScale,
            "fluid 2.5D should default windowed presentation playback to 1x");
    require(parsed.view.empty(),
            "fluid 2.5D should default the CLI view name to the catchment enum default");
    require(parsed.catchment_view.empty(),
            "fluid 2.5D should default the catchment presentation to Composite");
    require(!parsed.gpu_oracle_validation,
            "fluid 2.5D should keep solver readback disabled unless explicitly requested");
    const Fluid25DProjectConfig finite_volume =
        parse_project({"fluid_25d", "--fluid25d-solver", "finite-volume"});
    require(finite_volume.simulation.solver == Fluid25DSolver::FiniteVolume,
            "fluid 2.5D parser should opt into the finite-volume comparison solver");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-solver", "not-a-solver"}));
        },
        "fluid 2.5D parser should reject an unknown solver");

    const Fluid25DProjectConfig scheduled =
        parse_project({"fluid_25d", "--fluid25d-source-active-duration-seconds", "0.05"});
    require(scheduled.simulation.source_active_duration_seconds == 0.05F,
            "fluid 2.5D parser should retain an optional source active duration");

    const Fluid25DProjectConfig playback = parse_project(
        {"fluid_25d", "--fluid25d-presentation-time-scale", "4"});
    require(playback.presentation_time_scale == 4.0F,
            "fluid 2.5D parser should bind the windowed presentation time scale");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--headless", "--fluid25d-presentation-time-scale", "4"}));
        },
        "fluid 2.5D presentation time scale should be rejected in headless mode");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--fluid25d-presentation-time-scale", "0"}));
        },
        "fluid 2.5D presentation time scale should reject zero");
    require_throws(
        [] { validate_fluid_25d_windowed_presentation_time_scale(0.1F); },
        "fluid 2.5D presentation time scale should reject values below the named minimum");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--fluid25d-presentation-time-scale", "9"}));
        },
        "fluid 2.5D presentation time scale should reject values above the review bound");

    const Fluid25DProjectConfig water_isolation = parse_project(
        {"fluid_25d", "--fluid25d-catchment-view", "water-isolation"});
    require(water_isolation.catchment_view == "water-isolation",
            "fluid 2.5D parser should propagate the water-isolation presentation mode");
    const Fluid25DProjectConfig flow_inspection = parse_project(
        {"fluid_25d", "--fluid25d-catchment-view", "flow-inspection"});
    require(flow_inspection.catchment_view == "flow-inspection",
            "fluid 2.5D parser should propagate the flow-inspection presentation mode");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-catchment-view",
                                              "not-a-catchment-view"}));
        },
        "fluid 2.5D parser should reject an unknown catchment presentation mode");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-view", "diagnostics",
                                              "--fluid25d-catchment-view", "composite"}));
        },
        "fluid 2.5D parser should reject diagnostics combined with a catchment mode");

    const Fluid25DProjectConfig terrain = parse_project(
        {"fluid_25d", "--fluid25d-scenario", "terrain-case", "--terrain-heightfield",
         "terrain-fixture", "--fluid25d-terrain-crop-x", "12", "--fluid25d-terrain-crop-z", "7"});
    require(terrain.simulation.scenario == Fluid25DScenario::TerrainCase,
            "fluid 2.5D parser should bind the terrain case scenario");
    require(terrain.terrain.heightfield_path == "terrain-fixture" &&
                terrain.terrain.crop_x == 12U && terrain.terrain.crop_z == 7U,
            "fluid 2.5D parser should retain the terrain path and native crop coordinates");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--terrain-heightfield", "terrain"})); },
        "fluid 2.5D should reject a terrain path with an analytic scenario");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case"}));
        },
        "fluid 2.5D should reject terrain-case without a terrain path");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-terrain-crop-x", "1"})); },
        "fluid 2.5D should reject terrain crop options for an analytic scenario");

    const Fluid25DProjectConfig mountain = parse_project(
        {"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo", "--terrain-heightfield",
         "terrain-fixture", "--fluid25d-solver", "finite-volume"});
    require(
        mountain.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo &&
            mountain.simulation.solver == Fluid25DSolver::FiniteVolume &&
            mountain.simulation.grid_width == kFluid25DMountainSourceOutletGridWidth &&
            mountain.simulation.grid_height == kFluid25DMountainSourceOutletGridHeight &&
            mountain.simulation.cell_size_m == kFluid25DMountainSourceOutletCellSizeM,
        "mountain source/outlet parser should establish its pinned finite-volume grid contract");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--fluid25d-solver", "finite-volume"}));
        },
        "mountain source/outlet demo should require a terrain heightfield");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture"}));
        },
        "mountain source/outlet demo should require finite-volume explicitly");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--grid-width", "255"}));
        },
        "mountain source/outlet demo should reject non-pinned grid dimensions");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--grid-height", "127"}));
        },
        "mountain source/outlet demo should reject non-pinned grid height");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-cell-size-m", "20"}));
        },
        "mountain source/outlet demo should reject non-native cell spacing");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-terrain-crop-x", "1"}));
        },
        "mountain source/outlet demo should reject crop overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-terrain-water-protocol", "none"}));
        },
        "mountain source/outlet demo should reject terrain-water protocol overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-rainfall-rate-mm-per-hour", "1"}));
        },
        "mountain source/outlet demo should reject rainfall forcing overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-sheet-depth-m", "0.01"}));
        },
        "mountain source/outlet demo should reject sheet-release forcing overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-source-active-duration-seconds", "1"}));
        },
        "mountain source/outlet demo should reject terrain-water forcing duration overrides");

    const Fluid25DProjectConfig rain_pulse =
        parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case", "--terrain-heightfield",
                       "terrain-fixture", "--fluid25d-terrain-water-protocol", "rain-pulse",
                       "--fluid25d-rainfall-rate-mm-per-hour", "720",
                       "--fluid25d-source-active-duration-seconds", "2.5"});
    require(rain_pulse.simulation.terrain_water_protocol ==
                    Fluid25DTerrainWaterProtocol::RainPulse &&
                rain_pulse.simulation.rainfall_depth_rate_m_per_s > 0.0F &&
                rain_pulse.simulation.source_active_duration_seconds == 2.5F,
            "fluid 2.5D should retain the complete rain-pulse contract");
    const Fluid25DProjectConfig sheet_release =
        parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case", "--terrain-heightfield",
                       "terrain-fixture", "--fluid25d-terrain-water-protocol", "sheet-release",
                       "--fluid25d-sheet-depth-m", "0.025"});
    require(sheet_release.simulation.terrain_water_protocol ==
                    Fluid25DTerrainWaterProtocol::SheetRelease &&
                sheet_release.simulation.sheet_initial_depth_m == 0.025F,
            "fluid 2.5D should retain the complete sheet-release contract");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case",
                                             "--terrain-heightfield", "terrain-fixture",
                                             "--fluid25d-terrain-water-protocol", "rain-pulse",
                                             "--fluid25d-rainfall-rate-mm-per-hour", "720"}));
        },
        "fluid 2.5D should require a fixed source duration for rain-pulse");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-terrain-water-protocol", "none"}));
        },
        "fluid 2.5D should reject explicit terrain-water protocol outside terrain-case");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case",
                                             "--terrain-heightfield", "terrain-fixture",
                                             "--fluid25d-sheet-depth-m", "0.025"}));
        },
        "fluid 2.5D should reject forcing parameters with terrain protocol none");

    const Fluid25DProjectConfig diagnostic_presentation =
        parse_project({"fluid_25d", "--fluid25d-view", "diagnostics", "--debug-view", "wet-dry"});
    require(diagnostic_presentation.view == "diagnostics",
            "fluid 2.5D parser should preserve the selected presentation view");

    const Fluid25DProjectConfig validation = parse_project(
        {"fluid_25d", "--headless", "--debug-view", "wet-dry", "--fluid25d-gpu-oracle-validation"});
    require(validation.gpu_oracle_validation,
            "fluid 2.5D parser should retain the explicit GPU oracle switch");
    require(validation.debug_view == "wet-dry",
            "fluid 2.5D parser should retain the selected diagnostic view");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-gpu-oracle-validation"})); },
        "fluid 2.5D should reject GPU oracle validation outside headless operation");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--profile-output", "fluid-profile", "--profile-diagnostics"}));
        },
        "fluid 2.5D should reject profile diagnostics outside headless operation");

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
    invalid = defaults;
    invalid.grid_height = 1U;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject product grids with fewer than two rows");
    invalid = defaults;
    invalid.source_active_duration_seconds = -0.1F;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject a negative source active duration");
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
    require(river.initial_water_depth_m[river.source_cell] > 0.0F &&
                river.initial_water_depth_m[river.sink_cell] == 0.0F,
            "River V0 should retain its narrow initially dry-outlet fixture");

    const Fluid25DScenarioData readable_river =
        make_fluid_25d_scenario(Fluid25DScenario::SourceOutletDemo, 128U, 64U, 1.0F);
    require(readable_river.source_cell != kFluid25DNoCell &&
                readable_river.sink_cell != kFluid25DNoCell &&
                readable_river.source_cell != readable_river.sink_cell,
            "source-outlet demo should identify distinct endpoint centroids");
    require(readable_river.initial_water_depth_m[readable_river.source_cell] > 0.0F &&
                readable_river.initial_water_depth_m[readable_river.sink_cell] > 0.0F,
            "source-outlet demo should seed a visible route through both endpoint regions");
    require(
        readable_river.initial_water_depth_m[readable_river.sink_cell] >
            readable_river.initial_water_depth_m[readable_river.source_cell],
        "source-outlet demo should prime its terminal basin without changing endpoint capacity");
    std::uint32_t source_region_cells = 0U;
    std::uint32_t sink_region_cells = 0U;
    float source_rate_sum = 0.0F;
    float sink_rate_sum = 0.0F;
    bool overlapping_endpoint_rates = false;
    for (std::size_t index = 0U; index < readable_river.source_depth_rate_m_per_s.size(); ++index) {
        const float source_rate = readable_river.source_depth_rate_m_per_s[index];
        const float sink_rate = readable_river.sink_depth_rate_m_per_s[index];
        source_region_cells += source_rate > 0.0F ? 1U : 0U;
        sink_region_cells += sink_rate > 0.0F ? 1U : 0U;
        source_rate_sum += source_rate;
        sink_rate_sum += sink_rate;
        overlapping_endpoint_rates =
            overlapping_endpoint_rates || (source_rate > 0.0F && sink_rate > 0.0F);
    }
    require(source_region_cells > 1U && sink_region_cells > 1U && !overlapping_endpoint_rates,
            "source-outlet demo should retain separate bounded endpoint regions");
    require_close(source_rate_sum, 0.03, kDepthToleranceM,
                  "source-outlet demo source should retain its total physical throughput");
    require_close(sink_rate_sum, 0.03, kDepthToleranceM,
                  "source-outlet demo outlet should retain its total physical throughput");

    const std::uint32_t readable_source_x =
        static_cast<std::uint32_t>(readable_river.source_cell % readable_river.width);
    const std::uint32_t readable_sink_x =
        static_cast<std::uint32_t>(readable_river.sink_cell % readable_river.width);
    require(readable_source_x == 14U && readable_sink_x == 110U &&
                readable_sink_x - readable_source_x == 96U,
            "source-outlet demo should retain its long 128 by 64 endpoint route");
    require_close(fluid_25d_catchment_height_scale(Fluid25DScenario::RiverCatchment), 0.08,
                  kDepthToleranceM,
                  "River V0 should retain the shared restrained render height scale");
    require_close(fluid_25d_catchment_height_scale(Fluid25DScenario::SourceOutletDemo), 0.65,
                  kDepthToleranceM,
                  "source-outlet demo should select its render-only relief scale");
    require_close(fluid_25d_catchment_height_scale(Fluid25DScenario::MountainSourceOutletDemo),
                  0.60, kDepthToleranceM,
                  "mountain demo should select its own render-only relief scale");
    require_close(
        fluid_25d_catchment_home_horizontal_extent(Fluid25DScenario::RiverCatchment, 127.0F, 96.0F),
        127.0, kDepthToleranceM,
        "River V0 home camera should retain its full-domain framing contract");
    require_close(fluid_25d_catchment_home_horizontal_extent(Fluid25DScenario::SourceOutletDemo,
                                                             127.0F, 96.0F),
                  120.0, kDepthToleranceM,
                  "source-outlet demo home camera should fit its long route without empty margins");
    require_close(fluid_25d_catchment_home_horizontal_extent(
                      Fluid25DScenario::MountainSourceOutletDemo, 7650.0F, 6720.0F),
                  7929.6, 0.001,
                  "mountain demo home camera should frame its full 6.97 kilometre route");
    require_close(fluid_25d_catchment_home_pitch(-0.92F, Fluid25DScenario::RiverCatchment), -0.92,
                  kDepthToleranceM,
                  "River V0 home camera should retain its original oblique pitch");
    require_close(fluid_25d_catchment_home_pitch(-0.92F, Fluid25DScenario::SourceOutletDemo), -0.72,
                  kDepthToleranceM,
                  "source-outlet demo home camera should expose its render-only relief");
    require_close(
        fluid_25d_catchment_home_pitch(-0.92F, Fluid25DScenario::MountainSourceOutletDemo), -0.55,
        kDepthToleranceM, "mountain demo home camera should expose its kilometre-scale relief");
    require_close(fluid_25d_catchment_home_distance_scale(Fluid25DScenario::RiverCatchment), 1.05,
                  kDepthToleranceM, "River V0 should retain its original home camera distance");
    require_close(
        fluid_25d_catchment_home_distance_scale(Fluid25DScenario::MountainSourceOutletDemo), 0.85,
        kDepthToleranceM, "mountain demo home camera should fill an overview with its full route");
    require_close(
        fluid_25d_catchment_home_fovy_radians(1.0471975512F, Fluid25DScenario::RiverCatchment),
        1.0471975512, kDepthToleranceM,
        "River V0 should retain its original home camera field of view");
    require_close(fluid_25d_catchment_home_fovy_radians(1.0471975512F,
                                                        Fluid25DScenario::MountainSourceOutletDemo),
                  0.84, kDepthToleranceM,
                  "mountain demo home camera should give the full route useful capture scale");
    require_close(fluid_25d_catchment_terrain_material_cue(Fluid25DScenario::RiverCatchment), 0.0,
                  kDepthToleranceM, "River V0 should retain the shared terrain material");
    require_close(fluid_25d_catchment_terrain_material_cue(Fluid25DScenario::SourceOutletDemo), 1.0,
                  kDepthToleranceM,
                  "source-outlet demo should select its render-only terrain height and slope cue");
    require_close(
        fluid_25d_catchment_terrain_material_cue(Fluid25DScenario::MountainSourceOutletDemo), 2.0,
        kDepthToleranceM,
        "mountain demo should select its separate immutable-crop terrain material cue");
    const float outlet_bed_height_m = readable_river.terrain_height_m[readable_river.sink_cell];
    const float source_bed_height_m = readable_river.terrain_height_m[readable_river.source_cell];
    float minimum_ribbon_center_y = std::numeric_limits<float>::infinity();
    float maximum_ribbon_center_y = -std::numeric_limits<float>::infinity();
    float maximum_transverse_terrain_relief_m = 0.0F;
    std::uint32_t narrowest_ribbon_cells = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t widest_ribbon_cells = 0U;
    for (std::uint32_t x = readable_source_x; x <= readable_sink_x; ++x) {
        std::uint32_t wet_cells = 0U;
        float weighted_y_sum = 0.0F;
        float depth_sum = 0.0F;
        for (std::uint32_t y = 0U; y < readable_river.height; ++y) {
            const float depth = readable_river.initial_water_depth_m[
                fluid_25d_scenario_index(readable_river.width, readable_river.height, x, y)];
            if (depth > 0.0F) {
                ++wet_cells;
                weighted_y_sum += static_cast<float>(y) * depth;
                depth_sum += depth;
            }
        }
        require(wet_cells >= 4U && depth_sum > 0.0F,
                "river fixture should seed a broad connected water ribbon in every route column");
        narrowest_ribbon_cells = std::min(narrowest_ribbon_cells, wet_cells);
        widest_ribbon_cells = std::max(widest_ribbon_cells, wet_cells);
        const float ribbon_center_y = weighted_y_sum / depth_sum;
        minimum_ribbon_center_y = std::min(minimum_ribbon_center_y, ribbon_center_y);
        maximum_ribbon_center_y = std::max(maximum_ribbon_center_y, ribbon_center_y);
        float transverse_minimum = std::numeric_limits<float>::infinity();
        float transverse_maximum = -std::numeric_limits<float>::infinity();
        for (std::uint32_t y = 0U; y < readable_river.height; ++y) {
            const float terrain = readable_river.terrain_height_m[fluid_25d_scenario_index(
                readable_river.width, readable_river.height, x, y)];
            transverse_minimum = std::min(transverse_minimum, terrain);
            transverse_maximum = std::max(transverse_maximum, terrain);
        }
        maximum_transverse_terrain_relief_m =
            std::max(maximum_transverse_terrain_relief_m, transverse_maximum - transverse_minimum);
    }
    require(maximum_ribbon_center_y - minimum_ribbon_center_y > 12.0F,
            "source-outlet demo should retain multiple broad terrain-guided bends");
    require(widest_ribbon_cells >= narrowest_ribbon_cells + 4U,
            "source-outlet demo should retain visible width variation and one constriction");
    require(maximum_transverse_terrain_relief_m > 2.8F,
            "source-outlet demo should retain readable broad dry-bank relief");
    require(source_bed_height_m > outlet_bed_height_m + 2.0F,
            "source-outlet demo should keep a meaningful smooth fall into its outlet basin");
    for (std::uint32_t x = readable_sink_x + 1U; x < readable_river.width; ++x) {
        float downstream_valley_floor_m = std::numeric_limits<float>::infinity();
        for (std::uint32_t y = 0U; y < readable_river.height; ++y) {
            const std::size_t index =
                fluid_25d_scenario_index(readable_river.width, readable_river.height, x, y);
            downstream_valley_floor_m =
                std::min(downstream_valley_floor_m, readable_river.terrain_height_m[index]);
            require(readable_river.initial_water_depth_m[index] == 0.0F,
                    "source-outlet demo should not seed water beyond its terminal outlet");
        }
        require(downstream_valley_floor_m > outlet_bed_height_m + 0.05F,
                "source-outlet demo should raise the closed downstream shoulder above the outlet");
    }
    require(!dry_a.terrain_provenance.has_value() && !lake.terrain_provenance.has_value() &&
                !river.terrain_provenance.has_value(),
            "analytic fixtures should not acquire terrain-case provenance");
    require(std::all_of(dry_a.boundary_outflow_face_mask.begin(),
                        dry_a.boundary_outflow_face_mask.end(),
                        [](std::uint32_t mask) { return mask == 0U; }) &&
                std::all_of(lake.boundary_outflow_face_mask.begin(),
                            lake.boundary_outflow_face_mask.end(),
                            [](std::uint32_t mask) { return mask == 0U; }) &&
                std::all_of(river.boundary_outflow_face_mask.begin(),
                            river.boundary_outflow_face_mask.end(),
                            [](std::uint32_t mask) { return mask == 0U; }),
            "analytic fixtures should retain closed outer boundaries by default");
}

void test_terrain_case_ingestion() {
    using namespace cubey::projects::fluid::fluid_25d;
    TerrainFixture fixture;
    Fluid25DConfig config = test_config(3U, 2U, Fluid25DScenario::TerrainCase);
    config.cell_size_m = 3.5F;
    validate_fluid_25d_config(config);

    Fluid25DProjectConfig near_equal_project{};
    near_equal_project.simulation = config;
    near_equal_project.fluid.cell_size_m = std::nextafter(3.5F, 4.0F);
    resolve_fluid_25d_terrain_cell_size(near_equal_project, 3.5F);
    require(near_equal_project.simulation.cell_size_m == 3.5F,
            "terrain startup should canonicalize an explicitly near-equal cell size");

    const Fluid25DScenarioData terrain =
        load_fluid_25d_terrain_scenario(config, fixture.root, 1U, 1U);
    require(terrain.width == 3U && terrain.height == 2U,
            "terrain case should retain the configured crop dimensions");
    require_close(terrain.cell_size_m, 3.5, kDepthToleranceM,
                  "terrain case should propagate native sample spacing");
    const std::vector<float> expected{24.0F, 25.5F, 27.0F, 31.5F, 33.0F, 34.5F};
    require(terrain.terrain_height_m == expected,
            "terrain case should apply the manifest height transform at native samples");
    require(std::all_of(terrain.initial_water_depth_m.begin(), terrain.initial_water_depth_m.end(),
                        [](float value) { return value == 0.0F; }) &&
                std::all_of(terrain.source_depth_rate_m_per_s.begin(),
                            terrain.source_depth_rate_m_per_s.end(),
                            [](float value) { return value == 0.0F; }) &&
                std::all_of(terrain.sink_depth_rate_m_per_s.begin(),
                            terrain.sink_depth_rate_m_per_s.end(),
                            [](float value) { return value == 0.0F; }),
            "terrain case should begin with zero water and no source or sink rates");
    require(terrain.source_cell == kFluid25DNoCell && terrain.sink_cell == kFluid25DNoCell,
            "terrain case should not invent source or sink cells");

    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    const Fluid25DScenarioData crop =
        make_fluid_25d_terrain_crop(3U, 2U, 3.5F, source, 1U, 1U);
    require(crop.width == terrain.width && crop.height == terrain.height &&
                crop.cell_size_m == terrain.cell_size_m &&
                crop.terrain_height_m == terrain.terrain_height_m &&
                crop.initial_water_depth_m == terrain.initial_water_depth_m &&
                crop.source_depth_rate_m_per_s == terrain.source_depth_rate_m_per_s &&
                crop.sink_depth_rate_m_per_s == terrain.sink_depth_rate_m_per_s &&
                crop.boundary_outflow_face_mask == terrain.boundary_outflow_face_mask,
            "scenario-neutral terrain crop helper should preserve the neutral terrain fields");
    require_throws(
        [&] {
            static_cast<void>(make_fluid_25d_terrain_crop(0U, 2U, 3.5F, source, 1U, 1U));
        },
        "scenario-neutral terrain crop helper should reject zero dimensions");
    require_throws(
        [&] {
            static_cast<void>(make_fluid_25d_terrain_crop(3U, 2U, 0.0F, source, 1U, 1U));
        },
        "scenario-neutral terrain crop helper should reject non-positive spacing");
    require(terrain.terrain_provenance.has_value(),
            "terrain case should expose provenance for inspection");
    const Fluid25DTerrainCaseProvenance& provenance = terrain.terrain_provenance.value();
    require(provenance.source_id == "fluid-25d-test-terrain" && provenance.crop_x == 1U &&
                provenance.crop_z == 1U && provenance.crop_width == 3U &&
                provenance.crop_height == 2U,
            "terrain case provenance should retain source and crop identity fields");
    require(provenance.elevation_sha256 ==
                fixture.manifest["files"]["elevation"]["sha256"].get<std::string>(),
            "terrain case provenance should retain the elevation SHA-256");
    require(provenance.transformed_crop_sha256.size() == 64U &&
                provenance.identity.find("elevation-sha256=" + provenance.elevation_sha256) !=
                    std::string::npos &&
                provenance.identity.find("crop-sha256=" + provenance.transformed_crop_sha256) !=
                    std::string::npos &&
                provenance.identity.find("crop=1,1,3x2") != std::string::npos &&
                provenance.identity.find("spacing-m=3.500000") != std::string::npos,
            "terrain case identity should include source/crop digests, crop, and spacing");
    require(provenance.manifest_path ==
                std::filesystem::absolute(fixture.root / "heightfield.json").lexically_normal(),
            "terrain case provenance should expose the normalized manifest path");

    const Fluid25DTerrainCaseProvenance baseline_provenance = provenance;
    fixture.manifest["height"]["offset_m"] = 11.0F;
    fixture.write();
    const Fluid25DScenarioData transformed =
        load_fluid_25d_terrain_scenario(config, fixture.root, 1U, 1U);
    require(transformed.terrain_provenance.has_value(),
            "terrain case should retain provenance after a source transform change");
    require(transformed.terrain_provenance->elevation_sha256 ==
                baseline_provenance.elevation_sha256,
            "terrain transform changes should retain the original elevation SHA-256");
    require(transformed.terrain_provenance->transformed_crop_sha256 !=
                    baseline_provenance.transformed_crop_sha256 &&
                transformed.terrain_provenance->identity != baseline_provenance.identity,
            "terrain transform changes should change the transformed crop digest and identity");

    require_throws(
        [&] { static_cast<void>(load_fluid_25d_terrain_scenario(config, fixture.root, 3U, 1U)); },
        "terrain case should reject an x crop that exceeds the source bounds");
    require_throws(
        [&] { static_cast<void>(load_fluid_25d_terrain_scenario(config, fixture.root, 1U, 3U)); },
        "terrain case should reject a z crop that exceeds the source bounds");
    Fluid25DConfig conflicting = config;
    conflicting.cell_size_m = 1.0F;
    require_throws(
        [&] {
            static_cast<void>(load_fluid_25d_terrain_scenario(conflicting, fixture.root, 1U, 1U));
        },
        "terrain case should reject an explicit cell size that conflicts with native spacing");
    require_throws(
        [&] {
            static_cast<void>(load_fluid_25d_terrain_scenario(
                config, fixture.root / "missing-heightfield", 0U, 0U));
        },
        "terrain case should reject a missing heightfield path");
    require_throws(
        [&] {
            static_cast<void>(
                load_fluid_25d_terrain_scenario(config, fixture.root / "invalid.txt", 0U, 0U));
        },
        "terrain case should reject an invalid heightfield path");
}

void test_terrain_water_protocol_construction() {
    using namespace cubey::projects::fluid::fluid_25d;
    TerrainFixture fixture;
    Fluid25DConfig rain_config = test_config(3U, 2U, Fluid25DScenario::TerrainCase);
    rain_config.cell_size_m = 3.5F;
    rain_config.terrain_water_protocol = Fluid25DTerrainWaterProtocol::RainPulse;
    rain_config.rainfall_depth_rate_m_per_s =
        fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(900.0F);
    rain_config.source_active_duration_seconds = 1.0F;
    const Fluid25DScenarioData rain =
        load_fluid_25d_terrain_scenario(rain_config, fixture.root, 1U, 1U);
    require(std::all_of(rain.initial_water_depth_m.begin(), rain.initial_water_depth_m.end(),
                        [](float value) { return value == 0.0F; }) &&
                std::all_of(rain.source_depth_rate_m_per_s.begin(),
                            rain.source_depth_rate_m_per_s.end(),
                            [&rain_config](float value) {
                                return value == rain_config.rainfall_depth_rate_m_per_s;
                            }),
            "rain-pulse should construct one uniform source field over immutable terrain");
    require(rain.boundary_outflow_face_mask ==
                make_fluid_25d_all_outward_boundary_outflow_mask(rain.width, rain.height),
            "rain-pulse should open every outward perimeter face");

    Fluid25DConfig sheet_config = test_config(3U, 2U, Fluid25DScenario::TerrainCase);
    sheet_config.cell_size_m = 3.5F;
    sheet_config.terrain_water_protocol = Fluid25DTerrainWaterProtocol::SheetRelease;
    sheet_config.sheet_initial_depth_m = 0.03F;
    const Fluid25DScenarioData sheet =
        load_fluid_25d_terrain_scenario(sheet_config, fixture.root, 1U, 1U);
    require(std::all_of(sheet.initial_water_depth_m.begin(), sheet.initial_water_depth_m.end(),
                        [&sheet_config](float value) {
                            return value == sheet_config.sheet_initial_depth_m;
                        }) &&
                std::all_of(sheet.source_depth_rate_m_per_s.begin(),
                            sheet.source_depth_rate_m_per_s.end(),
                            [](float value) { return value == 0.0F; }),
            "sheet-release should construct one uniform initial-depth field with no source");
    require(sheet.boundary_outflow_face_mask ==
                make_fluid_25d_all_outward_boundary_outflow_mask(sheet.width, sheet.height),
            "sheet-release should open every outward perimeter face");

    Fluid25DConfig invalid = sheet_config;
    invalid.sheet_initial_depth_m = 0.0F;
    require_throws(
        [&] { static_cast<void>(load_fluid_25d_terrain_scenario(invalid, fixture.root, 1U, 1U)); },
        "sheet-release construction should fail closed for zero depth");
}

void test_mountain_source_outlet_field_construction() {
    using namespace cubey::projects::fluid::fluid_25d;
    const std::size_t cell_count = fluid_25d_scenario_cell_count(
        kFluid25DMountainSourceOutletGridWidth, kFluid25DMountainSourceOutletGridHeight);
    Fluid25DScenarioData mountain{
        .width = kFluid25DMountainSourceOutletGridWidth,
        .height = kFluid25DMountainSourceOutletGridHeight,
        .cell_size_m = kFluid25DMountainSourceOutletCellSizeM,
        .terrain_height_m = std::vector<float>(cell_count, 0.0F),
        .initial_water_depth_m = std::vector<float>(cell_count, 1.0F),
        .source_depth_rate_m_per_s = std::vector<float>(cell_count, 1.0F),
        .sink_depth_rate_m_per_s = std::vector<float>(cell_count, 1.0F),
        .boundary_outflow_face_mask = std::vector<std::uint32_t>(cell_count, 7U),
    };
    for (std::size_t index = 0U; index < cell_count; ++index) {
        mountain.terrain_height_m[index] = static_cast<float>(index) * 0.001F;
    }
    const std::string expected_identity = fluid_25d_terrain_case_identity(
        kFluid25DMountainSourceOutletElevationSha256, kFluid25DMountainSourceOutletCropSha256,
        kFluid25DMountainSourceOutletCropX, kFluid25DMountainSourceOutletCropZ,
        kFluid25DMountainSourceOutletGridWidth, kFluid25DMountainSourceOutletGridHeight,
        kFluid25DMountainSourceOutletCellSizeM);
    mountain.terrain_provenance = Fluid25DTerrainCaseProvenance{
        .manifest_path = "synthetic-mountain-heightfield.json",
        .source_id = "synthetic-mountain-source",
        .elevation_sha256 = std::string(kFluid25DMountainSourceOutletElevationSha256),
        .transformed_crop_sha256 = std::string(kFluid25DMountainSourceOutletCropSha256),
        .crop_x = kFluid25DMountainSourceOutletCropX,
        .crop_z = kFluid25DMountainSourceOutletCropZ,
        .crop_width = kFluid25DMountainSourceOutletGridWidth,
        .crop_height = kFluid25DMountainSourceOutletGridHeight,
        .sample_spacing_m = kFluid25DMountainSourceOutletCellSizeM,
        .identity = expected_identity,
    };
    const std::vector<float> immutable_terrain = mountain.terrain_height_m;
    validate_fluid_25d_mountain_source_outlet_terrain_identity(mountain);
    author_fluid_25d_mountain_source_outlet_fields(mountain);

    require(mountain.terrain_height_m == immutable_terrain,
            "mountain source/outlet authoring should not modify imported terrain");
    require(mountain.terrain_provenance.has_value() &&
                mountain.terrain_provenance->identity == expected_identity &&
                mountain.terrain_provenance->source_id == "synthetic-mountain-source",
            "mountain source/outlet authoring should retain immutable terrain provenance");
    validate_fluid_25d_mountain_source_outlet_terrain_identity(mountain);
    require(mountain.source_cell ==
                    fluid_25d_scenario_index(mountain.width, mountain.height,
                                             kFluid25DMountainSourceOutletSourceX,
                                             kFluid25DMountainSourceOutletSourceZ) &&
                mountain.sink_cell == fluid_25d_scenario_index(mountain.width, mountain.height,
                                                               kFluid25DMountainSourceOutletSinkX,
                                                               kFluid25DMountainSourceOutletSinkZ),
            "mountain source/outlet authoring should retain reviewed endpoint centroids");

    std::size_t source_cells = 0U;
    std::size_t sink_cells = 0U;
    std::size_t visible_sink_disk_cells = 0U;
    double source_depth_rate_sum_m_per_s = 0.0;
    double sink_depth_rate_sum_m_per_s = 0.0;
    for (std::size_t index = 0U; index < cell_count; ++index) {
        const std::uint32_t x = static_cast<std::uint32_t>(index % mountain.width);
        const std::uint32_t z = static_cast<std::uint32_t>(index / mountain.width);
        const bool source = mountain.source_depth_rate_m_per_s[index] > 0.0F;
        const bool sink = mountain.sink_depth_rate_m_per_s[index] > 0.0F;
        const std::int32_t sink_dx = static_cast<std::int32_t>(x) -
                                     static_cast<std::int32_t>(kFluid25DMountainSourceOutletSinkX);
        const std::int32_t sink_dz = static_cast<std::int32_t>(z) -
                                     static_cast<std::int32_t>(kFluid25DMountainSourceOutletSinkZ);
        const bool visible_sink = (sink_dx * sink_dx) + (sink_dz * sink_dz) <= 25;
        require(!(source && sink),
                "mountain source/outlet endpoint regions should remain non-overlapping");
        if (sink) {
            require(visible_sink && fluid_25d_mountain_source_outlet_is_drain_cell(x, z) &&
                        mountain.initial_water_depth_m[index] >= 2.0F,
                    "mountain active outlet cells should be the reviewed visible drain reserve");
        }
        source_cells += source ? 1U : 0U;
        sink_cells += sink ? 1U : 0U;
        visible_sink_disk_cells += visible_sink ? 1U : 0U;
        source_depth_rate_sum_m_per_s += mountain.source_depth_rate_m_per_s[index];
        sink_depth_rate_sum_m_per_s += mountain.sink_depth_rate_m_per_s[index];
    }
    require(source_cells == 81U && visible_sink_disk_cells == 81U && sink_cells == 3U,
            "mountain source and visible outlet disks should cover 81 cells, with three reviewed "
            "drains");
    constexpr double kMountainCellAreaM2 =
        static_cast<double>(kFluid25DMountainSourceOutletCellSizeM) *
        static_cast<double>(kFluid25DMountainSourceOutletCellSizeM);
    require_close(source_depth_rate_sum_m_per_s * kMountainCellAreaM2,
                  kFluid25DMountainSourceOutletEndpointTotalVolumeRateM3PerS, 0.00001,
                  "mountain source region should normalize to its configured physical capacity");
    require_close(sink_depth_rate_sum_m_per_s * kMountainCellAreaM2,
                  kFluid25DMountainSourceOutletEndpointTotalVolumeRateM3PerS, 0.00001,
                  "mountain outlet region should normalize to its configured physical capacity");
    require(std::all_of(mountain.boundary_outflow_face_mask.begin(),
                        mountain.boundary_outflow_face_mask.end(),
                        [](std::uint32_t mask) { return mask == 0U; }),
            "mountain source/outlet demo should keep every outer boundary closed");

    constexpr std::array<std::array<std::uint32_t, 2U>, 15U> route_controls{
        std::array<std::uint32_t, 2U>{8U, 60U},
        {24U, 60U},
        {40U, 62U},
        {56U, 65U},
        {72U, 69U},
        {88U, 78U},
        {104U, 89U},
        {120U, 91U},
        {136U, 92U},
        {152U, 98U},
        {168U, 108U},
        {184U, 122U},
        {200U, 123U},
        {216U, 123U},
        {232U, 122U},
    };
    for (const auto& control : route_controls) {
        require(mountain.initial_water_depth_m[fluid_25d_scenario_index(
                    mountain.width, mountain.height, control[0], control[1])] >= 0.08F,
                "mountain source/outlet route controls should remain connected by nonzero water");
    }
    require_close(mountain.initial_water_depth_m[mountain.source_cell], 0.30, kDepthToleranceM,
                  "mountain source pool should retain its reviewed maximum initial depth");
    require_close(mountain.initial_water_depth_m[mountain.sink_cell], 0.50, kDepthToleranceM,
                  "mountain outlet pool should retain its reviewed maximum initial depth");

    Fluid25DScenarioData wrong_crop = mountain;
    wrong_crop.terrain_provenance->transformed_crop_sha256 = std::string(64U, '0');
    require_throws([&] { validate_fluid_25d_mountain_source_outlet_terrain_identity(wrong_crop); },
                   "mountain source/outlet demo should reject a synthetic wrong crop identity");
    Fluid25DScenarioData wrong_source = mountain;
    wrong_source.terrain_provenance->elevation_sha256 = std::string(64U, '0');
    require_throws(
        [&] { validate_fluid_25d_mountain_source_outlet_terrain_identity(wrong_source); },
        "mountain source/outlet demo should reject a synthetic wrong source identity");
}

void test_profile_diagnostic_metric_math() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(2U, 2U, Fluid25DScenario::TerrainCase);
    config.cell_size_m = 2.0F;
    config.minimum_wet_depth_m = 0.10F;
    const std::array<float, 4> depth{0.05F, 0.20F, 0.40F, 0.10F};
    const std::array<Fluid25DVelocityGpu, 4> velocity{{
        {.velocity_wet = {0.0F, 0.0F, 0.0F, 0.0F}},
        {.velocity_wet = {0.01F, 0.0F, 1.0F, 0.0F}},
        {.velocity_wet = {0.03F, 0.04F, 1.0F, 0.0F}},
        {.velocity_wet = {0.0F, 0.0F, 0.0F, 0.0F}},
    }};
    const std::array<Fluid25DLedgerGpu, 4> ledger{{
        {.source_sink_boundary_reserved_m3 = {0.25F, 0.0F, 0.0F, 0.0F}},
        {.source_sink_boundary_reserved_m3 = {0.75F, 0.25F, 0.10F, 0.0F}},
        {.source_sink_boundary_reserved_m3 = {0.0F, 0.0F, 0.15F, 0.0F}},
        {.source_sink_boundary_reserved_m3 = {0.0F, 0.0F, 0.0F, 0.0F}},
    }};
    const Fluid25DProfileDiagnostics diagnostics =
        compute_fluid_25d_profile_diagnostics(config, depth, velocity, ledger, 2.0);
    require(diagnostics.wet_cell_count == 2U && diagnostics.active_flow_cell_count == 1U,
            "profile diagnostics should classify wet and active cells from physical thresholds");
    require_close(diagnostics.total_water_volume_m3, 3.0, kDepthToleranceM,
                  "profile diagnostics should integrate stored volume by physical cell area");
    require_close(diagnostics.wet_cell_ratio, 0.5, kDepthToleranceM,
                  "profile diagnostics should compute wet ratio");
    require_close(diagnostics.wet_mean_depth_m, 0.3, kDepthToleranceM,
                  "profile diagnostics should compute wet mean depth");
    require_close(diagnostics.maximum_speed_m_per_s, 0.05, kDepthToleranceM,
                  "profile diagnostics should compute maximum wet speed");
    require_close(diagnostics.active_mean_speed_m_per_s, 0.05, kDepthToleranceM,
                  "profile diagnostics should compute active mean speed");
    require_close(diagnostics.slow_pooled_wet_fraction, 0.5, kDepthToleranceM,
                  "profile diagnostics should classify slow wet water as pooled");
    require_close(diagnostics.cumulative_source_volume_m3, 1.0, kDepthToleranceM,
                  "profile diagnostics should sum source ledger volume");
    require_close(diagnostics.cumulative_sink_volume_m3, 0.25, kDepthToleranceM,
                  "profile diagnostics should sum sink ledger volume");
    require_close(diagnostics.cumulative_boundary_outflow_volume_m3, 0.25, kDepthToleranceM,
                  "profile diagnostics should sum boundary ledger volume");
    require_close(diagnostics.conservation_residual_m3, 0.5, kDepthToleranceM,
                  "profile diagnostics should expose, not hide, conservation residual");
    require_throws(
        [&] {
            std::array<float, 4> invalid_depth = depth;
            invalid_depth[0] = -0.01F;
            static_cast<void>(compute_fluid_25d_profile_diagnostics(config, invalid_depth, velocity,
                                                                    ledger, 2.0));
        },
        "profile diagnostics should reject invalid readback depth");
}

void test_profile_frame_slot_attribution() {
    using namespace cubey::projects::fluid::fluid_25d;
    const cubey::ProjectFrame first_frame{.frame_index = 1U};
    const cubey::render::FrameSlot first_slot{.index = 0U, .count = 2U};
    require(profile_frame_index(first_frame) == 0U,
            "profile frame indexing should convert runtime frame one to profile frame zero");
    require(collected_profile_frame_index(first_frame, first_slot) == 0U,
            "first collected frame should retain profile frame zero attribution");

    const cubey::ProjectFrame delayed_frame{.frame_index = 4U};
    const cubey::render::FrameSlot delayed_slot{.index = 1U, .count = 2U};
    require(collected_profile_frame_index(delayed_frame, delayed_slot) == 1U,
            "collected GPU timing should attribute the completed slot to its delayed frame");
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

void test_windowed_pacing() {
    using namespace cubey::projects::fluid::fluid_25d;

    const auto count_steps = [](double refresh_hz, float presentation_time_scale,
                                std::uint32_t frame_count) {
        Fluid25DWindowedPacing pacing(1.0F / 60.0F, presentation_time_scale);
        std::uint64_t step_count = 0U;
        for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
            step_count += pacing.advance(1.0 / refresh_hz, false).fixed_step_count;
        }
        require(pacing.accumulator_seconds() >= 0.0 &&
                    pacing.accumulator_seconds() < 1.0 / 60.0 + 1.0e-12,
                "fluid 2.5D windowed pacing should retain only a substep remainder");
        return std::pair<std::uint64_t, std::uint64_t>{step_count,
                                                       pacing.dropped_backlog_frames()};
    };

    const auto [steps_144, drops_144] = count_steps(144.0, 1.0F, 144U);
    require(steps_144 == 60U && drops_144 == 0U,
            "fluid 2.5D windowed pacing should match one second at 144 Hz");

    const auto [steps_60, drops_60] = count_steps(60.0, 1.0F, 60U);
    require(steps_60 == 60U && drops_60 == 0U,
            "fluid 2.5D windowed pacing should match one second at 60 Hz");

    const auto [steps_30, drops_30] = count_steps(30.0, 1.0F, 30U);
    require(steps_30 == 60U && drops_30 == 0U,
            "fluid 2.5D windowed pacing should catch up two steps at 30 Hz");

    const auto [steps_4x, drops_4x] = count_steps(60.0, 4.0F, 60U);
    require(steps_4x == 240U && drops_4x == 0U,
            "fluid 2.5D windowed pacing should support 4x at 60 Hz within its cap");

    const auto [steps_8x, drops_8x] = count_steps(144.0, 8.0F, 144U);
    require(steps_8x == 480U && drops_8x == 0U,
            "fluid 2.5D windowed pacing should support 8x at 144 Hz within its cap");

    Fluid25DWindowedPacing paused(1.0F / 60.0F, 1.0F);
    require(paused.advance(1.0 / 120.0, false).fixed_step_count == 0U,
            "fluid 2.5D pacing should retain a fractional step");
    const double fractional_remainder = paused.accumulator_seconds();
    require(paused.advance(30.0, true).fixed_step_count == 0U &&
                paused.accumulator_seconds() == fractional_remainder,
            "fluid 2.5D paused pacing should not accumulate wall-time backlog");
    require(paused.advance(1.0 / 120.0, false).fixed_step_count == 1U,
            "fluid 2.5D pacing should resume from the pre-pause remainder");

    Fluid25DWindowedPacing stalled(1.0F / 60.0F, 1.0F);
    const Fluid25DWindowedPacingFrame catch_up = stalled.advance(1.0, false);
    require(catch_up.fixed_step_count == kFluid25DWindowedMaxFixedStepsPerFrame &&
                catch_up.dropped_backlog && stalled.dropped_backlog_frames() == 1U,
            "fluid 2.5D pacing should cap and report a long-frame backlog drop");
    require(stalled.accumulator_seconds() >= 0.0 &&
                stalled.accumulator_seconds() < 1.0 / 60.0 + 1.0e-12 &&
                stalled.advance(0.0, false).fixed_step_count == 0U,
            "fluid 2.5D pacing should discard excess backlog while retaining no burst");

    static_cast<void>(stalled.advance(1.0 / 120.0, false));
    stalled.reset();
    require(stalled.accumulator_seconds() == 0.0 && stalled.dropped_backlog_frames() == 0U &&
                stalled.advance(1.0 / 120.0, false).fixed_step_count == 0U,
            "fluid 2.5D pacing reset should clear backlog and drop history");
    require_throws(
        [] { static_cast<void>(Fluid25DWindowedPacing(0.0F, 1.0F)); },
        "fluid 2.5D windowed pacing should reject a nonpositive fixed delta");
    require_throws(
        [] { static_cast<void>(Fluid25DWindowedPacing(1.0F / 60.0F, 0.0F)); },
        "fluid 2.5D windowed pacing should reject a nonpositive presentation scale");
    require_throws(
        [] {
            Fluid25DWindowedPacing pacing;
            static_cast<void>(pacing.advance(std::numeric_limits<double>::infinity(), false));
        },
        "fluid 2.5D windowed pacing should reject a nonfinite wall delta");
}

void test_presentation_cue_contract() {
    using namespace cubey::projects::fluid::fluid_25d;

    require(kFluid25DPresentationCueRelaxationPerSecond > 0.0F &&
                kFluid25DPresentationCueRelaxationPerSecond < 0.01F,
            "presentation cue relaxation should remain a mild render-only stabilization");
    const float repeat_a = fluid_25d_presentation_cue_seed(37U, 19U);
    const float repeat_b = fluid_25d_presentation_cue_seed(37U, 19U);
    require(repeat_a == repeat_b && repeat_a >= 0.0F && repeat_a <= 1.0F,
            "presentation cue seed should be deterministic and normalized");

    float minimum = 1.0F;
    float maximum = 0.0F;
    float maximum_neighbor_delta = 0.0F;
    for (std::uint32_t y = 0U; y < 64U; ++y) {
        for (std::uint32_t x = 0U; x < 128U; ++x) {
            const float value = fluid_25d_presentation_cue_seed(x, y);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            if (x != 0U) {
                maximum_neighbor_delta =
                    std::max(maximum_neighbor_delta,
                             std::abs(value - fluid_25d_presentation_cue_seed(x - 1U, y)));
            }
            if (y != 0U) {
                maximum_neighbor_delta =
                    std::max(maximum_neighbor_delta,
                             std::abs(value - fluid_25d_presentation_cue_seed(x, y - 1U)));
            }
        }
    }
    require(maximum - minimum > 0.20F && maximum_neighbor_delta < 0.12F,
            "presentation cue seed should vary broadly without cell-scale white noise");

    Fluid25DPresentationCueParity parity;
    require(parity.source_is_a(), "presentation cue parity should begin on A after startup/reset");
    parity.advance();
    require(!parity.source_is_a(),
            "presentation cue parity should advance exactly once per update");
    parity.advance();
    require(parity.source_is_a(), "presentation cue parity should ping-pong deterministically");
    parity.reset();
    require(parity.source_is_a(), "presentation cue reset should restore the deterministic source");

    const Fluid25DQuiverLattice product_lattice = fluid_25d_quiver_lattice(256U, 128U);
    const Fluid25DQuiverLattice demo_lattice = fluid_25d_quiver_lattice(128U, 64U);
    const Fluid25DQuiverLattice tiny_lattice = fluid_25d_quiver_lattice(3U, 2U);
    const Fluid25DQuiverLattice degenerate_lattice = fluid_25d_quiver_lattice(0U, 1U);
    require(product_lattice.columns == 96U && product_lattice.rows == 48U &&
                demo_lattice.columns == 96U && demo_lattice.rows == 48U &&
                fluid_25d_quiver_count(256U, 128U) == 4608U &&
                fluid_25d_quiver_count(128U, 64U) == 4608U && tiny_lattice.columns == 3U &&
                tiny_lattice.rows == 2U && degenerate_lattice.columns == 1U &&
                degenerate_lattice.rows == 1U &&
                fluid_25d_quiver_anchor(0U, 0U, 1U).cell_x == 0.0F &&
                fluid_25d_quiver_anchor(0U, 0U, 1U).cell_y == 0.0F,
            "Flow Inspection should use a regular 96 by 48 field on product and demo grids while "
            "reducing tiny fixtures");
    const Fluid25DQuiverAnchor demo_first = fluid_25d_quiver_anchor(0U, 128U, 64U);
    const Fluid25DQuiverAnchor demo_second = fluid_25d_quiver_anchor(1U, 128U, 64U);
    const Fluid25DQuiverAnchor demo_next_row = fluid_25d_quiver_anchor(96U, 128U, 64U);
    const Fluid25DQuiverAnchor demo_repeat = fluid_25d_quiver_anchor(96U, 128U, 64U);
    require(demo_first.cell_x == 0.5F && demo_first.cell_y == 0.5F &&
                demo_second.cell_x > demo_first.cell_x && demo_second.cell_y == demo_first.cell_y &&
                demo_next_row.cell_x == demo_first.cell_x &&
                demo_next_row.cell_y > demo_first.cell_y &&
                demo_next_row.cell_x == demo_repeat.cell_x &&
                demo_next_row.cell_y == demo_repeat.cell_y &&
                std::abs((demo_second.cell_x - demo_first.cell_x) - (126.0F / 95.0F)) < 0.0001F,
            "quiver anchors should be deterministic, fixed, and regularly spaced near 1.3 cells on "
            "the demo");
    require(kFluid25DQuiverVertexCount == 9U && kFluid25DQuiverMinimumSpeedMPerS > 0.019828F &&
                kFluid25DQuiverSpeedUpperMPerS > 0.79F && kFluid25DQuiverSpeedUpperMPerS < 0.81F &&
                kFluid25DQuiverMinimumSilhouettePitchFraction > 0.57F &&
                kFluid25DQuiverMaximumSilhouettePitchFraction > 0.71F &&
                kFluid25DQuiverMaximumSilhouettePitchFraction < 0.73F &&
                kFluid25DQuiverNeighborhoodRadiusCells == 1U &&
                fluid_25d_quiver_smoothing_blend(2.0F, kFluid25DQuiverDirectionSmoothingSeconds) >
                    0.0F &&
                fluid_25d_quiver_smoothing_blend(2.0F, kFluid25DQuiverDirectionSmoothingSeconds) <
                    1.0F &&
                !fluid_25d_quiver_sample_is_visible(0.0F, 1.0F, 0.001F) &&
                !fluid_25d_quiver_sample_is_visible(1.0F, 0.019828F, 0.001F) &&
                fluid_25d_quiver_sample_is_visible(1.0F, 0.05F, 0.001F),
            "quiver field should use simulation-time smoothing and keep dry, still, and initial "
            "terrain flow absent");

    const std::filesystem::path shader_directory =
        std::filesystem::path(__FILE__).parent_path() / "shaders";
    const auto read_shader = [](const std::filesystem::path& path) {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error("failed to read presentation cue shader");
        }
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    };
    const std::string reset =
        read_shader(shader_directory / "fluid_25d_presentation_cue_reset.comp");
    const std::string advect =
        read_shader(shader_directory / "fluid_25d_presentation_cue_advect.comp");
    const std::string water = read_shader(shader_directory / "fluid_25d_water.frag");
    const std::string quiver_reset =
        read_shader(shader_directory / "fluid_25d_quiver_reset.comp");
    const std::string quiver_update =
        read_shader(shader_directory / "fluid_25d_quiver_update.comp");
    const std::string quiver_vertex =
        read_shader(shader_directory / "fluid_25d_quiver.vert");
    const std::string quiver_fragment =
        read_shader(shader_directory / "fluid_25d_quiver.frag");
    const std::string commands =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_commands.cpp");
    const std::string app =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_app.cpp");
    require(reset.find("cue_lattice") != std::string::npos &&
                reset.find("cue_a.values[index] = seed;") != std::string::npos &&
                reset.find("cue_b.values[index] = seed;") != std::string::npos,
            "presentation cue reset shader should restore both ping-pong fields from one seed");
    require(advect.find("sample_cue_bilinear") != std::string::npos &&
                advect.find("status.values[0].x != 0u") != std::string::npos &&
                advect.find("destination_cue.values[index] = source_cue.values[index];") !=
                    std::string::npos,
            "presentation cue advection should backtrace bilinearly and freeze on solver failure");
    require(water.find("presentation_cue") != std::string::npos &&
                water.find("flow_inspection") != std::string::npos &&
                water.find("params.animation") == std::string::npos &&
                water.find("sin(") == std::string::npos && water.find("cos(") == std::string::npos,
            "water shading should consume the persistent cue without procedural time bands");
    const std::size_t substep_loop = commands.find("for (std::uint32_t substep");
    const std::size_t cue_update =
        commands.find("record_presentation_cue_advection(", substep_loop);
    const std::size_t pause_return = commands.find("if (paused) {\n        return;");
    const std::size_t direct_batch = commands.find("void record_fluid_25d_compute_batch(");
    const std::size_t direct_compute = commands.find("void record_fluid_25d_compute(");
    const std::size_t frame_graph = commands.find("build_fluid_25d_frame_graph(");
    const std::size_t diagnostics_pass = commands.find("fluid_25d diagnostics");
    const std::size_t catchment_pass = commands.find("fluid_25d catchment");
    const std::size_t catchment_cue_a =
        commands.find(".read_storage_buffer(presentation_cue_a)", catchment_pass);
    const std::size_t catchment_cue_b =
        commands.find(".read_storage_buffer(presentation_cue_b)", catchment_pass);
    const std::size_t quiver_update_call =
        commands.find("record_quiver_update(", substep_loop);
    const std::size_t headless_quiver =
        commands.find("record_fluid_25d_flow_inspection_quiver_step(");
    const std::size_t headless_quiver_reset =
        app.find("record_fluid_25d_flow_inspection_quiver_reset(");
    const std::size_t headless_compute = app.find("record_fluid_25d_compute(");
    require(substep_loop != std::string::npos && cue_update != std::string::npos &&
                substep_loop < cue_update && pause_return != std::string::npos &&
                pause_return < substep_loop,
            "presentation cue should advance after complete fixed steps and never while paused");
    require(direct_batch != std::string::npos && direct_compute != std::string::npos &&
                frame_graph != std::string::npos && direct_batch < direct_compute &&
                commands.find("Fluid25DComputeRecordingPolicy{}", direct_batch) < direct_compute &&
                commands.find("Fluid25DPresentationCuePolicy::WindowedPresentation", frame_graph) !=
                    std::string::npos &&
                app.find("bool reset_requested_ = false;") != std::string::npos &&
                app.find("bool presentation_cue_reset_requested_ = true;") != std::string::npos,
            "direct headless compute should keep the solver-only path while frame-graph presentation owns the cue");
    require(diagnostics_pass != std::string::npos && catchment_pass != std::string::npos &&
                diagnostics_pass < catchment_pass && catchment_cue_a != std::string::npos &&
                catchment_cue_b != std::string::npos && catchment_cue_a > catchment_pass &&
                catchment_cue_b > catchment_pass,
            "the catchment pass should declare the cue buffers read by the water shader");
    require(quiver_reset.find("anchor_axis") != std::string::npos &&
                quiver_reset.find("kMaxColumns = 96u") != std::string::npos &&
                quiver_reset.find("kMaxRows = 48u") != std::string::npos &&
                quiver_reset.find("kMinimumPitchCells = 1u") != std::string::npos &&
                quiver_reset.find("sample_average_velocity") != std::string::npos &&
                quiver_reset.find("status.values[0].x != 0u") != std::string::npos &&
                quiver_reset.find("velocity_sample.z < 0.5") != std::string::npos &&
                quiver_update.find("sample_average_velocity") != std::string::npos &&
                quiver_update.find("status.values[0].x != 0u") != std::string::npos &&
                quiver_update.find("state.anchor_xy_reserved =") == std::string::npos &&
                quiver_update.find("smoothed_direction") != std::string::npos &&
                quiver_update.find("mixed_length < kDirectionEpsilon") != std::string::npos &&
                quiver_update.find("state.direction_xy_strength_opacity = vec4(0.0)") !=
                    std::string::npos,
            "quiver compute should use fixed wet-aware local samples, smooth safely, and freeze on "
            "rejected status");
    require(quiver_vertex.find("gl_InstanceIndex") != std::string::npos &&
                quiver_vertex.find("shaft_vertex") != std::string::npos &&
                quiver_vertex.find("head_vertex") != std::string::npos &&
                quiver_vertex.find("lattice_pitch") != std::string::npos &&
                quiver_vertex.find("kSpeedUpperMPerS = 0.80") != std::string::npos &&
                quiver_vertex.find("kMaximumSilhouettePitchFraction") != std::string::npos &&
                quiver_vertex.find("current_velocity.z < 0.5") != std::string::npos &&
                quiver_vertex.find("current_velocity.xy") != std::string::npos &&
                quiver_vertex.find("length(current_velocity.xy)") == std::string::npos &&
                quiver_fragment.find("fwidth(normalized_edge)") != std::string::npos &&
                quiver_fragment.find("frag_part != 0u") != std::string::npos &&
                quiver_fragment.find("color * alpha") != std::string::npos,
            "Flow Inspection should render fixed conventional antialiased arrow glyphs without a draw-time speed cutoff");
    require(quiver_update_call != std::string::npos && quiver_update_call > substep_loop &&
                headless_quiver != std::string::npos &&
                headless_quiver_reset != std::string::npos &&
                headless_compute != std::string::npos &&
                headless_quiver_reset < headless_compute &&
                app.find("record_fluid_25d_flow_inspection_quiver_step") != std::string::npos &&
                app.find("bool quiver_reset_requested_ = true;") != std::string::npos,
            "quiver should seed the initial headless field before solving, then follow completed outer steps separately");
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

void test_dye_config_and_fixed_step_schedule() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(6U, 3U, Fluid25DScenario::SourceOutletDemo);
    config.fixed_delta_seconds = 0.25F;
    config.dye_pulse_start_seconds = 0.50F;
    config.dye_pulse_duration_seconds = 0.50F;
    validate_fluid_25d_config(config);

    Fluid25DDyeSourceSchedule schedule;
    require(schedule.source_concentration(config) == 0.0F,
            "dye schedule should begin inactive before its pulse start");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 0.0F,
            "dye schedule should remain inactive strictly before its pulse start");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 1.0F,
            "dye schedule should activate exactly at its pulse start boundary");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 1.0F,
            "dye schedule should remain active within its half-open pulse interval");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 0.0F,
            "dye schedule should deactivate exactly at its pulse end boundary");
    schedule.reset();
    require(schedule.completed_steps() == 0U && schedule.source_concentration(config) == 0.0F,
            "dye schedule reset should restore its inactive zero-step state");

    const Fluid25DProjectConfig parsed =
        parse_project({"fluid_25d", "--fluid25d-scenario", "source-outlet-demo",
                       "--fluid25d-solver", "finite-volume", "--fluid25d-dye-pulse-start-seconds",
                       "0.5", "--fluid25d-dye-pulse-duration-seconds", "1.25"});
    require(parsed.simulation.dye_pulse_start_seconds == 0.5F &&
                parsed.simulation.dye_pulse_duration_seconds == 1.25F,
            "dye CLI options should bind the validated source-outlet pulse timing");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--fluid25d-scenario", "source-outlet-demo", "--fluid25d-solver",
                 "finite-volume", "--fluid25d-dye-pulse-start-seconds", "0.5"}));
        },
        "dye CLI should require pulse start and duration together");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario",
                                             "source-outlet-demo", "--fluid25d-solver",
                                             "finite-volume", "--fluid25d-dye-pulse-start-seconds",
                                             "0.5", "--fluid25d-dye-pulse-duration-seconds", "0"}));
        },
        "dye CLI should reject a nonpositive pulse duration");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "river-catchment",
                                             "--fluid25d-dye-pulse-start-seconds", "0",
                                             "--fluid25d-dye-pulse-duration-seconds", "1"}));
        },
        "dye CLI should reject timing options for non-demo scenarios");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "source-outlet-demo",
                               "--fluid25d-dye-pulse-start-seconds", "0",
                               "--fluid25d-dye-pulse-duration-seconds", "1"}));
        },
        "dye CLI should reject timing options without finite-volume");
}

[[nodiscard]] cubey::projects::fluid::fluid_25d::Fluid25DScenarioData
make_dye_test_scenario(std::uint32_t width = 6U, std::uint32_t height = 3U) {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DScenarioData scenario =
        make_fluid_25d_scenario(Fluid25DScenario::SourceOutletDemo, width, height, 1.0F);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.20F);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              0.0F);
    std::fill(scenario.sink_depth_rate_m_per_s.begin(), scenario.sink_depth_rate_m_per_s.end(),
              0.0F);
    scenario.source_cell = fluid_25d_scenario_index(width, height, 1U, height / 2U);
    scenario.sink_cell = fluid_25d_scenario_index(width, height, width - 2U, height / 2U);
    scenario.source_depth_rate_m_per_s[scenario.source_cell] = 0.20F;
    scenario.sink_depth_rate_m_per_s[scenario.sink_cell] = 0.20F;
    return scenario;
}

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
    std::fill(scenario.source_depth_rate_m_per_s.begin(),
              scenario.source_depth_rate_m_per_s.end(), 0.0F);
    std::fill(scenario.sink_depth_rate_m_per_s.begin(), scenario.sink_depth_rate_m_per_s.end(),
              0.0F);
    scenario.source_cell = fluid_25d_scenario_index(6U, 3U, 0U, 1U);
    scenario.sink_cell = kFluid25DNoCell;
    scenario.source_depth_rate_m_per_s[scenario.source_cell] = 0.20F;
    scenario.boundary_outflow_face_mask[scenario.source_cell] = kFluid25DBoundaryOutflowLeft;
    const double source_delta_m = static_cast<double>(scenario.source_depth_rate_m_per_s[
        scenario.source_cell]) * static_cast<double>(config.fixed_delta_seconds);
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
    require_close(oracle.total_tracer_amount_m3(), result.tracer.source_amount_m3 -
                                                       result.tracer.boundary_outflow_amount_m3,
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

void test_finite_volume_gpu_candidate_commit_shader_contract() {
    // Invalid GPU candidate-state injection is deliberately not a user-facing
    // scenario knob. Keep a small structural test on the shipped shaders in
    // addition to the direct GPU CFL rejection lane: candidate must zero its
    // isolated ledger delta and validate the prospective cumulative ledger;
    // commit must be the sole writer of the shared ledger/velocity state.
    const std::filesystem::path shader_directory =
        std::filesystem::path(__FILE__).parent_path() / "shaders";
    const auto read_shader = [](const std::filesystem::path& path) {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error("failed to read finite-volume transaction shader");
        }
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    };
    const std::string candidate = read_shader(shader_directory / "fluid_25d_fv_update.comp");
    const std::string commit = read_shader(shader_directory / "fluid_25d_fv_commit.comp");
    require(
        candidate.find("candidate_ledger_delta.values[index] = vec4(0.0);") != std::string::npos &&
            candidate.find("vec4 prospective_ledger = cumulative_ledger.values[index] + "
                           "ledger_delta;") != std::string::npos &&
            candidate.find("!finite_nonnegative_ledger(prospective_ledger)") != std::string::npos &&
            candidate.find("ledger.values[index] +=") == std::string::npos,
        "finite-volume candidate shader must isolate and validate ledger deltas");
    require(commit.find("if (status.flags != 0u)") != std::string::npos &&
                commit.find("next_depth.values[index] = source_depth.values[index];") !=
                    std::string::npos &&
                commit.find("next_momentum.values[index] = source_momentum.values[index];") !=
                    std::string::npos &&
                commit.find("ledger.values[index] += candidate_ledger_delta.values[index];") !=
                    std::string::npos,
            "finite-volume commit shader must copy through rejected state and publish ledger once");
}

} // namespace

int main() {
    try {
        test_config_defaults_and_parsing();
        test_deterministic_scenarios();
        test_terrain_case_ingestion();
        test_terrain_water_protocol_construction();
        test_mountain_source_outlet_field_construction();
        test_profile_frame_slot_attribution();
        test_profile_diagnostic_metric_math();
        test_dry_bed_stability();
        test_lake_at_rest();
        test_dynamic_closed_domain_conservation();
        test_boundary_outflow_contract();
        test_boundary_mask_validation_and_helper();
        test_source_rate_scale_and_schedule();
        test_windowed_pacing();
        test_presentation_cue_contract();
        test_retained_flux_inertia();
        test_river_mass_and_positivity();
        test_finite_volume_dry_bed_and_uneven_lake_at_rest();
        test_finite_volume_high_absolute_elevation_shallow_film();
        test_finite_volume_symmetric_dam_break();
        test_finite_volume_open_boundary_and_source_sink_ledgers();
        test_finite_volume_cfl_fails_closed();
        test_dye_config_and_fixed_step_schedule();
        test_dye_zero_path_and_resting_concentration();
        test_dye_advection_bounds_and_ledgers();
        test_dye_open_boundary_ledger();
        test_dye_cfl_rejection_and_reset();
        test_finite_volume_gpu_candidate_commit_shader_contract();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_25d_tests: %s\n", error.what());
        return 1;
    }
    return 0;
}
