#include "planet_config.h"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace cubey::projects::planet::tests {
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

PlanetConfig parse(std::initializer_list<std::string> arguments) {
    std::vector<std::string> values;
    values.emplace_back("planet");
    values.insert(values.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(values.size());
    for (std::string& value : values) {
        argv.push_back(value.data());
    }
    return parse_planet_config(static_cast<int>(argv.size()), argv.data());
}

} // namespace

void parses_live_options_and_preserves_typed_unset_state() {
    const PlanetConfig config =
        parse({"--headless", "--planet-camera-mode", "orbit", "--planet-view", "crescent",
               "--planet-disk-coverage", "0.48", "--planet-surface-quality", "draft",
               "--planet-terrain-seed", "9012"});
    require(config.common.headless, "planet parser should preserve common host options");
    require(config.planet.camera_mode == PlanetCameraMode::Orbit,
            "planet camera mode should parse into its typed enum");
    require(config.planet.orbital_view == PlanetOrbitalView::Crescent,
            "planet orbital view should parse into its typed enum");
    require(config.planet.disk_coverage == 0.48F, "planet coverage should parse");
    require(config.planet.surface_quality == PlanetSurfaceQuality::Draft,
            "planet quality should parse into its typed enum");
    require(config.planet.terrain_seed == 9012U, "planet seed should parse");
    const PlanetConfig defaults = parse({});
    require(!defaults.planet.disk_coverage.has_value(),
            "unset planet coverage should remain an optional unset value");
    require(!defaults.planet.terrain_seed.has_value(),
            "unset planet seed should remain an optional unset value");
}

void rejects_unrelated_paths_and_flags() {
    require_throws([] { static_cast<void>(parse({"--planet-atmosphere-mode", "analytic"})); },
                   "planet should reject legacy planet flags");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey-planet-config-reject.json";
    {
        std::ofstream output(path);
        output << R"({"planet":{"terrain_enabled":true}})";
    }
    require_throws([&] { static_cast<void>(parse({"--config", path.string()})); },
                   "planet should reject unrelated JSON paths");
    std::filesystem::remove(path);
}

void parses_debug_view_and_resolves_runtime_selection() {
    const PlanetConfig cli_config = parse({"--debug-view", "land"});
    require(cli_config.debug_view == "land",
            "planet should preserve its project-owned debug-view CLI value");
    require(resolve_planet_debug_view(cli_config.debug_view) == PlanetDebugView::Land,
            "planet should resolve the land debug view");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey-planet-debug-view.json";
    {
        std::ofstream output(path);
        output << R"({"debug_view":"roughness"})";
    }
    const PlanetConfig json_config = parse({"--config", path.string()});
    require(resolve_planet_debug_view(json_config.debug_view) == PlanetDebugView::Roughness,
            "planet should resolve the JSON debug view");
    std::filesystem::remove(path);
}

void template_contains_only_common_profile_and_live_planet_scope() {
    PlanetConfig config;
    const cubey::config::Schema schema = planet_config_schema(config);
    for (const cubey::config::OptionSpec& option : schema.options()) {
        require(!option.label.empty() && !option.group_path.empty() && !option.help.empty(),
                "planet schema options should expose UI metadata");
    }
    const nlohmann::json document = schema.template_json();
    require(document.contains("width") && document.contains("profile"),
            "planet template should include common and profile options");
    require(document.contains("debug_view"),
            "planet template should keep the project-owned debug-view path");
    require(document.at("planet").size() == 5U,
            "planet template should expose exactly five live project options");
    require(document.at("planet").contains("camera_mode"),
            "planet template should include camera mode");
    require(!document.at("planet").contains("terrain_enabled"),
            "planet template should exclude legacy terrain controls");
    require(!document.contains("grid") && !document.contains("clouds") &&
                !document.contains("atmosphere"),
            "planet template should exclude unrelated project groups");
}

void layered_sources_preserve_precedence() {
    const std::filesystem::path first =
        std::filesystem::temp_directory_path() / "cubey-planet-config-first.json";
    const std::filesystem::path second =
        std::filesystem::temp_directory_path() / "cubey-planet-config-second.json";
    {
        std::ofstream output(first);
        output << R"({"planet":{"orbital_view":"lit","terrain_seed":1}})";
    }
    {
        std::ofstream output(second);
        output << R"({"planet":{"orbital_view":"terminator","terrain_seed":2}})";
    }
    const PlanetConfig config =
        parse({"--config", first.string(), "--config", second.string(), "--planet-view", "crescent",
               "--set", "planet.orbital_view=night", "--set", "planet.terrain_seed=7"});
    require(config.planet.orbital_view == PlanetOrbitalView::Night,
            "deferred --set should override named CLI and config files");
    require(config.planet.terrain_seed == 7U,
            "deferred --set should override layered config values");
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

} // namespace cubey::projects::planet::tests
