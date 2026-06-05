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
    std::uint32_t planned_patch_count = 0;
    std::uint32_t visible_patch_count = 0;
    std::uint32_t culled_horizon_count = 0;
    std::uint32_t culled_view_count = 0;
    std::uint32_t patch_count = 0;
    std::uint32_t base_patch_count = 0;
    std::uint32_t refined_patch_count = 0;
    std::uint32_t subdivided_patch_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t seam_edge_count = 0;
    std::uint32_t skirt_triangle_count = 0;
    std::uint32_t min_lod_level = 0;
    std::uint32_t max_lod_level = 0;
    std::array<std::uint32_t, 4> patches_by_lod{};
    float min_screen_error_px = 0.0F;
    float max_screen_error_px = 0.0F;
    float min_edge_length_m = 0.0F;
    float max_edge_length_m = 0.0F;
    float min_skirt_depth_m = 0.0F;
    float max_skirt_depth_m = 0.0F;
};

struct PlanetSurfaceView {
    cubey::math::DVec3 camera_world_position_m{
        0.0, 0.0, kPlanetDefaultRadiusM + kPlanetDefaultCameraAltitudeM};
    cubey::math::Vec3 camera_forward_world{0.0F, 0.0F, -1.0F};
    float vertical_fov_radians = 1.0471975803375244F;
    float aspect_ratio = 16.0F / 9.0F;
    float viewport_height_px = 720.0F;
    bool culling_enabled = false;
};

struct PlanetSurfacePatchId {
    std::uint32_t face = 0;
    std::uint32_t level = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    friend bool operator==(const PlanetSurfacePatchId&, const PlanetSurfacePatchId&) = default;
};

struct PlanetSurfacePatchBounds {
    float u0 = -1.0F;
    float v0 = -1.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
};

struct PlanetSurfacePatch {
    PlanetSurfacePatchId id{};
    float screen_error_px = 0.0F;
};

struct PlanetSurfacePatchPlan {
    std::vector<PlanetSurfacePatch> patches{};
    PlanetSurfaceDiagnostics diagnostics{};
};

struct PlanetSurfaceBuildResult {
    PlanetSurfaceMeshData mesh{};
    PlanetSurfaceDiagnostics diagnostics{};
};

[[nodiscard]] PlanetSurfacePatchBounds planet_surface_patch_bounds(const PlanetConfig& config,
                                                                   PlanetSurfacePatchId id);
[[nodiscard]] PlanetSurfacePatchId planet_surface_child_patch_id(PlanetSurfacePatchId id,
                                                                 std::uint32_t child_index);
[[nodiscard]] PlanetSurfacePatchPlan plan_planet_surface_patches(const PlanetConfig& config,
                                                                 PlanetSurfaceView view);
[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config);
[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                                PlanetSurfaceView view);
[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                                PlanetSurfaceView view,
                                                                const PlanetFrame& frame);
[[nodiscard]] PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                                PlanetSurfaceView view,
                                                                const PlanetFrame& frame,
                                                                const PlanetSurfacePatchPlan& plan);

} // namespace cubey::projects::planet
