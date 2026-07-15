#pragma once

#include "terrain_backdrop_density.h"
#include "terrain_source.h"

#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainBackdropDensityProfile {
    std::uint32_t angular_intervals = 0U;
    std::uint32_t hidden_radial_intervals = 0U;
    std::uint32_t visible_radial_intervals = 0U;
    std::uint32_t sector_count = 0U;
};

[[nodiscard]] TerrainBackdropDensityProfile
terrain_backdrop_density_profile(TerrainBackdropMeshDensity density) noexcept;

struct TerrainBackdropProductRequest {
    TerrainSourceParameters source{};
    cubey::math::Vec2 source_focus_xz{0.0F, 0.0F};
    TerrainBackdropMeshDensity density = TerrainBackdropMeshDensity::High;
    float consumer_radius_m = 300.0F;
    float visible_inner_radius_m = 3'200.0F;
    float outer_radius_m = 16'384.0F;
    float vertical_scale = 1.0F;
    float vertical_offset_m = 0.0F;
};

struct TerrainBackdropSectorBounds {
    cubey::math::Vec3 minimum{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 maximum{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 center{0.0F, 0.0F, 0.0F};
    float radius_m = 0.0F;
};

struct TerrainBackdropSectorMesh {
    std::vector<cubey::render::VertexPositionColorNormal> vertices{};
    std::vector<std::uint32_t> indices{};
    std::vector<std::uint32_t> render_indices{};
    TerrainBackdropSectorBounds bounds{};
    float begin_azimuth_radians = 0.0F;
    float end_azimuth_radians = 0.0F;

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
    [[nodiscard]] std::uint32_t triangle_count() const noexcept;
};

struct TerrainBackdropProductDiagnostics {
    TerrainBackdropDensityProfile density{};
    std::uint64_t source_sample_count = 0U;
    std::uint64_t visible_vertex_count = 0U;
    std::uint64_t visible_index_count = 0U;
    std::uint64_t visible_triangle_count = 0U;
    std::uint64_t render_triangle_count = 0U;
    float minimum_height_m = 0.0F;
    float maximum_height_m = 0.0F;
    float maximum_sector_boundary_delta_m = 0.0F;
    std::uint64_t content_hash = 0U;
};

struct TerrainBackdropProduct {
    TerrainBackdropProductRequest request{};
    std::vector<TerrainBackdropSectorMesh> sectors{};
    TerrainBackdropProductDiagnostics diagnostics{};
};

[[nodiscard]] TerrainBackdropProduct
make_terrain_backdrop_product(const TerrainBackdropProductRequest& request);

} // namespace cubey::projects::terrain
