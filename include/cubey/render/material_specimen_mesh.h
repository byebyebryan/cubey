#pragma once

#include <algorithm>
#include <cmath>
#include <cubey/render/pbr.h>

// Existing material-workshop specimen geometry, shared without changing the
// forward vertex/index construction. Consumers may adapt index width/winding.
namespace cubey::render {
[[nodiscard]] inline PrimitiveMeshData<PbrVertex>
make_material_sphere_mesh(float radius, std::uint32_t latitude_segments = 64,
                          std::uint32_t longitude_segments = 128) {
    const PrimitiveMeshData<VertexPositionColorNormalUv> cube =
        make_uv_sphere_position_color_normal_uv_mesh({.radius = radius,
                                                      .latitude_segments = latitude_segments,
                                                      .longitude_segments = longitude_segments});

    PrimitiveMeshData<PbrVertex> result;
    result.vertices.reserve(cube.vertices.size());
    result.indices = cube.indices;
    for (const VertexPositionColorNormalUv& vertex : cube.vertices) {
        const math::Vec3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        auto tangent = glm::cross(math::Vec3{0, 1, 0}, normal);
        tangent = glm::length(tangent) < 1e-5F ? math::Vec3{1, 0, 0} : glm::normalize(tangent);
        result.vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            .tangent = {tangent, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return result;
}

inline PrimitiveMeshData<PbrVertex>
make_material_beveled_block_mesh(math::Vec3 half = math::Vec3{.25F}, float radius = .045F) {
    PrimitiveMeshData<PbrVertex> mesh;
    constexpr int n = 28;
    for (int face = 0; face < 6; ++face) {
        const int axis = face / 2;
        const float sign = face % 2 ? 1.0F : -1.0F;
        const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
        for (int y = 0; y <= n; ++y)
            for (int x = 0; x <= n; ++x) {
                math::Vec3 q{0};
                q[axis] = sign * half[axis];
                q[(axis + 1) % 3] = half[(axis + 1) % 3] * (2.0F * static_cast<float>(x) / n - 1);
                q[(axis + 2) % 3] = half[(axis + 2) % 3] * (2.0F * static_cast<float>(y) / n - 1);
                const auto core = glm::clamp(q, -half + radius, half - radius);
                const auto normal = glm::normalize(q - core);
                const auto p = core + normal * radius;
                auto tangent = glm::cross(math::Vec3{0, 1, 0}, normal);
                tangent =
                    glm::length(tangent) < 1e-5F ? math::Vec3{1, 0, 0} : glm::normalize(tangent);
                mesh.vertices.push_back({.position = p, .normal = normal, .tangent = {tangent, 1}});
            }
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                auto a = static_cast<std::uint16_t>(base + y * (n + 1) + x),
                     b = static_cast<std::uint16_t>(a + 1),
                     c = static_cast<std::uint16_t>(a + n + 1),
                     d = static_cast<std::uint16_t>(c + 1);
                mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
            }
    }
    return mesh;
}

inline PrimitiveMeshData<PbrVertex> make_material_cut_block_mesh() {
    PrimitiveMeshData<PbrVertex> mesh;
    const auto quad = [&](std::array<math::Vec3, 4> p, std::array<math::Vec3, 4> normals) {
        const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
        for (std::size_t i = 0; i < 4; ++i) {
            auto tangent = glm::cross(math::Vec3{0, 1, 0}, normals[i]);
            tangent = glm::length(tangent) < 1e-5F ? math::Vec3{1, 0, 0} : glm::normalize(tangent);
            mesh.vertices.push_back(
                {.position = p[i], .normal = normals[i], .tangent = {tangent, 1}});
        }
        mesh.indices.insert(mesh.indices.end(), {base, static_cast<std::uint16_t>(base + 1),
                                                 static_cast<std::uint16_t>(base + 2), base,
                                                 static_cast<std::uint16_t>(base + 2),
                                                 static_cast<std::uint16_t>(base + 3)});
    };
    const auto edge = [](float angle) {
        const math::Vec2 d{std::cos(angle), std::sin(angle)};
        const float t =
            std::min(.235F / std::max(std::abs(d.x), 1e-6F), .20F / std::max(std::abs(d.y), 1e-6F));
        return d * t;
    };
    constexpr int segments = 128;
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) * 2 * 3.141592654F / segments,
                    b = static_cast<float>(i + 1) * 2 * 3.141592654F / segments;
        const math::Vec2 u{std::cos(a), std::sin(a)}, v{std::cos(b), std::sin(b)};
        const auto outer0 = edge(a), outer1 = edge(b);
        for (float sign : {-1.0F, 1.0F}) {
            const float z = sign * .16F;
            const math::Vec3 n{0, 0, sign};
            quad({math::Vec3{u * .065F, z}, math::Vec3{outer0, z}, math::Vec3{outer1, z},
                  math::Vec3{v * .065F, z}},
                 {n, n, n, n});
        }
        const math::Vec3 n0{-u, 0}, n1{-v, 0};
        quad({math::Vec3{u * .065F, -.16F}, math::Vec3{v * .065F, -.16F},
              math::Vec3{v * .065F, .16F}, math::Vec3{u * .065F, .16F}},
             {n0, n1, n1, n0});
        const math::Vec2 middle = (outer0 + outer1) * .5F;
        const math::Vec3 side = std::abs(middle.x / .235F) > std::abs(middle.y / .20F)
                                    ? math::Vec3{middle.x > 0 ? 1.0F : -1.0F, 0, 0}
                                    : math::Vec3{0, middle.y > 0 ? 1.0F : -1.0F, 0};
        quad({math::Vec3{outer0, -.16F}, math::Vec3{outer1, -.16F}, math::Vec3{outer1, .16F},
              math::Vec3{outer0, .16F}},
             {side, side, side, side});
    }
    return mesh;
}

} // namespace cubey::render
