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

struct SurfacePatch {
    std::uint32_t face = 0;
    std::uint32_t level = 0;
    std::uint32_t patch_index = 0;
    float u0 = -1.0F;
    float v0 = -1.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    float screen_error_px = 0.0F;
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

[[nodiscard]] cubey::math::Vec3 sphere_position(const PlanetConfig& config, std::uint32_t face,
                                                float u, float v) {
    return glm::normalize(cube_face_point(face, u, v)) * config.radius_m;
}

[[nodiscard]] PrimitiveVec3 to_primitive(cubey::math::Vec3 value) {
    return {value.x, value.y, value.z};
}

[[nodiscard]] cubey::math::Vec3 from_primitive(PrimitiveVec3 value) {
    return {value[0], value[1], value[2]};
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

[[nodiscard]] PrimitiveVec3 vertex_color(const PlanetConfig& config, const SurfacePatch& patch,
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
    }
    return final_color(normal);
}

void update_edge_range(PlanetSurfaceDiagnostics& diagnostics, cubey::math::Vec3 a,
                       cubey::math::Vec3 b) {
    const float length = glm::length(a - b);
    if (diagnostics.min_edge_length_m == 0.0F) {
        diagnostics.min_edge_length_m = length;
    } else {
        diagnostics.min_edge_length_m = std::min(diagnostics.min_edge_length_m, length);
    }
    diagnostics.max_edge_length_m = std::max(diagnostics.max_edge_length_m, length);
}

void update_screen_error_range(PlanetSurfaceDiagnostics& diagnostics, float value) {
    if (diagnostics.patch_count == 0U || diagnostics.min_screen_error_px == 0.0F) {
        diagnostics.min_screen_error_px = value;
    } else {
        diagnostics.min_screen_error_px = std::min(diagnostics.min_screen_error_px, value);
    }
    diagnostics.max_screen_error_px = std::max(diagnostics.max_screen_error_px, value);
}

[[nodiscard]] float patch_screen_error_px(const PlanetConfig& config, PlanetSurfaceView view,
                                          const SurfacePatch& patch) {
    const float u_mid = (patch.u0 + patch.u1) * 0.5F;
    const float v_mid = (patch.v0 + patch.v1) * 0.5F;
    const cubey::math::Vec3 center = sphere_position(config, patch.face, u_mid, v_mid);
    const cubey::math::Vec3 edge_a = sphere_position(config, patch.face, patch.u0, v_mid);
    const cubey::math::Vec3 edge_b = sphere_position(config, patch.face, patch.u1, v_mid);
    const float patch_edge_m = glm::length(edge_b - edge_a);
    const float cell_edge_m = patch_edge_m / static_cast<float>(config.patch_resolution);
    const float distance_m = std::max(glm::length(center - view.camera_position_m), 1.0F);
    const float pixel_scale =
        view.viewport_height_px / (2.0F * std::tan(view.vertical_fov_radians * 0.5F));
    return (cell_edge_m / distance_m) * pixel_scale;
}

void append_leaf_patches(const PlanetConfig& config, PlanetSurfaceView view, SurfacePatch patch,
                         std::vector<SurfacePatch>& patches) {
    patch.screen_error_px = patch_screen_error_px(config, view, patch);
    if (patch.level < config.max_lod_level && patch.screen_error_px > config.lod_target_edge_px) {
        const float um = (patch.u0 + patch.u1) * 0.5F;
        const float vm = (patch.v0 + patch.v1) * 0.5F;
        const std::uint32_t next_level = patch.level + 1U;
        const std::uint32_t child_base = patch.patch_index * 4U;
        append_leaf_patches(config, view,
                            SurfacePatch{.face = patch.face,
                                         .level = next_level,
                                         .patch_index = child_base,
                                         .u0 = patch.u0,
                                         .v0 = patch.v0,
                                         .u1 = um,
                                         .v1 = vm},
                            patches);
        append_leaf_patches(config, view,
                            SurfacePatch{.face = patch.face,
                                         .level = next_level,
                                         .patch_index = child_base + 1U,
                                         .u0 = um,
                                         .v0 = patch.v0,
                                         .u1 = patch.u1,
                                         .v1 = vm},
                            patches);
        append_leaf_patches(config, view,
                            SurfacePatch{.face = patch.face,
                                         .level = next_level,
                                         .patch_index = child_base + 2U,
                                         .u0 = patch.u0,
                                         .v0 = vm,
                                         .u1 = um,
                                         .v1 = patch.v1},
                            patches);
        append_leaf_patches(config, view,
                            SurfacePatch{.face = patch.face,
                                         .level = next_level,
                                         .patch_index = child_base + 3U,
                                         .u0 = um,
                                         .v0 = vm,
                                         .u1 = patch.u1,
                                         .v1 = patch.v1},
                            patches);
        return;
    }
    patches.push_back(patch);
}

[[nodiscard]] std::vector<SurfacePatch> make_surface_patches(const PlanetConfig& config,
                                                             PlanetSurfaceView view) {
    std::vector<SurfacePatch> patches;
    const float inv_patch_count = 1.0F / static_cast<float>(config.patches_per_face);
    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t py = 0; py < config.patches_per_face; ++py) {
            for (std::uint32_t px = 0; px < config.patches_per_face; ++px) {
                const std::uint32_t patch_index =
                    face * config.patches_per_face * config.patches_per_face +
                    py * config.patches_per_face + px;
                append_leaf_patches(
                    config, view,
                    SurfacePatch{
                        .face = face,
                        .level = 0,
                        .patch_index = patch_index,
                        .u0 = -1.0F + 2.0F * static_cast<float>(px) * inv_patch_count,
                        .v0 = -1.0F + 2.0F * static_cast<float>(py) * inv_patch_count,
                        .u1 = -1.0F + 2.0F * static_cast<float>(px + 1U) * inv_patch_count,
                        .v1 = -1.0F + 2.0F * static_cast<float>(py + 1U) * inv_patch_count,
                    },
                    patches);
            }
        }
    }
    return patches;
}

