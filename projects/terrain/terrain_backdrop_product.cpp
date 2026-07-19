#include "terrain_backdrop_product.h"

#include <cubey/core/jobs.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <thread>

namespace cubey::projects::terrain {
namespace {

constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] float logarithmic_radius(float inner, float outer, std::uint32_t index,
                                       std::uint32_t interval_count) {
    const float t = static_cast<float>(index) / static_cast<float>(interval_count);
    return inner * std::pow(outer / inner, t);
}

[[nodiscard]] std::vector<float> radial_samples(const TerrainBackdropProductRequest& request,
                                                TerrainBackdropDensityProfile density) {
    std::vector<float> radii;
    const bool continuous = request.center_mode == TerrainBackdropCenterMode::Continuous;
    radii.reserve((continuous ? static_cast<std::size_t>(density.center_radial_intervals) : 0U) +
                  static_cast<std::size_t>(density.hidden_radial_intervals) +
                  static_cast<std::size_t>(density.visible_radial_intervals) + 1U);
    if (continuous && request.center_sampling == TerrainBackdropCenterSampling::Uniform) {
        const std::uint32_t center_intervals =
            density.center_radial_intervals + density.hidden_radial_intervals;
        for (std::uint32_t index = 0U; index <= center_intervals; ++index) {
            radii.push_back(request.visible_inner_radius_m * static_cast<float>(index) /
                            static_cast<float>(center_intervals));
        }
    } else if (continuous &&
               request.center_sampling == TerrainBackdropCenterSampling::SeamMatched) {
        const std::uint32_t center_intervals =
            density.center_radial_intervals + density.hidden_radial_intervals;
        const float average_step =
            request.visible_inner_radius_m / static_cast<float>(center_intervals);
        const float outer_first_radius =
            logarithmic_radius(request.visible_inner_radius_m, request.outer_radius_m, 1U,
                               density.visible_radial_intervals);
        const float final_step =
            std::min(outer_first_radius - request.visible_inner_radius_m, average_step * 1.9F);
        const float first_step = 2.0F * average_step - final_step;
        const float step_delta =
            (final_step - first_step) / static_cast<float>(center_intervals - 1U);
        float radius = 0.0F;
        radii.push_back(radius);
        for (std::uint32_t index = 0U; index < center_intervals; ++index) {
            radius += first_step + step_delta * static_cast<float>(index);
            radii.push_back(index + 1U == center_intervals ? request.visible_inner_radius_m
                                                           : radius);
        }
    } else if (continuous) {
        for (std::uint32_t index = 0U; index <= density.center_radial_intervals; ++index) {
            radii.push_back(request.consumer_radius_m * static_cast<float>(index) /
                            static_cast<float>(density.center_radial_intervals));
        }
    }
    if (!continuous || request.center_sampling == TerrainBackdropCenterSampling::SplitLinearLog) {
        const std::uint32_t hidden_begin = continuous ? 1U : 0U;
        for (std::uint32_t index = hidden_begin; index <= density.hidden_radial_intervals;
             ++index) {
            radii.push_back(logarithmic_radius(request.consumer_radius_m,
                                               request.visible_inner_radius_m, index,
                                               density.hidden_radial_intervals));
        }
    }
    for (std::uint32_t index = 1U; index <= density.visible_radial_intervals; ++index) {
        radii.push_back(logarithmic_radius(request.visible_inner_radius_m, request.outer_radius_m,
                                           index, density.visible_radial_intervals));
    }
    return radii;
}

void validate_request(const TerrainBackdropProductRequest& request,
                      TerrainBackdropDensityProfile density,
                      const TerrainHeightSourceMetadata& source) {
    validate_terrain_height_source_metadata(source);
    if (!std::isfinite(request.source_focus_xz.x) || !std::isfinite(request.source_focus_xz.y) ||
        !std::isfinite(request.consumer_radius_m) ||
        !std::isfinite(request.visible_inner_radius_m) || !std::isfinite(request.outer_radius_m) ||
        !std::isfinite(request.vertical_scale) || !std::isfinite(request.vertical_offset_m) ||
        request.consumer_radius_m <= 0.0F ||
        request.visible_inner_radius_m <= request.consumer_radius_m ||
        request.outer_radius_m <= request.visible_inner_radius_m ||
        request.vertical_scale <= 0.0F || density.angular_intervals == 0U ||
        density.center_radial_intervals == 0U || density.hidden_radial_intervals == 0U ||
        density.visible_radial_intervals == 0U || density.sector_count == 0U ||
        density.angular_intervals % density.sector_count != 0U ||
        (request.center_sampling != TerrainBackdropCenterSampling::SplitLinearLog &&
         request.center_sampling != TerrainBackdropCenterSampling::Uniform &&
         request.center_sampling != TerrainBackdropCenterSampling::SeamMatched) ||
        request.render_stride > density.angular_intervals ||
        request.render_stride > density.visible_radial_intervals) {
        throw std::runtime_error("invalid terrain backdrop product request");
    }
}

[[nodiscard]] std::size_t field_index(std::uint32_t angular_index, std::uint32_t radial_index,
                                      std::uint32_t radial_count) {
    return static_cast<std::size_t>(angular_index) * radial_count + radial_index;
}

[[nodiscard]] float radial_footprint(const std::vector<float>& radii, std::uint32_t index) {
    const float inward = index == 0U ? radii[1U] - radii[0U] : radii[index] - radii[index - 1U];
    const float outward = index + 1U == radii.size() ? inward : radii[index + 1U] - radii[index];
    return std::max(inward, outward);
}

[[nodiscard]] cubey::math::Vec3 position_at(const std::vector<float>& heights,
                                            const std::vector<float>& radii,
                                            std::uint32_t angular_index, std::uint32_t radial_index,
                                            TerrainBackdropDensityProfile density) {
    const std::uint32_t wrapped_angle = angular_index % density.angular_intervals;
    const float angle =
        static_cast<float>(wrapped_angle) * kTwoPi / static_cast<float>(density.angular_intervals);
    const float radius = radii[radial_index];
    return {
        std::sin(angle) * radius,
        heights[field_index(wrapped_angle, radial_index, static_cast<std::uint32_t>(radii.size()))],
        -std::cos(angle) * radius};
}

[[nodiscard]] cubey::math::Vec3 subtract(cubey::math::Vec3 lhs, cubey::math::Vec3 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] cubey::math::Vec3 cross(cubey::math::Vec3 lhs, cubey::math::Vec3 rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

[[nodiscard]] cubey::math::Vec3 normalized(cubey::math::Vec3 value) {
    const float magnitude = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (magnitude <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

[[nodiscard]] cubey::math::Vec3 normal_at(const std::vector<float>& heights,
                                          const std::vector<float>& radii,
                                          std::uint32_t angular_index, std::uint32_t radial_index,
                                          TerrainBackdropDensityProfile density) {
    const std::uint32_t previous_angle =
        (angular_index + density.angular_intervals - 1U) % density.angular_intervals;
    const std::uint32_t next_angle = (angular_index + 1U) % density.angular_intervals;
    const std::uint32_t previous_radius = radial_index == 0U ? 0U : radial_index - 1U;
    const std::uint32_t next_radius =
        std::min(radial_index + 1U, static_cast<std::uint32_t>(radii.size() - 1U));
    const cubey::math::Vec3 angular_tangent =
        subtract(position_at(heights, radii, next_angle, radial_index, density),
                 position_at(heights, radii, previous_angle, radial_index, density));
    const cubey::math::Vec3 radial_tangent =
        subtract(position_at(heights, radii, angular_index, next_radius, density),
                 position_at(heights, radii, angular_index, previous_radius, density));
    return normalized(cross(angular_tangent, radial_tangent));
}

[[nodiscard]] cubey::math::Vec3
material_channels(const TerrainBackdropProductRequest& request, const std::vector<float>& heights,
                  const std::vector<float>& radii, std::uint32_t angular_index,
                  std::uint32_t radial_index, TerrainBackdropDensityProfile density,
                  cubey::math::Vec3 normal, const TerrainHeightSourceMetadata& source) {
    const std::uint32_t previous_angle =
        (angular_index + density.angular_intervals - 1U) % density.angular_intervals;
    const std::uint32_t next_angle = (angular_index + 1U) % density.angular_intervals;
    const std::uint32_t previous_radius = radial_index == 0U ? 0U : radial_index - 1U;
    const std::uint32_t next_radius =
        std::min(radial_index + 1U, static_cast<std::uint32_t>(radii.size() - 1U));
    const std::uint32_t radial_count = static_cast<std::uint32_t>(radii.size());
    const float height = heights[field_index(angular_index, radial_index, radial_count)];
    const float neighbor_mean =
        0.25F * (heights[field_index(previous_angle, radial_index, radial_count)] +
                 heights[field_index(next_angle, radial_index, radial_count)] +
                 heights[field_index(angular_index, previous_radius, radial_count)] +
                 heights[field_index(angular_index, next_radius, radial_count)]);
    const float source_height = (height - request.vertical_offset_m) / request.vertical_scale;
    const float normalized_height = std::clamp(
        (source_height - source.base_height_m) / std::max(source.relief_scale_m, 1.0F), 0.0F, 1.0F);
    const float slope = 1.0F - std::clamp(normal.y, 0.0F, 1.0F);
    const float mountain_factor = smoothstep(1'300.0F, 2'800.0F, source.relief_scale_m);
    const float exposed_rock = smoothstep(0.17F, 0.54F, slope);
    const float alpine_rock = mountain_factor * smoothstep(0.42F, 0.72F, normalized_height) *
                              smoothstep(0.035F, 0.30F, slope);
    float snow = mountain_factor * smoothstep(0.25F, 0.39F, normalized_height) *
                 smoothstep(0.30F, 0.82F, normal.y);
    snow = std::clamp(snow, 0.0F, 1.0F);
    const float rock = std::clamp(std::max(exposed_rock, alpine_rock) * (1.0F - snow), 0.0F, 1.0F);
    const float concavity_m = neighbor_mean - height;
    const float ambient_visibility = 1.0F - 0.35F * smoothstep(20.0F, 240.0F, concavity_m);
    return {rock, snow, ambient_visibility};
}

void include(TerrainBackdropSectorBounds& bounds, cubey::math::Vec3 position) {
    bounds.minimum.x = std::min(bounds.minimum.x, position.x);
    bounds.minimum.y = std::min(bounds.minimum.y, position.y);
    bounds.minimum.z = std::min(bounds.minimum.z, position.z);
    bounds.maximum.x = std::max(bounds.maximum.x, position.x);
    bounds.maximum.y = std::max(bounds.maximum.y, position.y);
    bounds.maximum.z = std::max(bounds.maximum.z, position.z);
}

[[nodiscard]] TerrainBackdropSectorBounds finalize_bounds(TerrainBackdropSectorBounds bounds) {
    bounds.center = {(bounds.minimum.x + bounds.maximum.x) * 0.5F,
                     (bounds.minimum.y + bounds.maximum.y) * 0.5F,
                     (bounds.minimum.z + bounds.maximum.z) * 0.5F};
    const cubey::math::Vec3 extent = subtract(bounds.maximum, bounds.center);
    bounds.radius_m = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
    return bounds;
}

[[nodiscard]] std::vector<std::uint32_t> strided_vertices(std::uint32_t first,
                                                          std::uint32_t last,
                                                          std::uint32_t stride) {
    std::vector<std::uint32_t> result;
    for (std::uint32_t value = first; value < last; value += stride) {
        result.push_back(value);
    }
    if (result.empty() || result.back() != last) {
        result.push_back(last);
    }
    return result;
}

[[nodiscard]] std::vector<std::uint32_t>
render_angular_vertices(TerrainBackdropDensityProfile density, std::uint32_t stride) {
    std::vector<std::uint32_t> result =
        strided_vertices(0U, density.angular_intervals, stride);
    const std::uint32_t intervals_per_sector =
        density.angular_intervals / density.sector_count;
    for (std::uint32_t boundary = intervals_per_sector;
         boundary < density.angular_intervals; boundary += intervals_per_sector) {
        result.push_back(boundary);
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

[[nodiscard]] TerrainBackdropSectorMesh make_center_mesh(
    const TerrainBackdropProductRequest& request, const std::vector<float>& heights,
    const std::vector<float>& radii, TerrainBackdropDensityProfile density,
    const TerrainHeightSourceMetadata& source, std::uint32_t radial_interval_count,
    std::span<const std::uint32_t> render_angles, std::uint32_t radial_render_stride) {
    TerrainBackdropSectorMesh mesh;
    mesh.begin_azimuth_radians = 0.0F;
    mesh.end_azimuth_radians = kTwoPi;
    const std::uint32_t angular_vertex_count = density.angular_intervals + 1U;
    mesh.vertices.reserve(1U + static_cast<std::size_t>(radial_interval_count) *
                                   angular_vertex_count);
    TerrainBackdropSectorBounds bounds{
        .minimum = {std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity()},
        .maximum = {-std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity()},
    };

    const cubey::math::Vec3 center_position = position_at(heights, radii, 0U, 0U, density);
    const cubey::math::Vec3 center_normal = normal_at(heights, radii, 0U, 0U, density);
    const cubey::math::Vec3 center_channels = material_channels(
        request, heights, radii, 0U, 0U, density, center_normal, source);
    mesh.vertices.push_back({
        .position = {center_position.x, center_position.y, center_position.z},
        .color = {center_channels.x, center_channels.y, center_channels.z},
        .normal = {center_normal.x, center_normal.y, center_normal.z},
    });
    include(bounds, center_position);
    for (std::uint32_t radial = 1U; radial <= radial_interval_count; ++radial) {
        for (std::uint32_t angular = 0U; angular <= density.angular_intervals; ++angular) {
            const std::uint32_t wrapped = angular % density.angular_intervals;
            const cubey::math::Vec3 position =
                position_at(heights, radii, wrapped, radial, density);
            const cubey::math::Vec3 normal = normal_at(heights, radii, wrapped, radial, density);
            const cubey::math::Vec3 channels = material_channels(
                request, heights, radii, wrapped, radial, density, normal, source);
            mesh.vertices.push_back({
                .position = {position.x, position.y, position.z},
                .color = {channels.x, channels.y, channels.z},
                .normal = {normal.x, normal.y, normal.z},
            });
            include(bounds, position);
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(density.angular_intervals) *
                         (1U + (radial_interval_count - 1U) * 6U));
    for (std::uint32_t angular = 0U; angular < density.angular_intervals; ++angular) {
        mesh.indices.insert(mesh.indices.end(), {0U, 1U + angular + 1U, 1U + angular});
    }
    for (std::uint32_t radial = 1U; radial < radial_interval_count; ++radial) {
        const std::uint32_t inner = 1U + (radial - 1U) * angular_vertex_count;
        const std::uint32_t outer = inner + angular_vertex_count;
        for (std::uint32_t angular = 0U; angular < density.angular_intervals; ++angular) {
            const std::uint32_t inner0 = inner + angular;
            const std::uint32_t inner1 = inner0 + 1U;
            const std::uint32_t outer0 = outer + angular;
            const std::uint32_t outer1 = outer0 + 1U;
            mesh.indices.insert(mesh.indices.end(),
                                {inner0, inner1, outer0, inner1, outer1, outer0});
        }
    }

    const std::vector<std::uint32_t> render_radii =
        strided_vertices(radial_render_stride, radial_interval_count, radial_render_stride);
    mesh.render_indices.reserve((render_angles.size() - 1U) *
                                (1U + (render_radii.size() - 1U) * 6U));
    for (std::size_t angular = 0U; angular + 1U < render_angles.size(); ++angular) {
        mesh.render_indices.insert(mesh.render_indices.end(),
                                   {0U, 1U + render_angles[angular + 1U],
                                    1U + render_angles[angular]});
    }
    for (std::size_t radial = 0U; radial + 1U < render_radii.size(); ++radial) {
        const std::uint32_t inner = 1U + (render_radii[radial] - 1U) * angular_vertex_count;
        const std::uint32_t outer =
            1U + (render_radii[radial + 1U] - 1U) * angular_vertex_count;
        for (std::size_t angular = 0U; angular + 1U < render_angles.size(); ++angular) {
            const std::uint32_t inner0 = inner + render_angles[angular];
            const std::uint32_t inner1 = inner + render_angles[angular + 1U];
            const std::uint32_t outer0 = outer + render_angles[angular];
            const std::uint32_t outer1 = outer + render_angles[angular + 1U];
            mesh.render_indices.insert(mesh.render_indices.end(),
                                       {inner0, inner1, outer0, inner1, outer1, outer0});
        }
    }
    mesh.bounds = finalize_bounds(bounds);
    return mesh;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) {
    for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
    }
}

void hash_float(std::uint64_t& hash, float value) {
    hash_u32(hash, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint64_t content_hash(const TerrainBackdropProduct& product) {
    std::uint64_t hash = kFnvOffset;
    const auto hash_mesh = [&hash](const TerrainBackdropSectorMesh& mesh) {
        for (const cubey::render::VertexPositionColorNormal& vertex : mesh.vertices) {
            for (const float value : vertex.position) {
                hash_float(hash, value);
            }
            for (const float value : vertex.color) {
                hash_float(hash, value);
            }
            for (const float value : vertex.normal) {
                hash_float(hash, value);
            }
        }
        for (const std::uint32_t index : mesh.indices) {
            hash_u32(hash, index);
        }
        for (const std::uint32_t index : mesh.render_indices) {
            hash_u32(hash, index);
        }
    };
    if (product.center.has_value()) {
        hash_mesh(product.center.value());
    }
    for (const TerrainBackdropSectorMesh& sector : product.sectors) {
        hash_mesh(sector);
    }
    return hash;
}

} // namespace

TerrainBackdropDensityProfile
terrain_backdrop_density_profile(TerrainBackdropMeshDensity density) noexcept {
    switch (density) {
    case TerrainBackdropMeshDensity::Low:
        return {1'024U, 16U, 32U, 256U, 32U};
    case TerrainBackdropMeshDensity::Medium:
        return {2'048U, 24U, 48U, 512U, 32U};
    case TerrainBackdropMeshDensity::High:
        return {3'072U, 32U, 64U, 768U, 48U};
    }
    return {3'072U, 32U, 64U, 768U, 48U};
}

cubey::render::MeshConfig TerrainBackdropSectorMesh::mesh_config() const {
    return cubey::render::indexed_mesh_config(
        std::span<const cubey::render::VertexPositionColorNormal>(vertices),
        std::span<const std::uint32_t>(render_indices));
}

std::uint32_t TerrainBackdropSectorMesh::triangle_count() const noexcept {
    return static_cast<std::uint32_t>(render_indices.size() / 3U);
}

TerrainBackdropProduct make_terrain_backdrop_product(const TerrainBackdropProductRequest& request,
                                                     const TerrainHeightSource& source) {
    const TerrainBackdropDensityProfile density = terrain_backdrop_density_profile(request.density);
    const TerrainHeightSourceMetadata source_metadata = source.metadata();
    validate_request(request, density, source_metadata);
    const std::vector<float> radii = radial_samples(request, density);
    const std::uint32_t radial_count = static_cast<std::uint32_t>(radii.size());
    const std::size_t sample_count =
        static_cast<std::size_t>(density.angular_intervals) * radial_count;
    std::vector<float> heights(sample_count);

    const std::uint32_t rows_per_job = 16U;
    const std::uint32_t job_count = (density.angular_intervals + rows_per_job - 1U) / rows_per_job;
    cubey::jobs::JobSystem jobs(std::min<std::size_t>(
        std::max(1U, std::thread::hardware_concurrency()), static_cast<std::size_t>(job_count)));
    std::vector<cubey::jobs::JobHandle<void>> handles;
    handles.reserve(job_count);
    for (std::uint32_t job = 0U; job < job_count; ++job) {
        const std::uint32_t angular_begin = job * rows_per_job;
        const std::uint32_t angular_end =
            std::min(angular_begin + rows_per_job, density.angular_intervals);
        handles.push_back(jobs.submit([&, angular_begin, angular_end] {
            for (std::uint32_t angular = angular_begin; angular < angular_end; ++angular) {
                const float angle = static_cast<float>(angular) * kTwoPi /
                                    static_cast<float>(density.angular_intervals);
                const cubey::math::Vec2 direction{std::sin(angle), -std::cos(angle)};
                for (std::uint32_t radial = 0U; radial < radial_count; ++radial) {
                    const float footprint = std::max(
                        radial_footprint(radii, radial),
                        radii[radial] * kTwoPi / static_cast<float>(density.angular_intervals));
                    const cubey::math::Vec2 source_xz =
                        request.source_focus_xz + direction * radii[radial];
                    heights[field_index(angular, radial, radial_count)] =
                        source.sample_height({.world_xz = source_xz, .footprint_m = footprint}) *
                            request.vertical_scale +
                        request.vertical_offset_m;
                }
            }
        }));
    }
    for (auto& handle : handles) {
        handle.get();
    }
    jobs.shutdown();

    TerrainBackdropProduct product;
    product.request = request;
    product.source = source_metadata;
    product.sectors.resize(density.sector_count);
    const std::uint32_t angular_intervals_per_sector =
        density.angular_intervals / density.sector_count;
    const std::uint32_t center_radial_intervals =
        request.center_mode == TerrainBackdropCenterMode::Continuous
            ? density.center_radial_intervals + density.hidden_radial_intervals
            : 0U;
    const std::uint32_t visible_radial_begin =
        density.hidden_radial_intervals +
        (request.center_mode == TerrainBackdropCenterMode::Continuous
             ? density.center_radial_intervals
             : 0U);
    const std::uint32_t visible_radial_rows = density.visible_radial_intervals + 1U;
    const std::uint32_t angular_render_stride =
        request.render_stride != 0U ? request.render_stride
        : request.density == TerrainBackdropMeshDensity::High
            ? 3U
            : (request.density == TerrainBackdropMeshDensity::Medium ? 2U : 1U);
    const std::uint32_t radial_render_stride = angular_render_stride;
    const std::vector<std::uint32_t> global_render_angles =
        render_angular_vertices(density, angular_render_stride);

    if (request.center_mode == TerrainBackdropCenterMode::Continuous) {
        product.center = make_center_mesh(request, heights, radii, density, source_metadata,
                                          center_radial_intervals, global_render_angles,
                                          radial_render_stride);
    }

    float minimum_height = std::numeric_limits<float>::infinity();
    float maximum_height = -std::numeric_limits<float>::infinity();
    for (const float height : heights) {
        minimum_height = std::min(minimum_height, height);
        maximum_height = std::max(maximum_height, height);
    }

    for (std::uint32_t sector_index = 0U; sector_index < density.sector_count; ++sector_index) {
        TerrainBackdropSectorMesh& sector = product.sectors[sector_index];
        const std::uint32_t angular_begin = sector_index * angular_intervals_per_sector;
        const std::uint32_t angular_vertex_count = angular_intervals_per_sector + 1U;
        sector.begin_azimuth_radians = static_cast<float>(angular_begin) * kTwoPi /
                                       static_cast<float>(density.angular_intervals);
        sector.end_azimuth_radians =
            static_cast<float>(angular_begin + angular_intervals_per_sector) * kTwoPi /
            static_cast<float>(density.angular_intervals);
        sector.vertices.reserve(static_cast<std::size_t>(visible_radial_rows) *
                                angular_vertex_count);
        sector.indices.reserve(static_cast<std::size_t>(density.visible_radial_intervals) *
                               angular_intervals_per_sector * 6U);
        TerrainBackdropSectorBounds bounds{
            .minimum = {std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity()},
            .maximum = {-std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity()},
        };
        for (std::uint32_t radial_row = 0U; radial_row < visible_radial_rows; ++radial_row) {
            const std::uint32_t radial = visible_radial_begin + radial_row;
            for (std::uint32_t local_angular = 0U; local_angular < angular_vertex_count;
                 ++local_angular) {
                const std::uint32_t angular =
                    (angular_begin + local_angular) % density.angular_intervals;
                const cubey::math::Vec3 position =
                    position_at(heights, radii, angular, radial, density);
                const cubey::math::Vec3 normal =
                    normal_at(heights, radii, angular, radial, density);
                const cubey::math::Vec3 channels = material_channels(
                    request, heights, radii, angular, radial, density, normal, source_metadata);
                sector.vertices.push_back({
                    .position = {position.x, position.y, position.z},
                    .color = {channels.x, channels.y, channels.z},
                    .normal = {normal.x, normal.y, normal.z},
                });
                include(bounds, position);
            }
        }
        for (std::uint32_t radial = 0U; radial < density.visible_radial_intervals; ++radial) {
            for (std::uint32_t angular = 0U; angular < angular_intervals_per_sector; ++angular) {
                const std::uint32_t inner0 = radial * angular_vertex_count + angular;
                const std::uint32_t inner1 = inner0 + 1U;
                const std::uint32_t outer0 = inner0 + angular_vertex_count;
                const std::uint32_t outer1 = outer0 + 1U;
                sector.indices.insert(sector.indices.end(),
                                      {inner0, inner1, outer0, inner1, outer1, outer0});
            }
        }
        std::vector<std::uint32_t> render_angular_vertices;
        const std::uint32_t angular_end = angular_begin + angular_intervals_per_sector;
        for (const std::uint32_t angular : global_render_angles) {
            if (angular >= angular_begin && angular <= angular_end) {
                render_angular_vertices.push_back(angular - angular_begin);
            }
        }
        std::vector<std::uint32_t> render_radial_vertices;
        for (std::uint32_t radial = 0U; radial < density.visible_radial_intervals;
             radial += radial_render_stride) {
            render_radial_vertices.push_back(radial);
        }
        if (render_radial_vertices.back() != density.visible_radial_intervals) {
            render_radial_vertices.push_back(density.visible_radial_intervals);
        }
        sector.render_indices.reserve((render_angular_vertices.size() - 1U) *
                                      (render_radial_vertices.size() - 1U) * 6U);
        for (std::size_t radial = 0U; radial + 1U < render_radial_vertices.size(); ++radial) {
            for (std::size_t angular = 0U; angular + 1U < render_angular_vertices.size();
                 ++angular) {
                const std::uint32_t inner0 = render_radial_vertices[radial] * angular_vertex_count +
                                             render_angular_vertices[angular];
                const std::uint32_t inner1 = render_radial_vertices[radial] * angular_vertex_count +
                                             render_angular_vertices[angular + 1U];
                const std::uint32_t outer0 =
                    render_radial_vertices[radial + 1U] * angular_vertex_count +
                    render_angular_vertices[angular];
                const std::uint32_t outer1 =
                    render_radial_vertices[radial + 1U] * angular_vertex_count +
                    render_angular_vertices[angular + 1U];
                sector.render_indices.insert(sector.render_indices.end(),
                                             {inner0, inner1, outer0, inner1, outer1, outer0});
            }
        }
        sector.bounds = finalize_bounds(bounds);
    }

    float maximum_boundary_delta = 0.0F;
    for (std::uint32_t sector_index = 0U; sector_index < density.sector_count; ++sector_index) {
        const TerrainBackdropSectorMesh& current = product.sectors[sector_index];
        const TerrainBackdropSectorMesh& next =
            product.sectors[(sector_index + 1U) % density.sector_count];
        for (std::uint32_t radial = 0U; radial < visible_radial_rows; ++radial) {
            const auto& lhs = current.vertices[static_cast<std::size_t>(radial) *
                                                   (angular_intervals_per_sector + 1U) +
                                               angular_intervals_per_sector];
            const auto& rhs = next.vertices[static_cast<std::size_t>(radial) *
                                            (angular_intervals_per_sector + 1U)];
            for (std::size_t component = 0U; component < 3U; ++component) {
                maximum_boundary_delta =
                    std::max(maximum_boundary_delta,
                             std::abs(lhs.position[component] - rhs.position[component]));
                maximum_boundary_delta =
                    std::max(maximum_boundary_delta,
                             std::abs(lhs.normal[component] - rhs.normal[component]));
                maximum_boundary_delta = std::max(
                    maximum_boundary_delta, std::abs(lhs.color[component] - rhs.color[component]));
            }
        }
    }

    if (product.center.has_value()) {
        const TerrainBackdropSectorMesh& center = product.center.value();
        const std::uint32_t center_outer_begin =
            1U + (center_radial_intervals - 1U) * (density.angular_intervals + 1U);
        for (std::uint32_t sector_index = 0U; sector_index < density.sector_count; ++sector_index) {
            const TerrainBackdropSectorMesh& sector = product.sectors[sector_index];
            const std::uint32_t angular_begin = sector_index * angular_intervals_per_sector;
            for (std::uint32_t local = 0U; local <= angular_intervals_per_sector; ++local) {
                const auto& lhs = center.vertices[center_outer_begin + angular_begin + local];
                const auto& rhs = sector.vertices[local];
                for (std::size_t component = 0U; component < 3U; ++component) {
                    maximum_boundary_delta =
                        std::max(maximum_boundary_delta,
                                 std::abs(lhs.position[component] - rhs.position[component]));
                    maximum_boundary_delta =
                        std::max(maximum_boundary_delta,
                                 std::abs(lhs.normal[component] - rhs.normal[component]));
                    maximum_boundary_delta =
                        std::max(maximum_boundary_delta,
                                 std::abs(lhs.color[component] - rhs.color[component]));
                }
            }
        }
    }

    const std::uint64_t center_vertex_count =
        product.center.has_value() ? product.center->vertices.size() : 0U;
    const std::uint64_t center_index_count =
        product.center.has_value() ? product.center->indices.size() : 0U;
    const std::uint64_t center_triangle_count = center_index_count / 3U;
    const std::uint64_t center_render_triangle_count =
        product.center.has_value() ? product.center->triangle_count() : 0U;
    product.diagnostics = {
        .density = density,
        .source_sample_count = sample_count,
        .center_vertex_count = center_vertex_count,
        .center_index_count = center_index_count,
        .center_triangle_count = center_triangle_count,
        .center_render_triangle_count = center_render_triangle_count,
        .visible_vertex_count = center_vertex_count +
                                static_cast<std::uint64_t>(density.sector_count) *
                                    visible_radial_rows * (angular_intervals_per_sector + 1U),
        .visible_index_count = center_index_count +
                               static_cast<std::uint64_t>(density.angular_intervals) *
                                   density.visible_radial_intervals * 6U,
        .visible_triangle_count = center_triangle_count +
                                  static_cast<std::uint64_t>(density.angular_intervals) *
                                      density.visible_radial_intervals * 2U,
        .minimum_height_m = minimum_height,
        .maximum_height_m = maximum_height,
        .maximum_sector_boundary_delta_m = maximum_boundary_delta,
    };
    product.diagnostics.render_triangle_count = center_render_triangle_count;
    for (const TerrainBackdropSectorMesh& sector : product.sectors) {
        product.diagnostics.render_triangle_count += sector.triangle_count();
    }
    product.diagnostics.content_hash = content_hash(product);
    return product;
}

TerrainBackdropProduct make_terrain_backdrop_product(const TerrainBackdropProductRequest& request,
                                                     const TerrainSourceParameters& source,
                                                     std::uint64_t seed) {
    const ParameterTerrainHeightSource adapter(source, seed);
    return make_terrain_backdrop_product(request, adapter);
}

} // namespace cubey::projects::terrain
