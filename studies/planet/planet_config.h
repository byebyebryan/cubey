#pragma once

#include <cubey/host/common_config.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cubey::projects::planet {

enum class PlanetCameraMode {
    Orbit,
    Surface,
};

enum class PlanetOrbitalView {
    Lit,
    Terminator,
    Crescent,
    Night,
};

enum class PlanetSurfaceQuality {
    Draft,
    Standard,
};

enum class PlanetDebugView {
    Final,
    Land,
    Elevation,
    Ice,
    Roughness,
    Albedo,
};

// The generic enum binding maps choices by ordinal; these declarations must
// remain zero-based and in the same order as planet_config_schema().
struct PlanetLiveOptions {
    std::optional<PlanetCameraMode> camera_mode;
    std::optional<PlanetOrbitalView> orbital_view;
    std::optional<float> disk_coverage;
    std::optional<PlanetSurfaceQuality> surface_quality;
    std::optional<std::uint32_t> terrain_seed;
};

struct PlanetConfig {
    cubey::host::CommonRunConfig common;
    std::string debug_view;
    PlanetLiveOptions planet;
};

[[nodiscard]] cubey::config::Schema planet_config_schema(PlanetConfig& config);
[[nodiscard]] PlanetConfig parse_planet_config(int argc, char** argv,
                                               cubey::config::ParseResult* result = nullptr);
void validate_planet_config(const PlanetConfig& config);
[[nodiscard]] PlanetDebugView resolve_planet_debug_view(std::string_view value);

} // namespace cubey::projects::planet
