#pragma once

#include "../sim/fluid_25d/fluid_25d_config.h"

#include <cubey/host/common_config.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DTerrainOptions {
    std::optional<std::filesystem::path> heightfield_path{};
    std::optional<std::uint32_t> crop_x{};
    std::optional<std::uint32_t> crop_z{};
};

struct Fluid25DProjectConfig {
    host::CommonRunConfig common{};
    common::FluidGridOptions grid{};
    std::string view{};
    std::string catchment_view{};
    std::string debug_view{};
    float presentation_time_scale = kFluid25DDefaultWindowedPresentationTimeScale;
    bool gpu_oracle_validation = false;
    Fluid25DStartupOptions fluid{};
    Fluid25DTerrainOptions terrain{};
    Fluid25DConfig simulation{};
};

namespace fluid_25d_project_config_detail {

inline config::OptionSpec option(std::string path, std::string cli, std::string label,
                                 std::string help, config::ValueType type, config::Range range = {},
                                 std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Fluid 2.5D",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

} // namespace fluid_25d_project_config_detail

inline void validate_fluid_25d_project_config(const Fluid25DProjectConfig& project_config) {
    validate_fluid_25d_windowed_presentation_time_scale(
        project_config.presentation_time_scale);
    const Fluid25DCatchmentView catchment_view =
        fluid_25d_catchment_view_from_name(project_config.catchment_view);
    if (catchment_view == Fluid25DCatchmentView::TransportInspection &&
        !fluid_25d_transport_inspection_available(project_config.simulation)) {
        throw std::runtime_error(
            "fluid 2.5D transport-inspection requires source-outlet-demo with finite-volume "
            "and a positive dye pulse duration");
    }
    const bool terrain_case = project_config.simulation.scenario == Fluid25DScenario::TerrainCase;
    const bool mountain_source_outlet =
        project_config.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo;
    const bool terrain_backed = terrain_case || mountain_source_outlet;
    const bool has_heightfield = project_config.terrain.heightfield_path.has_value();
    const bool has_nonempty_heightfield =
        has_heightfield && !project_config.terrain.heightfield_path->empty();
    if (terrain_backed && !has_nonempty_heightfield) {
        throw std::runtime_error(
            "fluid 2.5D terrain-backed scenarios require a non-empty --terrain-heightfield path");
    }
    if (!terrain_backed && has_heightfield) {
        throw std::runtime_error(
            "fluid 2.5D --terrain-heightfield requires a terrain-backed scenario");
    }
    if (!terrain_case &&
        (project_config.terrain.crop_x.has_value() || project_config.terrain.crop_z.has_value())) {
        throw std::runtime_error(
            "fluid 2.5D terrain crop options require --fluid25d-scenario terrain-case");
    }

    const bool has_explicit_terrain_water_option =
        project_config.fluid.terrain_water_protocol.has_value() ||
        project_config.fluid.rainfall_rate_mm_per_hour.has_value() ||
        project_config.fluid.sheet_depth_m.has_value();
    const bool has_explicit_dye_option =
        project_config.fluid.dye_pulse_start_seconds.has_value() ||
        project_config.fluid.dye_pulse_duration_seconds.has_value();
    if (has_explicit_dye_option &&
        (project_config.simulation.scenario != Fluid25DScenario::SourceOutletDemo ||
         project_config.simulation.solver != Fluid25DSolver::FiniteVolume)) {
        throw std::runtime_error(
            "fluid 2.5D dye pulse timing requires source-outlet-demo with finite-volume");
    }
    if (!terrain_case && has_explicit_terrain_water_option) {
        throw std::runtime_error(
            "fluid 2.5D terrain-water protocol options require --fluid25d-scenario terrain-case");
    }
    if (mountain_source_outlet && project_config.fluid.source_active_duration_seconds.has_value()) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo rejects terrain-water forcing options");
    }
    if (!terrain_case) {
        return;
    }

    const Fluid25DTerrainWaterProtocol protocol = project_config.simulation.terrain_water_protocol;
    const bool has_rainfall_rate = project_config.fluid.rainfall_rate_mm_per_hour.has_value();
    const bool has_sheet_depth = project_config.fluid.sheet_depth_m.has_value();
    const bool has_source_duration =
        project_config.fluid.source_active_duration_seconds.has_value();
    switch (protocol) {
    case Fluid25DTerrainWaterProtocol::None:
        if (has_rainfall_rate || has_sheet_depth || has_source_duration) {
            throw std::runtime_error(
                "fluid 2.5D terrain-case protocol none rejects forcing parameters");
        }
        break;
    case Fluid25DTerrainWaterProtocol::RainPulse:
        if (!has_rainfall_rate || !(project_config.simulation.rainfall_depth_rate_m_per_s > 0.0F) ||
            has_sheet_depth || !has_source_duration ||
            !(*project_config.simulation.source_active_duration_seconds > 0.0F)) {
            throw std::runtime_error(
                "fluid 2.5D rain-pulse requires positive rainfall and source-active duration, and "
                "rejects sheet depth");
        }
        break;
    case Fluid25DTerrainWaterProtocol::SheetRelease:
        if (!has_sheet_depth || !(project_config.simulation.sheet_initial_depth_m > 0.0F) ||
            has_rainfall_rate || has_source_duration) {
            throw std::runtime_error(
                "fluid 2.5D sheet-release requires positive sheet depth and rejects rain and "
                "source-active duration");
        }
        break;
    }
}

