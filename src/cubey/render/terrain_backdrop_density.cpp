#include <cubey/render/terrain_backdrop_density.h>

#include <stdexcept>
#include <string>

namespace cubey::render {

std::string_view terrain_backdrop_mesh_density_name(TerrainBackdropMeshDensity density) noexcept {
    switch (density) {
    case TerrainBackdropMeshDensity::Low:
        return "low";
    case TerrainBackdropMeshDensity::Medium:
        return "medium";
    case TerrainBackdropMeshDensity::High:
        return "high";
    }
    return "high";
}

TerrainBackdropMeshDensity terrain_backdrop_mesh_density_from_name(std::string_view name) {
    if (name == "low") {
        return TerrainBackdropMeshDensity::Low;
    }
    if (name == "medium") {
        return TerrainBackdropMeshDensity::Medium;
    }
    if (name.empty() || name == "high") {
        return TerrainBackdropMeshDensity::High;
    }
    throw std::runtime_error("unknown terrain backdrop mesh density: " + std::string(name));
}

} // namespace cubey::render
