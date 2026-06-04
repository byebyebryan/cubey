#include "planet_surface.h"

#include <cubey/core/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cubey::projects::planet {
namespace {

using cubey::render::PrimitiveVec2;
using cubey::render::PrimitiveVec3;
using cubey::render::VertexPositionColorNormalUv;

constexpr std::array<PrimitiveVec3, 6> kFaceColors{
    PrimitiveVec3{0.95F, 0.22F, 0.18F}, PrimitiveVec3{0.18F, 0.45F, 0.95F},
    PrimitiveVec3{0.20F, 0.78F, 0.36F}, PrimitiveVec3{0.96F, 0.70F, 0.18F},
    PrimitiveVec3{0.58F, 0.30F, 0.92F}, PrimitiveVec3{0.15F, 0.78F, 0.78F},
};

[[nodiscard]] cubey::math::Vec3 cube_face_point(std::uint32_t face, float u, float v) {
    switch (face) {
    case 0:
        return {1.0F, v, -u};
    case 1:
        return {-1.0F, v, u};
    case 2:
        return {u, 1.0F, -v};
    case 3:
        return {u, -1.0F, v};
    case 4:
        return {u, v, 1.0F};
    case 5:
        return {-u, v, -1.0F};
    default:
        return {0.0F, 1.0F, 0.0F};
    }
}

[[nodiscard]] cubey::math::DVec3 sphere_world_position(const PlanetConfig& config,
                                                       std::uint32_t face, float u, float v) {
    const cubey::math::Vec3 normal = glm::normalize(cube_face_point(face, u, v));
    return {
        static_cast<double>(normal.x) * static_cast<double>(config.radius_m),
        static_cast<double>(normal.y) * static_cast<double>(config.radius_m),
        static_cast<double>(normal.z) * static_cast<double>(config.radius_m),
    };
}

[[nodiscard]] PrimitiveVec3 to_primitive(cubey::math::Vec3 value) {
    return {value.x, value.y, value.z};
}

[[nodiscard]] PrimitiveVec3 final_color(cubey::math::Vec3 normal) {
    const float latitude = normal.y * 0.5F + 0.5F;
    return {
        0.035F + 0.030F * latitude,
        0.100F + 0.070F * latitude,
        0.230F + 0.200F * latitude,
    };
}

[[nodiscard]] PrimitiveVec3 patch_color(std::uint32_t patch_index) {
    const float band = static_cast<float>((patch_index * 37U) % 97U) / 96.0F;
    return {
        0.18F + 0.58F * band,
        0.78F - 0.42F * band,
        0.28F + 0.36F * (1.0F - band),
    };
}

[[nodiscard]] PrimitiveVec3 lod_color(std::uint32_t level, std::uint32_t max_level) {
    const float t =
        max_level == 0U ? 0.0F : static_cast<float>(level) / static_cast<float>(max_level);
    return {
        0.12F + 0.82F * t,
        0.55F - 0.28F * t,
        0.95F - 0.76F * t,
    };
}

[[nodiscard]] PrimitiveVec3 screen_error_color(float error_px, float target_px) {
    const float t = std::clamp(error_px / std::max(target_px, 0.0001F), 0.0F, 2.0F) * 0.5F;
    return {
        0.16F + 0.80F * t,
        0.82F - 0.46F * t,
        0.24F,
    };
}

[[nodiscard]] PrimitiveVec3 seam_surface_color(cubey::math::Vec3 normal) {
    const PrimitiveVec3 color = final_color(normal);
    return {
        color[0] * 0.28F,
        color[1] * 0.34F,
        color[2] * 0.42F,
    };
}

[[nodiscard]] PrimitiveVec3 skirt_color(const PlanetConfig& config, cubey::math::Vec3 normal) {
    if (config.debug_view == PlanetDebugView::Seams) {
        return {1.0F, 0.82F, 0.22F};
    }
    return final_color(normal);
}

[[nodiscard]] PrimitiveVec3 vertex_color(const PlanetConfig& config,
                                         const PlanetSurfacePatch& patch,
                                         cubey::math::Vec3 normal) {
    switch (config.debug_view) {
    case PlanetDebugView::Final:
        return final_color(normal);
    case PlanetDebugView::FaceId:
        return kFaceColors[patch.face];
    case PlanetDebugView::PatchId:
        return patch_color(patch.patch_index);
    case PlanetDebugView::LodLevel:
        return lod_color(patch.level, config.max_lod_level);
    case PlanetDebugView::ScreenError:
        return screen_error_color(patch.screen_error_px, config.lod_target_edge_px);
    case PlanetDebugView::Seams:
        return seam_surface_color(normal);
    }
    return final_color(normal);
}

void update_edge_range(PlanetSurfaceDiagnostics& diagnostics, cubey::math::DVec3 a,
                       cubey::math::DVec3 b) {
    const float length = static_cast<float>(glm::length(a - b));
    if (diagnostics.min_edge_length_m == 0.0F) {
        diagnostics.min_edge_length_m = length;
    } else {
        diagnostics.min_edge_length_m = std::min(diagnostics.min_edge_length_m, length);
    }
    diagnostics.max_edge_length_m = std::max(diagnostics.max_edge_length_m, length);
}

void update_screen_error_range(PlanetSurfaceDiagnostics& diagnostics, float value) {
    if (diagnostics.visible_patch_count == 0U || diagnostics.min_screen_error_px == 0.0F) {
        diagnostics.min_screen_error_px = value;
    } else {
        diagnostics.min_screen_error_px = std::min(diagnostics.min_screen_error_px, value);
    }
    diagnostics.max_screen_error_px = std::max(diagnostics.max_screen_error_px, value);
}

[[nodiscard]] std::array<cubey::math::DVec3, 5>
patch_sample_points(const PlanetConfig& config, const PlanetSurfacePatch& patch) {
    const float u_mid = (patch.u0 + patch.u1) * 0.5F;
    const float v_mid = (patch.v0 + patch.v1) * 0.5F;
    return {
        sphere_world_position(config, patch.face, u_mid, v_mid),
        sphere_world_position(config, patch.face, patch.u0, patch.v0),
        sphere_world_position(config, patch.face, patch.u1, patch.v0),
        sphere_world_position(config, patch.face, patch.u0, patch.v1),
        sphere_world_position(config, patch.face, patch.u1, patch.v1),
    };
}

[[nodiscard]] bool patch_passes_horizon_cull(const PlanetConfig& config, PlanetSurfaceView view,
                                             const PlanetSurfacePatch& patch) {
    if (!view.culling_enabled) {
        return true;
    }
    const double camera_distance_m = glm::length(view.camera_world_position_m);
    if (camera_distance_m <= static_cast<double>(config.radius_m) * 1.001) {
        return true;
    }

    const double radius_m = static_cast<double>(config.radius_m);
    const double horizon_dot_m2 = radius_m * radius_m;
    const double conservative_margin_m2 = horizon_dot_m2 * 0.08;
    for (cubey::math::DVec3 sample : patch_sample_points(config, patch)) {
        if (glm::dot(sample, view.camera_world_position_m) >=
            horizon_dot_m2 - conservative_margin_m2) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] cubey::math::Vec3 normalized_camera_forward(PlanetSurfaceView view) {
    if (glm::length(view.camera_forward_world) <= 0.0001F) {
        return {0.0F, 0.0F, -1.0F};
    }
    return glm::normalize(view.camera_forward_world);
}

[[nodiscard]] bool patch_passes_view_cull(const PlanetConfig& config, PlanetSurfaceView view,
                                          const PlanetSurfacePatch& patch) {
    if (!view.culling_enabled) {
        return true;
    }
    const cubey::math::Vec3 forward = normalized_camera_forward(view);
    const float aspect = std::max(view.aspect_ratio, 0.001F);
    const float tan_half_vertical = std::tan(view.vertical_fov_radians * 0.5F);
    const float diagonal_half_angle =
        std::atan(tan_half_vertical * std::sqrt(1.0F + aspect * aspect));
    const float conservative_margin_radians = 0.22F;
    const float cos_limit =
        std::cos(std::min(diagonal_half_angle + conservative_margin_radians, 3.0F));

    for (cubey::math::DVec3 sample : patch_sample_points(config, patch)) {
        const cubey::math::DVec3 to_sample_d = sample - view.camera_world_position_m;
        const double distance_m = glm::length(to_sample_d);
        if (distance_m <= 0.0001) {
            return true;
        }
        const cubey::math::Vec3 to_sample{
            static_cast<float>(to_sample_d.x / distance_m),
            static_cast<float>(to_sample_d.y / distance_m),
            static_cast<float>(to_sample_d.z / distance_m),
        };
        if (glm::dot(forward, to_sample) >= cos_limit) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] float patch_screen_error_px(const PlanetConfig& config, PlanetSurfaceView view,
                                          const PlanetSurfacePatch& patch) {
    const float u_mid = (patch.u0 + patch.u1) * 0.5F;
    const float v_mid = (patch.v0 + patch.v1) * 0.5F;
    const cubey::math::DVec3 center = sphere_world_position(config, patch.face, u_mid, v_mid);
    const cubey::math::DVec3 edge_a = sphere_world_position(config, patch.face, patch.u0, v_mid);
    const cubey::math::DVec3 edge_b = sphere_world_position(config, patch.face, patch.u1, v_mid);
    const float patch_edge_m = static_cast<float>(glm::length(edge_b - edge_a));
    const float cell_edge_m = patch_edge_m / static_cast<float>(config.patch_resolution);
    const float distance_m =
        std::max(static_cast<float>(glm::length(center - view.camera_world_position_m)), 1.0F);
    const float pixel_scale =
        view.viewport_height_px / (2.0F * std::tan(view.vertical_fov_radians * 0.5F));
    return (cell_edge_m / distance_m) * pixel_scale;
}

void record_culled_patch(PlanetSurfacePatchPlan& plan, bool horizon_culled) {
    ++plan.diagnostics.planned_patch_count;
    if (horizon_culled) {
        ++plan.diagnostics.culled_horizon_count;
    } else {
        ++plan.diagnostics.culled_view_count;
    }
}

void record_visible_patch(PlanetSurfacePatchPlan& plan, const PlanetSurfacePatch& patch) {
    ++plan.diagnostics.planned_patch_count;
    plan.diagnostics.min_lod_level = plan.diagnostics.visible_patch_count == 0U
                                         ? patch.level
                                         : std::min(plan.diagnostics.min_lod_level, patch.level);
    plan.diagnostics.max_lod_level = std::max(plan.diagnostics.max_lod_level, patch.level);
    if (patch.level < plan.diagnostics.patches_by_lod.size()) {
        ++plan.diagnostics.patches_by_lod[patch.level];
    }
    update_screen_error_range(plan.diagnostics, patch.screen_error_px);
    ++plan.diagnostics.visible_patch_count;
    plan.diagnostics.patch_count = plan.diagnostics.visible_patch_count;
    plan.patches.push_back(patch);
}

void append_leaf_patches(const PlanetConfig& config, PlanetSurfaceView view,
                         PlanetSurfacePatch patch, PlanetSurfacePatchPlan& plan) {
    patch.screen_error_px = patch_screen_error_px(config, view, patch);

    if (!patch_passes_horizon_cull(config, view, patch)) {
        record_culled_patch(plan, true);
        return;
    }
    if (!patch_passes_view_cull(config, view, patch)) {
        record_culled_patch(plan, false);
        return;
    }

    if (patch.level < config.max_lod_level && patch.screen_error_px > config.lod_target_edge_px) {
        const float um = (patch.u0 + patch.u1) * 0.5F;
        const float vm = (patch.v0 + patch.v1) * 0.5F;
        const std::uint32_t next_level = patch.level + 1U;
        const std::uint32_t child_base = patch.patch_index * 4U;
        append_leaf_patches(config, view,
                            PlanetSurfacePatch{.face = patch.face,
                                               .level = next_level,
                                               .patch_index = child_base,
                                               .u0 = patch.u0,
                                               .v0 = patch.v0,
                                               .u1 = um,
                                               .v1 = vm},
                            plan);
        append_leaf_patches(config, view,
                            PlanetSurfacePatch{.face = patch.face,
                                               .level = next_level,
                                               .patch_index = child_base + 1U,
                                               .u0 = um,
                                               .v0 = patch.v0,
                                               .u1 = patch.u1,
                                               .v1 = vm},
                            plan);
        append_leaf_patches(config, view,
                            PlanetSurfacePatch{.face = patch.face,
                                               .level = next_level,
                                               .patch_index = child_base + 2U,
                                               .u0 = patch.u0,
                                               .v0 = vm,
                                               .u1 = um,
                                               .v1 = patch.v1},
                            plan);
        append_leaf_patches(config, view,
                            PlanetSurfacePatch{.face = patch.face,
                                               .level = next_level,
                                               .patch_index = child_base + 3U,
                                               .u0 = um,
                                               .v0 = vm,
                                               .u1 = patch.u1,
                                               .v1 = patch.v1},
                            plan);
        return;
    }
    record_visible_patch(plan, patch);
}

[[nodiscard]] PlanetSurfacePatchPlan make_surface_patch_plan(const PlanetConfig& config,
                                                             PlanetSurfaceView view) {
    PlanetSurfacePatchPlan plan{};
    const float inv_patch_count = 1.0F / static_cast<float>(config.patches_per_face);
    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t py = 0; py < config.patches_per_face; ++py) {
            for (std::uint32_t px = 0; px < config.patches_per_face; ++px) {
                const std::uint32_t patch_index =
                    face * config.patches_per_face * config.patches_per_face +
                    py * config.patches_per_face + px;
                append_leaf_patches(
                    config, view,
                    PlanetSurfacePatch{
                        .face = face,
                        .level = 0,
                        .patch_index = patch_index,
                        .u0 = -1.0F + 2.0F * static_cast<float>(px) * inv_patch_count,
                        .v0 = -1.0F + 2.0F * static_cast<float>(py) * inv_patch_count,
                        .u1 = -1.0F + 2.0F * static_cast<float>(px + 1U) * inv_patch_count,
                        .v1 = -1.0F + 2.0F * static_cast<float>(py + 1U) * inv_patch_count,
                    },
                    plan);
            }
        }
    }
    return plan;
}

[[nodiscard]] cubey::math::Vec3
vertex_position(const cubey::render::VertexPositionColorNormalUv& vertex) {
    return {vertex.position[0], vertex.position[1], vertex.position[2]};
}

[[nodiscard]] cubey::math::Vec3
vertex_normal(const cubey::render::VertexPositionColorNormalUv& vertex) {
    return glm::normalize(cubey::math::Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]});
}

