#include "gltf_viewer_loading_cage.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cubey::projects::gltf_viewer {
namespace {

using cubey::asset::GltfBounds3D;
using cubey::math::Vec3;
using cubey::render::PbrVertex;
using AxisIndex = glm::length_t;

struct BoundsAccumulator {
    Vec3 min{0.0F};
    Vec3 max{0.0F};
    bool has_value = false;

    void add(Vec3 point) {
        if (!has_value) {
            min = point;
            max = point;
            has_value = true;
            return;
        }
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
};

[[nodiscard]] float finite_or_zero(float value) {
    return std::isfinite(value) ? value : 0.0F;
}

[[nodiscard]] Vec3 sanitized_center(Vec3 center) {
    return {
        finite_or_zero(center.x),
        finite_or_zero(center.y),
        finite_or_zero(center.z),
    };
}

[[nodiscard]] Vec3 sanitized_half_extent(Vec3 half_extent) {
    return {
        std::abs(finite_or_zero(half_extent.x)),
        std::abs(finite_or_zero(half_extent.y)),
        std::abs(finite_or_zero(half_extent.z)),
    };
}

[[nodiscard]] Vec3 component_axis(AxisIndex axis) {
    Vec3 result{0.0F};
    result[axis] = 1.0F;
    return result;
}

void append_box(GltfViewerLoadingCageMesh& mesh, BoundsAccumulator& bounds, Vec3 minimum,
                Vec3 maximum) {
    const std::array<Vec3, 6> normals{
        Vec3{0.0F, 0.0F, 1.0F}, Vec3{0.0F, 0.0F, -1.0F}, Vec3{-1.0F, 0.0F, 0.0F},
        Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 1.0F, 0.0F},  Vec3{0.0F, -1.0F, 0.0F},
    };
    const std::array<Vec3, 6> tangents{
        Vec3{1.0F, 0.0F, 0.0F},  Vec3{-1.0F, 0.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
        Vec3{0.0F, 0.0F, -1.0F}, Vec3{1.0F, 0.0F, 0.0F},  Vec3{1.0F, 0.0F, 0.0F},
    };
    const std::array<std::array<Vec3, 4>, 6> face_positions{
        std::array<Vec3, 4>{
            Vec3{minimum.x, minimum.y, maximum.z}, Vec3{maximum.x, minimum.y, maximum.z},
            Vec3{maximum.x, maximum.y, maximum.z}, Vec3{minimum.x, maximum.y, maximum.z}},
        std::array<Vec3, 4>{
            Vec3{maximum.x, minimum.y, minimum.z}, Vec3{minimum.x, minimum.y, minimum.z},
            Vec3{minimum.x, maximum.y, minimum.z}, Vec3{maximum.x, maximum.y, minimum.z}},
        std::array<Vec3, 4>{
            Vec3{minimum.x, minimum.y, minimum.z}, Vec3{minimum.x, minimum.y, maximum.z},
            Vec3{minimum.x, maximum.y, maximum.z}, Vec3{minimum.x, maximum.y, minimum.z}},
        std::array<Vec3, 4>{
            Vec3{maximum.x, minimum.y, maximum.z}, Vec3{maximum.x, minimum.y, minimum.z},
            Vec3{maximum.x, maximum.y, minimum.z}, Vec3{maximum.x, maximum.y, maximum.z}},
        std::array<Vec3, 4>{
            Vec3{minimum.x, maximum.y, maximum.z}, Vec3{maximum.x, maximum.y, maximum.z},
            Vec3{maximum.x, maximum.y, minimum.z}, Vec3{minimum.x, maximum.y, minimum.z}},
        std::array<Vec3, 4>{
            Vec3{minimum.x, minimum.y, minimum.z}, Vec3{maximum.x, minimum.y, minimum.z},
            Vec3{maximum.x, minimum.y, maximum.z}, Vec3{minimum.x, minimum.y, maximum.z}},
    };

    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.reserve(mesh.vertices.size() + 24U);
    mesh.indices.reserve(mesh.indices.size() + 36U);
    for (std::size_t face = 0; face < face_positions.size(); ++face) {
        for (std::size_t corner = 0; corner < face_positions[face].size(); ++corner) {
            const Vec3 position = face_positions[face][corner];
            mesh.vertices.push_back({
                .position = position,
                .normal = normals[face],
                .tangent = {tangents[face].x, tangents[face].y, tangents[face].z, 1.0F},
                .uv0 = {corner == 1U || corner == 2U ? 1.0F : 0.0F, corner >= 2U ? 1.0F : 0.0F},
            });
            bounds.add(position);
        }
        const std::uint32_t face_base = base + static_cast<std::uint32_t>(face * 4U);
        mesh.indices.insert(mesh.indices.end(), {
                                                    face_base + 0U,
                                                    face_base + 1U,
                                                    face_base + 2U,
                                                    face_base + 0U,
                                                    face_base + 2U,
                                                    face_base + 3U,
                                                });
    }
}

} // namespace

GltfViewerLoadingCageMesh make_gltf_viewer_loading_cage(cubey::asset::GltfBounds3D bounds) {
    const Vec3 center = sanitized_center(bounds.center);
    const Vec3 half_extent = sanitized_half_extent(bounds.half_extent);
    const float maximum_dimension = std::max({half_extent.x, half_extent.y, half_extent.z}) * 2.0F;
    constexpr float kThicknessFraction = 0.018F;
    constexpr float kMinimumThickness = 0.005F;
    const float thickness = std::max(maximum_dimension * kThicknessFraction, kMinimumThickness);
    const float half_thickness = thickness * 0.5F;

    GltfViewerLoadingCageMesh mesh;
    mesh.vertices.reserve(12U * 24U);
    mesh.indices.reserve(12U * 36U);
    BoundsAccumulator render_bounds;

    for (AxisIndex axis = 0; axis < 3; ++axis) {
        const AxisIndex first_cross_axis = (axis + 1) % 3;
        const AxisIndex second_cross_axis = (axis + 2) % 3;
        const Vec3 axis_vector = component_axis(axis);
        for (const float first_sign : {-1.0F, 1.0F}) {
            for (const float second_sign : {-1.0F, 1.0F}) {
                Vec3 segment_center = center;
                segment_center[first_cross_axis] += first_sign * half_extent[first_cross_axis];
                segment_center[second_cross_axis] += second_sign * half_extent[second_cross_axis];

                const float segment_half_extent = std::max(half_extent[axis], half_thickness);
                Vec3 minimum = segment_center - (axis_vector * segment_half_extent);
                Vec3 maximum = segment_center + (axis_vector * segment_half_extent);
                for (const AxisIndex cross_axis : {first_cross_axis, second_cross_axis}) {
                    minimum[cross_axis] -= half_thickness;
                    maximum[cross_axis] += half_thickness;
                }
                append_box(mesh, render_bounds, minimum, maximum);
            }
        }
    }

    mesh.render_bounds = {
        .center = (render_bounds.min + render_bounds.max) * 0.5F,
        .half_extent = (render_bounds.max - render_bounds.min) * 0.5F,
    };
    mesh.thickness = thickness;
    return mesh;
}

} // namespace cubey::projects::gltf_viewer
