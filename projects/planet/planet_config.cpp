#include "planet_config.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::projects::planet {
namespace {

cubey::config::OptionSpec option(std::string path, std::string cli_name, std::string label,
                                 std::string group_path, std::string help,
                                 cubey::config::ValueType type, cubey::config::Range range = {},
                                 std::vector<std::string> enum_values = {}) {
    return {
        .path = std::move(path),
        .cli_name = std::move(cli_name),
        .negative_cli_name = {},
        .label = std::move(label),
        .group_path = std::move(group_path),
        .help = std::move(help),
        .type = type,
        .range = range,
        .enum_values = std::move(enum_values),
    };
}

} // namespace

cubey::config::Schema planet_config_schema(PlanetConfig& config) {
    cubey::config::Schema::Builder builder = cubey::config::Schema::builder().compose(
        cubey::host::common_run_config_schema(config.common));
    builder
        .bind(option("planet.camera_mode", "--planet-camera-mode", "Camera Mode", "Planet",
                     "Initial planet camera mode.", cubey::config::ValueType::Enum, {},
                     {"orbit", "surface"}),
              config.planet.camera_mode)
        .bind(option("planet.orbital_view", "--planet-view", "Orbital View", "Planet",
                     "Orbital lighting preset: lit, terminator, crescent, or night.",
                     cubey::config::ValueType::Enum, {},
                     {"lit", "terminator", "crescent", "night"}),
              config.planet.orbital_view)
        .bind(option("planet.disk_coverage", "--planet-disk-coverage", "Disk Coverage", "Planet",
                     "Target planet-disk height as a viewport fraction.",
                     cubey::config::ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.15, .max = 0.70}),
              config.planet.disk_coverage)
        .bind(option("planet.surface_quality", "--planet-surface-quality", "Surface Quality",
                     "Planet", "Orbital source resolution: draft or standard.",
                     cubey::config::ValueType::Enum, {}, {"draft", "standard"}),
              config.planet.surface_quality)
        .bind(option("planet.terrain_seed", "--planet-terrain-seed", "Terrain Seed", "Planet",
                     "Project-local planet terrain seed.", cubey::config::ValueType::UInt32),
              config.planet.terrain_seed);
    return std::move(builder).build();
}

void validate_planet_config(const PlanetConfig& config) {
    if (config.planet.camera_mode.has_value() &&
        config.planet.camera_mode.value() != PlanetCameraMode::Orbit) {
        throw std::invalid_argument("planet orbital V1 only supports --planet-camera-mode orbit");
    }
}

PlanetDebugView resolve_planet_debug_view(std::string_view value) {
    if (value == "land") {
        return PlanetDebugView::Land;
    }
    if (value == "elevation") {
        return PlanetDebugView::Elevation;
    }
    if (value == "ice") {
        return PlanetDebugView::Ice;
    }
    if (value == "roughness") {
        return PlanetDebugView::Roughness;
    }
    if (value == "albedo") {
        return PlanetDebugView::Albedo;
    }
    return PlanetDebugView::Final;
}

PlanetConfig parse_planet_config(int argc, char** argv, cubey::config::ParseResult* result) {
    PlanetConfig config;
    cubey::config::Schema schema = planet_config_schema(config);
    cubey::config::ParseResult parsed = schema.parse_cli(argc, argv);
    cubey::host::normalize_common_run_config(config.common,
                                             parsed.path_was_assigned("output") ||
                                                 config.common.output_path != "cubey-output.png");
    validate_planet_config(config);
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = std::move(parsed);
    }
    return config;
}

} // namespace cubey::projects::planet