[[nodiscard]] float patch_skirt_depth_m(const PlanetConfig& config,
                                        const PlanetSurfacePatch& patch) {
    const float u_mid = (patch.u0 + patch.u1) * 0.5F;
    const float v_mid = (patch.v0 + patch.v1) * 0.5F;
    const cubey::math::DVec3 edge_a = sphere_world_position(config, patch.face, patch.u0, v_mid);
    const cubey::math::DVec3 edge_b = sphere_world_position(config, patch.face, patch.u1, v_mid);
    const cubey::math::DVec3 edge_c = sphere_world_position(config, patch.face, u_mid, patch.v0);
    const cubey::math::DVec3 edge_d = sphere_world_position(config, patch.face, u_mid, patch.v1);
    const float horizontal_cell_m = static_cast<float>(glm::length(edge_b - edge_a)) /
                                    static_cast<float>(config.patch_resolution);
    const float vertical_cell_m = static_cast<float>(glm::length(edge_d - edge_c)) /
                                  static_cast<float>(config.patch_resolution);
    return std::max(std::min(horizontal_cell_m, vertical_cell_m) * config.skirt_depth_scale,
                    config.radius_m * 0.00001F);
}

void update_skirt_depth_range(PlanetSurfaceDiagnostics& diagnostics, float depth_m) {
    if (diagnostics.skirt_triangle_count == 0U || diagnostics.min_skirt_depth_m == 0.0F) {
        diagnostics.min_skirt_depth_m = depth_m;
    } else {
        diagnostics.min_skirt_depth_m = std::min(diagnostics.min_skirt_depth_m, depth_m);
    }
    diagnostics.max_skirt_depth_m = std::max(diagnostics.max_skirt_depth_m, depth_m);
}