void append_patch_mesh(const PlanetConfig& config, const SurfacePatch& patch,
                       PlanetSurfaceBuildResult& result) {
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
            const cubey::math::Vec3 position = normal * config.radius_m;
            result.mesh.vertices.push_back(VertexPositionColorNormalUv{
                .position = to_primitive(position),
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

            update_edge_range(result.diagnostics, from_primitive(result.mesh.vertices[i0].position),
                              from_primitive(result.mesh.vertices[i1].position));
            update_edge_range(result.diagnostics, from_primitive(result.mesh.vertices[i0].position),
                              from_primitive(result.mesh.vertices[i2].position));
        }
    }
}

} // namespace

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config) {
    const float camera_distance =
        std::max(config.radius_m + config.camera_altitude_m, config.radius_m * 1.01F);
    const PlanetSurfaceView view{
        .camera_position_m = {0.0F, 0.0F, camera_distance},
    };
    return make_planet_surface_mesh(config, view);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view) {
    validate_planet_config(config);

    PlanetSurfaceBuildResult result{};
    const std::vector<SurfacePatch> patches = make_surface_patches(config, view);
    result.diagnostics.patch_count = static_cast<std::uint32_t>(patches.size());
    result.diagnostics.min_lod_level = config.max_lod_level;
    for (const SurfacePatch& patch : patches) {
        result.diagnostics.min_lod_level = std::min(result.diagnostics.min_lod_level, patch.level);
        result.diagnostics.max_lod_level = std::max(result.diagnostics.max_lod_level, patch.level);
        if (patch.level < result.diagnostics.patches_by_lod.size()) {
            ++result.diagnostics.patches_by_lod[patch.level];
        }
        update_screen_error_range(result.diagnostics, patch.screen_error_px);
        append_patch_mesh(config, patch, result);
    }

    result.diagnostics.vertex_count = static_cast<std::uint32_t>(result.mesh.vertices.size());
    result.diagnostics.triangle_count = static_cast<std::uint32_t>(result.mesh.indices.size() / 3U);
    if (patches.empty()) {
        result.diagnostics.min_lod_level = 0;
    }
    return result;
}

} // namespace cubey::projects::planet
