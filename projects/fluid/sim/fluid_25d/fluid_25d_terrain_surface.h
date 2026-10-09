#pragma once

#include <cubey/core/math.h>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

// Render-only, source-coordinate weights: rock, cover potential, climate
// moisture potential, snow potential. Neither moisture nor snow is live state.
struct Fluid25DTerrainSurface {
    std::uint32_t width = 0U, height = 0U;
    std::vector<math::Vec4> landform, climate;
    std::vector<math::Vec4> correlated;
    std::string receipt;
};

// Reuses Terrain's climate classifier; no noise is added to imported climate.
[[nodiscard]] Fluid25DTerrainSurface
fluid_25d_build_terrain_surface(std::uint32_t width, std::uint32_t height, float spacing_m,
                                std::span<const float> bed,
                                std::span<const math::Vec4> climate_samples);

// A completed-recording study only. Checks source manifest, crop orientation,
// transformed source bed and climate-to-elevation binding before any bake.
[[nodiscard]] Fluid25DTerrainSurface
fluid_25d_load_terrain_surface(const std::filesystem::path& heightfield,
                               const nlohmann::json& source_identity, std::uint32_t width,
                               std::uint32_t height, float spacing_m, std::span<const float> bed,
                               std::span<const float> source_bed);

[[nodiscard]] unsigned fluid_25d_terrain_surface_mode(std::string_view mode);

} // namespace cubey::projects::fluid::fluid_25d