[[nodiscard]] std::uint32_t append_skirt_vertex(const PlanetConfig& config,
                                                const PlanetFrame& frame,
                                                PlanetSurfaceBuildResult& result,
                                                std::uint32_t top_index, float depth_m) {
    const cubey::render::VertexPositionColorNormalUv& top = result.mesh.vertices[top_index];
    const cubey::math::Vec3 normal = vertex_normal(top);
    const cubey::math::DVec3 top_world =
        planet_frame_render_to_world_m(frame, vertex_position(top));
    const cubey::math::DVec3 bottom_world =
        top_world - cubey::math::DVec3{normal.x, normal.y, normal.z} * static_cast<double>(depth_m);
    const cubey::math::Vec3 bottom_render = planet_frame_world_to_render_m(frame, bottom_world);
    const std::uint32_t bottom_index = static_cast<std::uint32_t>(result.mesh.vertices.size());
    result.mesh.vertices.push_back(VertexPositionColorNormalUv{
        .position = to_primitive(bottom_render),
        .color = skirt_color(config, normal),
        .normal = to_primitive(normal),
        .uv = top.uv,
    });
    return bottom_index;
}

void append_skirt_segment(const PlanetConfig& config, const PlanetFrame& frame,
                          PlanetSurfaceBuildResult& result, std::uint32_t top0, std::uint32_t top1,
                          float depth_m) {
    const std::uint32_t bottom0 = append_skirt_vertex(config, frame, result, top0, depth_m);
    const std::uint32_t bottom1 = append_skirt_vertex(config, frame, result, top1, depth_m);
    const auto push_triangle = [&result](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        result.mesh.indices.push_back(a);
        result.mesh.indices.push_back(b);
        result.mesh.indices.push_back(c);
    };

    push_triangle(top0, bottom0, top1);
    push_triangle(top1, bottom0, bottom1);
    push_triangle(top1, bottom0, top0);
    push_triangle(bottom1, bottom0, top1);
    result.diagnostics.skirt_triangle_count += 4U;
}

