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

struct PlanetPatchGridVertex {
    cubey::render::PrimitiveVec2 uv{};
    float skirt = 0.0F;
};

struct PlanetPatchGridMeshData {
    std::vector<PlanetPatchGridVertex> vertices{};
    std::vector<std::uint32_t> indices{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const {
        return cubey::render::indexed_mesh_config(
            std::span<const PlanetPatchGridVertex>{vertices.data(), vertices.size()},
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
    std::uint32_t refinement_fallback_patch_count = 0;
    std::uint32_t budget_fallback_patch_count = 0;
    std::uint32_t hysteresis_delayed_split_count = 0;
    std::uint32_t hysteresis_delayed_merge_count = 0;
    std::uint32_t transition_candidate_count = 0;
    std::uint32_t lod_neighbor_edge_count = 0;
    std::uint32_t lod_neighbor_boundary_edge_count = 0;
    std::uint32_t lod_neighbor_mismatch_edge_count = 0;
    std::uint32_t max_lod_neighbor_delta = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t seam_edge_count = 0;
    std::uint32_t skirt_triangle_count = 0;
    std::uint32_t min_lod_level = 0;
    std::uint32_t max_lod_level = 0;
    std::array<std::uint32_t, kPlanetDiagnosticLodCapacity> patches_by_lod{};
    float min_screen_error_px = 0.0F;
    float max_screen_error_px = 0.0F;
    float max_transition_pressure = 0.0F;
    float min_edge_length_m = 0.0F;
    float max_edge_length_m = 0.0F;
    std::array<float, kPlanetDiagnosticLodCapacity> min_cell_edge_m_by_lod{};
    std::array<float, kPlanetDiagnosticLodCapacity> max_cell_edge_m_by_lod{};
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

struct PlanetSurfacePatchSelectionHints {
    std::span<const PlanetSurfacePatchId> previous_selected_patches{};
};

struct PlanetSurfacePatchBounds {
    float u0 = -1.0F;
    float v0 = -1.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
};

struct PlanetSurfacePatchInstance {
    PlanetSurfacePatchId id{};
    float screen_error_px = 0.0F;
};

struct PlanetSurfacePatchPlan {
    std::vector<PlanetSurfacePatchInstance> selected_patches{};
    PlanetSurfaceDiagnostics diagnostics{};
};

struct PlanetSurfaceLodNeighborDiagnostics {
    std::uint32_t edge_count = 0;
    std::uint32_t boundary_edge_count = 0;
    std::uint32_t mismatch_edge_count = 0;
    std::uint32_t max_lod_delta = 0;
};

struct PlanetSurfaceGpuPatchInstance {
    std::uint32_t face = 0;
    std::uint32_t level = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t edge_transition_mask = 0;
    float screen_error_px = 0.0F;
};

struct PlanetSurfaceBuildResult {
    PlanetSurfaceMeshData mesh{};
    PlanetSurfaceDiagnostics diagnostics{};
};

[[nodiscard]] PlanetPatchGridMeshData make_planet_patch_grid_mesh(const PlanetConfig& config);
[[nodiscard]] std::vector<PlanetSurfaceGpuPatchInstance>
make_planet_surface_gpu_patch_instances(const PlanetConfig& config,
                                        const PlanetSurfacePatchPlan& plan);
[[nodiscard]] PlanetSurfacePatchBounds planet_surface_patch_bounds(const PlanetConfig& config,
                                                                   PlanetSurfacePatchId id);
[[nodiscard]] PlanetSurfacePatchId planet_surface_child_patch_id(PlanetSurfacePatchId id,
                                                                 std::uint32_t child_index);
[[nodiscard]] PlanetSurfaceLodNeighborDiagnostics
analyze_planet_surface_lod_neighbors(const PlanetConfig& config,
                                     std::span<const PlanetSurfacePatchInstance> patches);
[[nodiscard]] PlanetSurfacePatchPlan plan_planet_surface_patches(const PlanetConfig& config,
                                                                 PlanetSurfaceView view,
                                                                 PlanetSurfacePatchSelectionHints
                                                                     hints = {});
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
