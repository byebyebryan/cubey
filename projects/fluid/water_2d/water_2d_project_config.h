#pragma once

#include "../sim/water_2d/water_2d_config.h"

#include <cubey/host/common_config.h>

#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::water_2d {

struct Water2DProjectConfig {
    host::CommonRunConfig common{};
    common::FluidGridOptions grid{};
    std::string debug_view{};
    Water2DStartupOptions water{};
    Water2DConfig simulation{};
};

namespace water_2d_project_config_detail {

inline config::OptionSpec option(std::string path, std::string cli, std::string label,
                                 std::string help, config::ValueType type,
                                 config::Range range = {},
                                 std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Water 2D",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

} // namespace water_2d_project_config_detail

[[nodiscard]] inline config::Schema water_2d_project_config_schema(
    Water2DProjectConfig& config) {
    using config::ValueType;
    using water_2d_project_config_detail::option;

    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(common::fluid_grid_schema(config.grid, common::FluidGridSchemaMode::TwoD));

    config::OptionSpec debug_view{
        .path = "debug_view",
        .cli_name = "--debug-view",
        .negative_cli_name = {},
        .label = "Debug View",
        .group_path = "Debug",
        .help = "Water 2D debug view: surface, particles, cells, velocity, divergence, pressure, solid, or foam.",
        .type = ValueType::String,
        .range = {},
        .enum_values = {}};
    builder
        .bind(std::move(debug_view), config.debug_view)
        .bind(option("water2d.transfer", "--water2d-transfer", "Transfer",
                     "Particle-grid transfer mode.", ValueType::Enum, {},
                     {"apic", "pic-flip", "picflip", "pic/flip"}),
              config.water.transfer_mode)
        .bind(option("water2d.transfer_limit", "--water2d-transfer-limit", "Transfer Limit",
                     "Particle samples consumed per grid cell.", ValueType::UInt32,
                     {.has_min = true, .min = 1.0}),
              config.water.transfer_limit);

    config::OptionSpec hose = option("water2d.hose", "--water2d-hose", "Hose",
                                     "Enable hose injection.", ValueType::Bool);
    hose.negative_cli_name = "--no-water2d-hose";
    builder.bind(std::move(hose), config.water.hose);
    config::OptionSpec drain = option("water2d.drain", "--water2d-drain", "Drain",
                                      "Enable draining.", ValueType::Bool);
    drain.negative_cli_name = "--no-water2d-drain";
    builder.bind(std::move(drain), config.water.drain);
    config::OptionSpec wave = option("water2d.wave", "--water2d-wave", "Wave",
                                    "Enable wave forcing.", ValueType::Bool);
    wave.negative_cli_name = "--no-water2d-wave";
    builder.bind(std::move(wave), config.water.wave);
    return std::move(builder).build();
}

[[nodiscard]] inline Water2DProjectConfig parse_water_2d_project_config(
    int argc, char** argv, config::ParseResult* result = nullptr) {
    Water2DProjectConfig config;
    config::Schema schema = water_2d_project_config_schema(config);
    config::ParseResult parsed = schema.parse_cli(argc, argv);
    host::normalize_common_run_config(
        config.common,
        parsed.path_was_assigned("output") || config.common.output_path != "cubey-output.png");
    config.simulation = water_2d_config_from_options(config.grid, config.water, config.common);
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = std::move(parsed);
    }
    return config;
}

} // namespace cubey::projects::fluid::water_2d