void append_patch_skirts(const PlanetConfig& config, const PlanetFrame& frame,
                         const PlanetSurfacePatch& patch, std::uint32_t base_vertex,
                         std::uint32_t vertices_per_side, PlanetSurfaceBuildResult& result) {
    if (!config.skirts_enabled) {
        return;
    }

    const float depth_m = patch_skirt_depth_m(config, patch);
    update_skirt_depth_range(result.diagnostics, depth_m);
    result.diagnostics.seam_edge_count += 4U;

    const std::uint32_t patch_resolution = config.patch_resolution;
    for (std::uint32_t x = 0; x < patch_resolution; ++x) {
        append_skirt_segment(config, frame, result, base_vertex + x, base_vertex + x + 1U, depth_m);
        const std::uint32_t bottom_row = base_vertex + patch_resolution * vertices_per_side;
        append_skirt_segment(config, frame, result, bottom_row + x, bottom_row + x + 1U, depth_m);
    }
    for (std::uint32_t y = 0; y < patch_resolution; ++y) {
        append_skirt_segment(config, frame, result, base_vertex + y * vertices_per_side,
                             base_vertex + (y + 1U) * vertices_per_side, depth_m);
        append_skirt_segment(
            config, frame, result, base_vertex + y * vertices_per_side + patch_resolution,
            base_vertex + (y + 1U) * vertices_per_side + patch_resolution, depth_m);
    }
}

