#pragma once

#include <cubey/core/config_schema.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace cubey::projects::fluid::common {

// Grid dimensions are shared by every fluid product.  Keep the legacy
// --grid-size fan-out here so the six project facades cannot drift in their
// handling of that alias.  Optional storage is intentional: zero is not an
// unset value in the V2 schema.
struct FluidGridOptions {
    std::optional<std::uint32_t> width{};
    std::optional<std::uint32_t> height{};
    std::optional<std::uint32_t> depth{};
    std::optional<std::uint32_t> size{};
};

enum class FluidGridSchemaMode {
    TwoD,
    ThreeD,
};

namespace fluid_config_schema_detail {

inline cubey::config::OptionSpec option(std::string path, std::string cli, std::string label,
                                        std::string help,
                                        cubey::config::ValueType type,
                                        cubey::config::Range range = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Grid",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = {}};
}

} // namespace fluid_config_schema_detail

[[nodiscard]] inline cubey::config::Schema
fluid_grid_schema(FluidGridOptions& options, FluidGridSchemaMode mode) {
    using cubey::config::ValueType;
    using fluid_config_schema_detail::option;

    auto builder = cubey::config::Schema::builder();
    constexpr cubey::config::Range positive{.has_min = true, .min = 1.0};
    builder.bind(option("grid.width", "--grid-width", "Width",
                        "Project grid width. Null leaves the project default in place.",
                        ValueType::UInt32, positive),
                 options.width);
    builder.bind(option("grid.height", "--grid-height", "Height",
                        "Project grid height. Null leaves the project default in place.",
                        ValueType::UInt32, positive),
                 options.height);

    cubey::config::OptionSpec size =
        option("grid.size", "--grid-size", "Size",
               "Set project grid width and height to the same value.", ValueType::UInt32,
               positive);
    builder.bind_custom(
        std::move(size),
        [&options](std::string_view value) {
            // Use a temporary schema binding for the exact generic integer/range parser.
            auto parser = cubey::config::Schema::builder();
            parser.bind(option("grid.size", "--grid-size", "Size", "Grid size",
                               ValueType::UInt32,
                               {.has_min = true, .min = 1.0}),
                        options.size);
            cubey::config::Schema schema = std::move(parser).build();
            schema.set("grid.size", value);
            options.width = options.size;
            options.height = options.size;
        },
        [&options](const nlohmann::json& value) {
            if (value.is_null()) {
                return;
            }
            auto parser = cubey::config::Schema::builder();
            parser.bind(option("grid.size", "--grid-size", "Size", "Grid size",
                               ValueType::UInt32,
                               {.has_min = true, .min = 1.0}),
                        options.size);
            cubey::config::Schema schema = std::move(parser).build();
            schema.set("grid.size", value);
            options.width = options.size;
            options.height = options.size;
        },
        [&options] {
            return options.size.has_value() ? nlohmann::json(*options.size)
                                             : nlohmann::json(nullptr);
        });

    if (mode == FluidGridSchemaMode::ThreeD) {
        builder.bind(option("grid.depth", "--grid-depth", "Depth",
                            "Project grid depth. Null leaves the project default in place.",
                            ValueType::UInt32, positive),
                     options.depth);
    }
    return std::move(builder).build();
}

} // namespace cubey::projects::fluid::common
