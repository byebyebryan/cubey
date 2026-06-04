#pragma once

#include "planet_config.h"

#include <cubey/render/primitive_mesh.h>

#include <cstdint>

namespace cubey::projects::planet {

using PlanetSurfaceMeshData =
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv>;

struct PlanetSurfaceDiagnostics {
    std::uint32_t face_count = 6;
    std::uint32_t patch_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    float min_edge_length_m = 0.0F;
    float max_edge_length_m = 0.0F;
};

struct PlanetSurfaceBuildResult {
    PlanetSurfaceMeshData mesh{};
    PlanetSurfaceDiagnostics diagnostics{};
};

[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config);

} // namespace cubey::projects::planet
