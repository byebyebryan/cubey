#include "planet_surface.h"

#include <cubey/core/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

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

[[nodiscard]] PrimitiveVec3 vertex_color(const PlanetConfig& config, std::uint32_t face,
                                         std::uint32_t patch_index, cubey::math::Vec3 normal) {
    switch (config.debug_view) {
    case PlanetDebugView::Final:
        return final_color(normal);
    case PlanetDebugView::FaceId:
        return kFaceColors[face];
    case PlanetDebugView::PatchId:
        return patch_color(patch_index);
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

} // namespace

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config) {
    validate_planet_config(config);

    PlanetSurfaceBuildResult result{};
    const std::uint32_t patch_resolution = config.patch_resolution;
    const std::uint32_t vertices_per_side = patch_resolution + 1U;
    const float inv_patch_count = 1.0F / static_cast<float>(config.patches_per_face);
    result.diagnostics.patch_count = 6U * config.patches_per_face * config.patches_per_face;

    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t py = 0; py < config.patches_per_face; ++py) {
            for (std::uint32_t px = 0; px < config.patches_per_face; ++px) {
                const std::uint32_t patch_index =
                    face * config.patches_per_face * config.patches_per_face +
                    py * config.patches_per_face + px;
                const std::uint32_t base_vertex =
                    static_cast<std::uint32_t>(result.mesh.vertices.size());
                const float patch_u0 = -1.0F + 2.0F * static_cast<float>(px) * inv_patch_count;
                const float patch_v0 = -1.0F + 2.0F * static_cast<float>(py) * inv_patch_count;
                const float patch_u1 = -1.0F + 2.0F * static_cast<float>(px + 1U) * inv_patch_count;
                const float patch_v1 = -1.0F + 2.0F * static_cast<float>(py + 1U) * inv_patch_count;

                for (std::uint32_t y = 0; y <= patch_resolution; ++y) {
                    const float tv = static_cast<float>(y) / static_cast<float>(patch_resolution);
                    const float v = patch_v0 + (patch_v1 - patch_v0) * tv;
                    for (std::uint32_t x = 0; x <= patch_resolution; ++x) {
                        const float tu =
                            static_cast<float>(x) / static_cast<float>(patch_resolution);
                        const float u = patch_u0 + (patch_u1 - patch_u0) * tu;
                        const cubey::math::Vec3 normal =
                            glm::normalize(cube_face_point(face, u, v));
                        const cubey::math::Vec3 position = normal * config.radius_m;
                        result.mesh.vertices.push_back(VertexPositionColorNormalUv{
                            .position = to_primitive(position),
                            .color = vertex_color(config, face, patch_index, normal),
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
                        if (i3 > std::numeric_limits<std::uint16_t>::max()) {
                            throw std::runtime_error("planet surface index exceeds uint16 limit");
                        }
                        result.mesh.indices.push_back(static_cast<std::uint16_t>(i0));
                        result.mesh.indices.push_back(static_cast<std::uint16_t>(i2));
                        result.mesh.indices.push_back(static_cast<std::uint16_t>(i1));
                        result.mesh.indices.push_back(static_cast<std::uint16_t>(i1));
                        result.mesh.indices.push_back(static_cast<std::uint16_t>(i2));
                        result.mesh.indices.push_back(static_cast<std::uint16_t>(i3));

                        update_edge_range(result.diagnostics,
                                          from_primitive(result.mesh.vertices[i0].position),
                                          from_primitive(result.mesh.vertices[i1].position));
                        update_edge_range(result.diagnostics,
                                          from_primitive(result.mesh.vertices[i0].position),
                                          from_primitive(result.mesh.vertices[i2].position));
                    }
                }
            }
        }
    }

    result.diagnostics.vertex_count = static_cast<std::uint32_t>(result.mesh.vertices.size());
    result.diagnostics.triangle_count = static_cast<std::uint32_t>(result.mesh.indices.size() / 3U);
    return result;
}

} // namespace cubey::projects::planet