void append_patch_mesh(const PlanetConfig& config, const PlanetFrame& frame,
                       const PlanetSurfacePatch& patch, PlanetSurfaceBuildResult& result) {
    const std::uint32_t patch_resolution = config.patch_resolution;
    const std::uint32_t vertices_per_side = patch_resolution + 1U;
    const std::uint32_t base_vertex = static_cast<std::uint32_t>(result.mesh.vertices.size());

    for (std::uint32_t y = 0; y <= patch_resolution; ++y) {
        const float tv = static_cast<float>(y) / static_cast<float>(patch_resolution);
        const float v = patch.v0 + (patch.v1 - patch.v0) * tv;
        for (std::uint32_t x = 0; x <= patch_resolution; ++x) {
            const float tu = static_cast<float>(x) / static_cast<float>(patch_resolution);
            const float u = patch.u0 + (patch.u1 - patch.u0) * tu;
            const cubey::math::Vec3 normal = glm::normalize(cube_face_point(patch.face, u, v));
            const cubey::math::DVec3 world_position =
                sphere_world_position(config, patch.face, u, v);
            const cubey::math::Vec3 render_position =
                planet_frame_world_to_render_m(frame, world_position);
            result.mesh.vertices.push_back(VertexPositionColorNormalUv{
                .position = to_primitive(render_position),
                .color = vertex_color(config, patch, normal),
                .normal = to_primitive(normal),
                .uv = PrimitiveVec2{tu, tv},
            });
        }
    }

    for (std::uint32_t y = 0; y < patch_resolution; ++y) {
        for (std::uint32_t x = 0; x < patch_resolution; ++x) {
            const std::uint32_t i0 = base_vertex + y * vertices_per_side + x;
            const std::uint32_t i1 = i0 + 1U;
            const std::uint32_t i2 = i0 + vertices_per_side;
            const std::uint32_t i3 = i2 + 1U;
            result.mesh.indices.push_back(i0);
            result.mesh.indices.push_back(i2);
            result.mesh.indices.push_back(i1);
            result.mesh.indices.push_back(i1);
            result.mesh.indices.push_back(i2);
            result.mesh.indices.push_back(i3);

            const cubey::math::DVec3 w0 = planet_frame_render_to_world_m(
                frame, {result.mesh.vertices[i0].position[0], result.mesh.vertices[i0].position[1],
                        result.mesh.vertices[i0].position[2]});
            const cubey::math::DVec3 w1 = planet_frame_render_to_world_m(
                frame, {result.mesh.vertices[i1].position[0], result.mesh.vertices[i1].position[1],
                        result.mesh.vertices[i1].position[2]});
            const cubey::math::DVec3 w2 = planet_frame_render_to_world_m(
                frame, {result.mesh.vertices[i2].position[0], result.mesh.vertices[i2].position[1],
                        result.mesh.vertices[i2].position[2]});
            update_edge_range(result.diagnostics, w0, w1);
            update_edge_range(result.diagnostics, w0, w2);
        }
    }

    append_patch_skirts(config, frame, patch, base_vertex, vertices_per_side, result);
}

} // namespace

