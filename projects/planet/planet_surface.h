#pragma once

#include "planet_config.h"
#include "planet_frame.h"

#include <cubey/core/math.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cubey::projects::planet {

struct PlanetSurfaceMeshData {
    std::vector<cubey::render::VertexPositionColorNormalUv> vertices{};
    std::vector<std::uint32_t> indices{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const {
        return cubey::render::indexed_mesh_config(
            std::span<const cubey::render::VertexPositionColorNormalUv>{vertices.data(),
                                                                        vertices.size()},
            std::span<const std::uint32_t>{indices.data(), indices.size()});
    }
};

struct PlanetSurfaceDiagnostics {
    std::uint32_t face_count = 6;
    std::uint32_t patch_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t min_lod_level = 0;
    std::uint32_t max_lod_level = 0;
    std::array<std::uint32_t, 4> patches_by_lod{};
    float min_screen_error_px = 0.0F;
    float max_screen_error_px = 0.0F;
    float min_edge_length_m = 0.0F;
    float max_edge_length_m = 0.0F;
};

struct PlanetSurfaceView {
    cubey::math::DVec3 camera_world_position_m{
        0.0, 0.0, kPlanetDefaultRadiusM + kPlanetDefaultCameraAltitudeM};
    float vertical_fov_radians = 1.0471975803375244F;
    float viewport_height_px = 720.0F;
};

struct PlanetSurfaceBuildResult {
    PlanetSurfaceMeshData mesh{};
    PlanetSurfaceDiagnostics diagnostics{};
};

[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config);
[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                                PlanetSurfaceView view);
[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                                PlanetSurfaceView view,
                                                                const PlanetFrame& frame);

} // namespace cubey::projects::planet
