#include "terrain_mesh.h"

#include <cubey/procedural/operators.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain {
namespace {

struct Rgb {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

enum class ViewKind {
    Surface,
    Scalar,
    FlowDirection,
};

struct ViewSelection {
    ViewKind kind = ViewKind::Surface;
    std::string field_name{};
};

[[nodiscard]] Rgb mix(Rgb a, Rgb b, float t) {
    const float amount = cubey::procedural::saturate(t);
    return {
        .r = cubey::procedural::lerp(a.r, b.r, amount),
        .g = cubey::procedural::lerp(a.g, b.g, amount),
        .b = cubey::procedural::lerp(a.b, b.b, amount),
    };
}

[[nodiscard]] std::string canonical_view_name(std::string_view name) {
    std::string result(name);
    std::replace(result.begin(), result.end(), '-', '_');
    return result;
}

[[nodiscard]] ViewSelection select_view(const TerrainPatchProduct& product,
                                        std::string_view debug_view) {
    const std::string name = canonical_view_name(debug_view);
    if (name.empty() || name == "surface" || name == "final" || name == "material") {
        return {};
    }
    if (name == "flow_direction") {
        return {.kind = ViewKind::FlowDirection};
    }
    if (name == "height") {
        return {.kind = ViewKind::Scalar, .field_name = std::string(kTerrainFieldHeightM)};
    }
    if (name == "source") {
        return {.kind = ViewKind::Scalar, .field_name = std::string(kTerrainFieldSourceHeightM)};
    }
    if (!product.fields.has_field(name)) {
        throw std::runtime_error("unknown terrain debug view: " + name);
    }
    return {.kind = ViewKind::Scalar, .field_name = name};
}

[[nodiscard]] float normalized(float value, float low, float high) {
    return high > low ? cubey::procedural::saturate((value - low) / (high - low)) : 0.5F;
}

[[nodiscard]] Rgb height_ramp(float value) {
    Rgb color = mix({0.09F, 0.20F, 0.16F}, {0.34F, 0.45F, 0.23F},
                    cubey::procedural::smoothstep(0.0F, 0.42F, value));
    color = mix(color, {0.46F, 0.41F, 0.34F}, cubey::procedural::smoothstep(0.32F, 0.72F, value));
    return mix(color, {0.82F, 0.84F, 0.81F}, cubey::procedural::smoothstep(0.68F, 0.96F, value));
}

[[nodiscard]] Rgb surface_color(const TerrainPatchProduct& product, std::uint32_t x,
                                std::uint32_t z,
                                const cubey::procedural::ScalarFieldStats& height_stats) {
    const cubey::procedural::ScalarField2D& height = product.fields.field(kTerrainFieldHeightM);
    const cubey::procedural::ScalarField2D& slope = product.fields.field(kTerrainFieldSlope);
    const cubey::procedural::ScalarField2D& support =
        product.fields.field(kTerrainFieldMountainSupport);
    const float h = normalized(height.at(x, z), height_stats.min, height_stats.max);
    const float steep = cubey::procedural::smoothstep(0.18F, 0.82F, slope.at(x, z));
    const float mountain = cubey::procedural::saturate(support.at(x, z));
    Rgb color = height_ramp(h);
    color = mix(color, {0.38F, 0.37F, 0.36F}, steep * (0.42F + (mountain * 0.28F)));
    return mix(color, {0.88F, 0.89F, 0.87F},
               cubey::procedural::smoothstep(0.76F, 0.96F, h) * (1.0F - steep * 0.45F));
}

[[nodiscard]] bool signed_field(std::string_view name) {
    return name == kTerrainFieldCurvature || name == kTerrainFieldFlowDirectionX ||
           name == kTerrainFieldFlowDirectionZ;
}

[[nodiscard]] Rgb scalar_color(const TerrainPatchProduct& product, const ViewSelection& selection,
                               std::uint32_t x, std::uint32_t z,
                               const cubey::procedural::ScalarFieldStats& stats) {
    if (selection.kind == ViewKind::FlowDirection) {
        const float dx = product.fields.field(kTerrainFieldFlowDirectionX).at(x, z);
        const float dz = product.fields.field(kTerrainFieldFlowDirectionZ).at(x, z);
        const float magnitude = std::sqrt((dx * dx) + (dz * dz));
        return {0.5F + (dx * 0.5F), 0.5F + (dz * 0.5F), magnitude * 0.65F};
    }

    const cubey::procedural::ScalarField2D& field = product.fields.field(selection.field_name);
    const float value = field.at(x, z);
    if (selection.field_name == kTerrainFieldHeightM ||
        selection.field_name == kTerrainFieldSourceHeightM ||
        selection.field_name == kTerrainFieldRoutingSurfaceM) {
        return height_ramp(normalized(value, stats.min, stats.max));
    }
    if (selection.field_name == kTerrainFieldContributingAreaM2) {
        const float cell_area = field.desc().cell_size * field.desc().cell_size;
        const float high = std::log1p(std::max(stats.max / cell_area, 1.0F));
        const float t = high > 0.0F ? std::log1p(value / cell_area) / high : 0.0F;
        return mix({0.02F, 0.03F, 0.04F}, {0.18F, 0.72F, 0.92F}, t);
    }
    if (selection.field_name == kTerrainFieldDischargeProxy) {
        return mix({0.02F, 0.04F, 0.05F}, {0.12F, 0.75F, 0.94F}, value);
    }
    if (selection.field_name == kTerrainFieldStreamOrder) {
        return mix({0.08F, 0.10F, 0.12F}, {0.90F, 0.74F, 0.18F},
                   normalized(value, stats.min, stats.max));
    }
    if (selection.field_name == kTerrainFieldSinkMask) {
        return mix({0.02F, 0.02F, 0.02F}, {0.94F, 0.16F, 0.10F}, value);
    }
    if (selection.field_name == kTerrainFieldFlowBoundaryMask) {
        return mix({0.02F, 0.02F, 0.02F}, {0.96F, 0.72F, 0.08F}, value);
    }
    if (signed_field(selection.field_name)) {
        const float extent = std::max(std::abs(stats.min), std::abs(stats.max));
        const float t = extent > 0.0F ? std::clamp(value / extent, -1.0F, 1.0F) : 0.0F;
        return t < 0.0F ? mix({0.84F, 0.86F, 0.88F}, {0.08F, 0.32F, 0.84F}, -t)
                        : mix({0.84F, 0.86F, 0.88F}, {0.90F, 0.18F, 0.08F}, t);
    }
    const float t = selection.field_name.ends_with("_mask")
                        ? cubey::procedural::saturate(value)
                        : normalized(value, stats.min, stats.max);
    return mix({0.04F, 0.05F, 0.06F}, {0.88F, 0.90F, 0.92F}, t);
}

[[nodiscard]] std::array<float, 3> terrain_normal(const cubey::procedural::ScalarField2D& height,
                                                  std::uint32_t x, std::uint32_t z,
                                                  float vertical_scale) {
    const std::uint32_t x0 = x == 0U ? x : x - 1U;
    const std::uint32_t x1 = std::min(x + 1U, height.desc().width - 1U);
    const std::uint32_t z0 = z == 0U ? z : z - 1U;
    const std::uint32_t z1 = std::min(z + 1U, height.desc().height - 1U);
    const float dx = std::max(static_cast<float>(x1 - x0) * height.desc().cell_size, 1.0F);
    const float dz = std::max(static_cast<float>(z1 - z0) * height.desc().cell_size, 1.0F);
    const float dhdx = (height.at(x1, z) - height.at(x0, z)) * vertical_scale / dx;
    const float dhdz = (height.at(x, z1) - height.at(x, z0)) * vertical_scale / dz;
    const float length = std::sqrt((dhdx * dhdx) + 1.0F + (dhdz * dhdz));
    return {-dhdx / length, 1.0F / length, -dhdz / length};
}

} // namespace

cubey::render::MeshConfig TerrainMeshData::mesh_config() const {
    return cubey::render::indexed_mesh_config(
        std::span<const cubey::render::VertexPositionColorNormal>(vertices.data(), vertices.size()),
        std::span<const std::uint32_t>(indices.data(), indices.size()));
}

TerrainMeshData make_terrain_mesh(const TerrainPatchProduct& product, std::string_view debug_view,
                                  float vertical_scale) {
    if (!std::isfinite(vertical_scale) || vertical_scale <= 0.0F) {
        throw std::runtime_error("terrain mesh vertical scale must be finite and positive");
    }
    const ViewSelection selection = select_view(product, debug_view);
    const cubey::procedural::ScalarField2D& height = product.fields.field(kTerrainFieldHeightM);
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const cubey::procedural::ScalarFieldStats height_stats = height.summarize();
    const cubey::procedural::ScalarFieldStats selected_stats =
        selection.kind == ViewKind::Scalar ? product.fields.summarize_field(selection.field_name)
                                           : cubey::procedural::ScalarFieldStats{};
    TerrainMeshData mesh;
    mesh.vertices.reserve(height.sample_count());
    mesh.indices.reserve(static_cast<std::size_t>(desc.width - 1U) * (desc.height - 1U) * 6U);

    for (std::uint32_t z = 0; z < desc.height; ++z) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const Rgb color = selection.kind == ViewKind::Surface
                                  ? surface_color(product, x, z, height_stats)
                                  : scalar_color(product, selection, x, z, selected_stats);
            mesh.vertices.push_back({
                .position =
                    {
                        cubey::procedural::grid_sample_x(desc, x),
                        height.at(x, z) * vertical_scale,
                        cubey::procedural::grid_sample_y(desc, z),
                    },
                .color = {color.r, color.g, color.b},
                .normal = terrain_normal(height, x, z, vertical_scale),
            });
        }
    }

    for (std::uint32_t z = 0; z + 1U < desc.height; ++z) {
        for (std::uint32_t x = 0; x + 1U < desc.width; ++x) {
            const auto i00 = static_cast<std::uint32_t>(height.index(x, z));
            const auto i10 = static_cast<std::uint32_t>(height.index(x + 1U, z));
            const auto i01 = static_cast<std::uint32_t>(height.index(x, z + 1U));
            const auto i11 = static_cast<std::uint32_t>(height.index(x + 1U, z + 1U));
            mesh.indices.insert(mesh.indices.end(), {i00, i01, i10, i10, i01, i11});
        }
    }
    return mesh;
}

std::uint32_t terrain_mesh_triangle_count(const TerrainMeshData& mesh) {
    return cubey::render::mesh_index_count(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::terrain