PlanetSurfacePatchPlan plan_planet_surface_patches(const PlanetConfig& config,
                                                   PlanetSurfaceView view) {
    validate_planet_config(config);
    return make_surface_patch_plan(config, view);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config) {
    const float camera_distance =
        std::max(config.radius_m + config.camera_altitude_m, config.radius_m * 1.01F);
    const PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, camera_distance},
    };
    return make_planet_surface_mesh(config, view);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view) {
    PlanetFrame frame{};
    frame.planet_radius_m = config.radius_m;
    frame.camera_world_position_m = view.camera_world_position_m;
    frame.render_origin_world_m = {0.0, 0.0, 0.0};
    return make_planet_surface_mesh(config, view, frame);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view,
                                                  const PlanetFrame& frame) {
    const PlanetSurfacePatchPlan plan = plan_planet_surface_patches(config, view);
    return make_planet_surface_mesh(config, view, frame, plan);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view, const PlanetFrame& frame,
                                                  const PlanetSurfacePatchPlan& plan) {
    validate_planet_config(config);
    (void)view;

    PlanetSurfaceBuildResult result{};
    result.diagnostics = plan.diagnostics;
    for (const PlanetSurfacePatch& patch : plan.patches) {
        append_patch_mesh(config, frame, patch, result);
    }

    result.diagnostics.vertex_count = static_cast<std::uint32_t>(result.mesh.vertices.size());
    result.diagnostics.triangle_count = static_cast<std::uint32_t>(result.mesh.indices.size() / 3U);
    if (plan.patches.empty()) {
        result.diagnostics.min_lod_level = 0;
    }
    return result;
}

} // namespace cubey::projects::planet
