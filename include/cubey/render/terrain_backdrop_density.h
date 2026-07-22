#pragma once

#include <cstdint>
#include <string_view>

namespace cubey::render {

enum class TerrainBackdropMeshDensity : std::uint8_t {
    Low,
    Medium,
    High,
};

[[nodiscard]] std::string_view
terrain_backdrop_mesh_density_name(TerrainBackdropMeshDensity density) noexcept;
[[nodiscard]] TerrainBackdropMeshDensity
terrain_backdrop_mesh_density_from_name(std::string_view name);

} // namespace cubey::render
