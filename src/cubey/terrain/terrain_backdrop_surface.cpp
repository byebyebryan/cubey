#include <cubey/terrain/terrain_backdrop_surface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::terrain {
namespace {

struct SurfacePoint {
    float x = 0.0F;
    float z = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] SurfacePoint surface_point(const TerrainBackdropVertex& vertex) {
    return {
        .x = vertex.position[0],
        .z = vertex.position[2],
        .y = vertex.position[1],
    };
}

[[nodiscard]] float squared_radius(SurfacePoint point) {
    return point.x * point.x + point.z * point.z;
}

[[nodiscard]] float cross_2d(float ax, float az, float bx, float bz) {
    return ax * bz - az * bx;
}

[[nodiscard]] bool point_in_triangle(float x, float z,
                                     const std::array<SurfacePoint, 3>& triangle) {
    constexpr float kEpsilon = 1.0e-4F;
    const float first = cross_2d(triangle[1].x - triangle[0].x, triangle[1].z - triangle[0].z,
                                 x - triangle[0].x, z - triangle[0].z);
    const float second = cross_2d(triangle[2].x - triangle[1].x, triangle[2].z - triangle[1].z,
                                  x - triangle[1].x, z - triangle[1].z);
    const float third = cross_2d(triangle[0].x - triangle[2].x, triangle[0].z - triangle[2].z,
                                 x - triangle[2].x, z - triangle[2].z);
    const bool has_negative = first < -kEpsilon || second < -kEpsilon || third < -kEpsilon;
    const bool has_positive = first > kEpsilon || second > kEpsilon || third > kEpsilon;
    return !(has_negative && has_positive);
}

void include_edge_circle_intersections(float& maximum, SurfacePoint begin, SurfacePoint end,
                                       float radius_m) {
    const float dx = end.x - begin.x;
    const float dz = end.z - begin.z;
    const float a = dx * dx + dz * dz;
    if (a <= std::numeric_limits<float>::epsilon()) {
        return;
    }
    const float b = 2.0F * (begin.x * dx + begin.z * dz);
    const float c = squared_radius(begin) - radius_m * radius_m;
    const float discriminant = b * b - 4.0F * a * c;
    if (discriminant < 0.0F) {
        return;
    }
    const float root = std::sqrt(std::max(discriminant, 0.0F));
    for (const float t : {(-b - root) / (2.0F * a), (-b + root) / (2.0F * a)}) {
        if (t >= 0.0F && t <= 1.0F) {
            maximum = std::max(maximum, std::lerp(begin.y, end.y, t));
        }
    }
}

void include_triangle_disk_maximum(float& maximum, const std::array<SurfacePoint, 3>& triangle,
                                   float radius_m) {
    const float radius_squared = radius_m * radius_m;
    for (const SurfacePoint point : triangle) {
        if (squared_radius(point) <= radius_squared) {
            maximum = std::max(maximum, point.y);
        }
    }
    for (std::size_t edge = 0U; edge < triangle.size(); ++edge) {
        include_edge_circle_intersections(maximum, triangle[edge],
                                          triangle[(edge + 1U) % triangle.size()], radius_m);
    }

    const float dx1 = triangle[1].x - triangle[0].x;
    const float dz1 = triangle[1].z - triangle[0].z;
    const float dx2 = triangle[2].x - triangle[0].x;
    const float dz2 = triangle[2].z - triangle[0].z;
    const float dy1 = triangle[1].y - triangle[0].y;
    const float dy2 = triangle[2].y - triangle[0].y;
    const float determinant = cross_2d(dx1, dz1, dx2, dz2);
    if (std::abs(determinant) <= std::numeric_limits<float>::epsilon()) {
        return;
    }
    const float gradient_x = (dy1 * dz2 - dy2 * dz1) / determinant;
    const float gradient_z = (dx1 * dy2 - dx2 * dy1) / determinant;
    const float gradient_length = std::sqrt(gradient_x * gradient_x + gradient_z * gradient_z);
    if (gradient_length <= std::numeric_limits<float>::epsilon()) {
        return;
    }
    const float candidate_x = radius_m * gradient_x / gradient_length;
    const float candidate_z = radius_m * gradient_z / gradient_length;
    if (point_in_triangle(candidate_x, candidate_z, triangle)) {
        maximum = std::max(maximum, triangle[0].y + gradient_x * (candidate_x - triangle[0].x) +
                                        gradient_z * (candidate_z - triangle[0].z));
    }
}

void include_mesh_disk_maximum(float& maximum, const TerrainBackdropSectorMesh& mesh,
                               float radius_m) {
    for (std::size_t index = 0U; index + 2U < mesh.indices.size(); index += 3U) {
        const std::array<SurfacePoint, 3> triangle{
            surface_point(mesh.vertices.at(mesh.indices[index])),
            surface_point(mesh.vertices.at(mesh.indices[index + 1U])),
            surface_point(mesh.vertices.at(mesh.indices[index + 2U])),
        };
        include_triangle_disk_maximum(maximum, triangle, radius_m);
    }
}

} // namespace

TerrainBackdropSurfaceEnvelope
terrain_backdrop_surface_envelope(const TerrainBackdropProduct& product, float footprint_radius_m) {
    if (!std::isfinite(footprint_radius_m) || footprint_radius_m < 0.0F ||
        !product.center.has_value() || product.center->vertices.empty()) {
        throw std::runtime_error("invalid terrain backdrop surface envelope request");
    }

    const auto nominal_vertex = std::ranges::min_element(
        product.center->vertices, {}, [](const TerrainBackdropVertex& vertex) {
            return vertex.position[0] * vertex.position[0] +
                   vertex.position[2] * vertex.position[2];
        });
    const float nominal_height_m = nominal_vertex->position[1];
    float maximum_height_m = nominal_height_m;
    if (footprint_radius_m > 0.0F) {
        include_mesh_disk_maximum(maximum_height_m, product.center.value(), footprint_radius_m);
        for (const TerrainBackdropSectorMesh& sector : product.sectors) {
            include_mesh_disk_maximum(maximum_height_m, sector, footprint_radius_m);
        }
    }
    return {
        .nominal_local_height_m = nominal_height_m,
        .maximum_local_height_m = maximum_height_m,
        .footprint_radius_m = footprint_radius_m,
    };
}

} // namespace cubey::terrain