inline void resolve_fluid_25d_terrain_cell_size(Fluid25DProjectConfig& project_config,
                                                float source_spacing_m) {
    if (!std::isfinite(source_spacing_m) || source_spacing_m <= 0.0F) {
        throw std::runtime_error("fluid 2.5D terrain source has invalid sample spacing");
    }
    if (project_config.fluid.cell_size_m.has_value()) {
        const float requested_spacing_m = *project_config.fluid.cell_size_m;
        const float scale = std::max({1.0F, std::abs(requested_spacing_m), source_spacing_m});
        if (!std::isfinite(requested_spacing_m) || requested_spacing_m <= 0.0F ||
            std::abs(requested_spacing_m - source_spacing_m) > 1.0e-6F * scale) {
            throw std::runtime_error("fluid 2.5D terrain-backed scenario rejects "
                                     "--fluid25d-cell-size-m when it conflicts "
                                     "with the source sample spacing");
        }
    }
    // Always write the exact source value after the conflict check. This keeps
    // the imported scenario and every downstream GPU/CPU consumer canonical,
    // even when the explicit CLI value only differs by float roundoff.
    project_config.simulation.cell_size_m = source_spacing_m;
    validate_fluid_25d_config(project_config.simulation);
}

[[nodiscard]] inline config::Schema fluid_25d_project_config_schema(Fluid25DProjectConfig& config) {
    using config::ValueType;
    using fluid_25d_project_config_detail::option;

    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(common::fluid_grid_schema(config.grid, common::FluidGridSchemaMode::TwoD));
    builder
        .bind(option("fluid25d.view", "--fluid25d-view", "View",
                     "Top-level River V0 surface: catchment or diagnostics.", ValueType::Enum, {},
                     {"catchment", "diagnostics"}),
              config.view)
        .bind(option("fluid25d.catchment_view", "--fluid25d-catchment-view", "Catchment View",
                     "Catchment presentation: composite, water-isolation, flow-inspection, or "
                     "transport-inspection (dye source/outlet demo only).",
                     ValueType::Enum, {}, {"composite", "water-isolation", "flow-inspection",
                                            "transport-inspection"}),
              config.catchment_view)
        .bind(option("fluid25d.presentation_time_scale", "--fluid25d-presentation-time-scale",
                     "Presentation Time Scale",
                     "Windowed-only simulation playback speed; 1, 4, and 8 are review-friendly.",
                     ValueType::Float,
                     {.has_min = true,
                      .has_max = true,
                      .min = static_cast<double>(kFluid25DMinWindowedPresentationTimeScale),
                      .max = static_cast<double>(kFluid25DMaxWindowedPresentationTimeScale)}),
              config.presentation_time_scale)
        .bind(option(
                  "fluid25d.debug_view", "--debug-view", "Debug View",
                  "Top-down diagnostic view: terrain, depth, surface, flow, direction, or wet-dry.",
                  ValueType::Enum, {},
                  {"terrain", "depth", "surface", "flow", "direction", "wet-dry"}),
              config.debug_view)
        .bind(
            option("fluid25d.gpu_oracle_validation", "--fluid25d-gpu-oracle-validation",
                   "GPU Oracle Validation",
                   "Headless-only opt-in CPU/GPU solver comparison with simulation-state readback.",
                   ValueType::Bool),
            config.gpu_oracle_validation)
        .bind(option("terrain.heightfield", "--terrain-heightfield", "Heightfield",
                     "Terrain heightfield manifest or directory used by a terrain-backed scenario.",
                     ValueType::Path),
              config.terrain.heightfield_path)
        .bind(option("fluid25d.terrain_crop_x", "--fluid25d-terrain-crop-x", "Terrain Crop X",
                     "Native x sample index of the terrain-case crop origin.", ValueType::UInt32),
              config.terrain.crop_x)
        .bind(option("fluid25d.terrain_crop_z", "--fluid25d-terrain-crop-z", "Terrain Crop Z",
                     "Native z sample index of the terrain-case crop origin.", ValueType::UInt32),
              config.terrain.crop_z)
        .bind(option("fluid25d.terrain_water_protocol", "--fluid25d-terrain-water-protocol",
                     "Terrain-Water Protocol",
                     "Terrain-case-only neutral forcing: none, rain-pulse, or sheet-release.",
                     ValueType::Enum, {}, {"none", "rain-pulse", "sheet-release"}),
              config.fluid.terrain_water_protocol)
        .bind(option("fluid25d.rainfall_rate_mm_per_hour", "--fluid25d-rainfall-rate-mm-per-hour",
                     "Rainfall Rate", "Terrain-case rain-pulse depth rate in millimetres per hour.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.rainfall_rate_mm_per_hour)
        .bind(option("fluid25d.sheet_depth_m", "--fluid25d-sheet-depth-m", "Sheet Depth",
                     "Terrain-case sheet-release initial water depth in metres.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.fluid.sheet_depth_m)
        .bind(option("fluid25d.scenario", "--fluid25d-scenario", "Scenario",
                     "Deterministic River V0 fixture, opt-in source/outlet demo, or terrain-backed "
                     "scenario.",
                     ValueType::Enum, {},
                     {"dry-bed", "lake-at-rest", "river-catchment", "source-outlet-demo",
                      "mountain-source-outlet-demo", "terrain-case", "boundary-drain-fixture"}),
              config.fluid.scenario)
        .bind(
            option("fluid25d.solver", "--fluid25d-solver", "Solver",
                   "Opt-in numerical solver: virtual-pipes (default) or finite-volume comparison.",
                   ValueType::Enum, {}, {"virtual-pipes", "finite-volume"}),
            config.fluid.solver)
        .bind(option("fluid25d.cell_size_m", "--fluid25d-cell-size-m", "Cell Size",
                     "Simulation cell width in metres.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.cell_size_m)
        .bind(option("fluid25d.fixed_delta_seconds", "--fluid25d-fixed-delta-seconds",
                     "Fixed Delta", "Fixed simulation frame duration in seconds.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.fixed_delta_seconds)
        .bind(option("fluid25d.simulation_substeps", "--fluid25d-substeps", "Substeps",
                     "Fixed solver substeps per simulation frame.", ValueType::UInt32,
                     {.has_min = true,
                      .has_max = true,
                      .min = 1.0,
                      .max = static_cast<double>(kMaxFluid25DSubsteps)}),
              config.fluid.simulation_substeps)
        .bind(option("fluid25d.gravity_m_per_s2", "--fluid25d-gravity-m-per-s2", "Gravity",
                     "Gravitational acceleration in metres per second squared.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.gravity_m_per_s2)
        .bind(option("fluid25d.flow_damping_per_second", "--fluid25d-flow-damping-per-second",
                     "Flow Damping", "Flow damping rate in inverse seconds.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.fluid.flow_damping_per_second)
        .bind(option("fluid25d.minimum_wet_depth_m", "--fluid25d-minimum-wet-depth-m",
                     "Minimum Wet Depth",
                     "Numerical wet/dry depth threshold used by flux evolution, in metres.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.minimum_wet_depth_m)
        .bind(option("fluid25d.source_active_duration_seconds",
                     "--fluid25d-source-active-duration-seconds", "Source Active Duration",
                     "Optional fixed-simulation duration before source rates switch to zero.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.source_active_duration_seconds)
        .bind(option("fluid25d.dye_pulse_start_seconds", "--fluid25d-dye-pulse-start-seconds",
                     "Dye Pulse Start",
                     "Opt-in source-outlet dye pulse start time in fixed-simulation seconds.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.dye_pulse_start_seconds)
        .bind(option("fluid25d.dye_pulse_duration_seconds", "--fluid25d-dye-pulse-duration-seconds",
                     "Dye Pulse Duration",
                     "Opt-in source-outlet dye pulse duration in fixed-simulation seconds.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.dye_pulse_duration_seconds);
    return std::move(builder).build();
}

[[nodiscard]] inline Fluid25DProjectConfig
parse_fluid_25d_project_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    Fluid25DProjectConfig project_config;
    config::Schema schema = fluid_25d_project_config_schema(project_config);
    config::ParseResult parsed = schema.parse_cli(argc, argv);
    host::normalize_common_run_config(project_config.common,
                                      parsed.path_was_assigned("output") ||
                                          project_config.common.output_path != "cubey-output.png");
    project_config.simulation =
        fluid_25d_config_from_options(project_config.grid, project_config.fluid);
    validate_fluid_25d_project_config(project_config);
    if (project_config.common.headless &&
        parsed.path_was_assigned("fluid25d.presentation_time_scale")) {
        throw std::runtime_error(
            "fluid 2.5D presentation time scale is windowed-only; omit it in headless mode");
    }
    static_cast<void>(fluid_25d_presentation_view_from_name(project_config.view));
    static_cast<void>(fluid_25d_catchment_view_from_name(project_config.catchment_view));
    if (!project_config.catchment_view.empty() && project_config.view == "diagnostics") {
        throw std::runtime_error(
            "fluid 2.5D --fluid25d-catchment-view cannot be combined with --fluid25d-view diagnostics");
    }
    static_cast<void>(fluid_25d_debug_view_from_name(project_config.debug_view));
    if (project_config.gpu_oracle_validation && !project_config.common.headless) {
        throw std::runtime_error(
            "fluid 2.5D GPU oracle validation is headless-only; pass --headless");
    }
    if (project_config.common.profile_diagnostics && !project_config.common.headless) {
        throw std::runtime_error(
            "fluid 2.5D profile diagnostics are headless-only; pass --headless");
    }
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = std::move(parsed);
    }
    return project_config;
}

} // namespace cubey::projects::fluid::fluid_25d
