#include "terrain_preview_mesh.h"

#include <cubey/render/color_space.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] const cubey::procedural::ScalarField2D* optional_field(
    const TerrainRegionProduct& product, std::string_view name) {
    return product.fields.try_field(name);
}

[[nodiscard]] float optional_at(const cubey::procedural::ScalarField2D* field, std::uint32_t x,
                                std::uint32_t y) {
    return field == nullptr ? 0.0F : field->at(x, y);
}

[[nodiscard]] cubey::render::PrimitiveVec3 to_primitive(cubey::math::Vec3 value) {
    return {value.x, value.y, value.z};
}

[[nodiscard]] float normalize01(float value, float min_value, float max_value) {
    const float span = std::max(max_value - min_value, 0.001F);
    return std::clamp((value - min_value) / span, 0.0F, 1.0F);
}

[[nodiscard]] float height_at(const cubey::procedural::ScalarField2D& height, std::uint32_t x,
                              std::uint32_t y, float vertical_scale) {
    return height.at(x, y) * vertical_scale;
}

[[nodiscard]] cubey::math::Vec3 normal_at(const cubey::procedural::ScalarField2D& height,
                                          std::uint32_t x, std::uint32_t y,
                                          float vertical_scale) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const std::uint32_t left = x == 0U ? x : x - 1U;
    const std::uint32_t right = std::min(x + 1U, desc.width - 1U);
    const std::uint32_t down = y == 0U ? y : y - 1U;
    const std::uint32_t up = std::min(y + 1U, desc.height - 1U);
    const float dx = static_cast<float>(right - left) * desc.cell_size;
    const float dz = static_cast<float>(up - down) * desc.cell_size;
    const float dhdx = (height_at(height, right, y, vertical_scale) -
                        height_at(height, left, y, vertical_scale)) /
                       std::max(dx, desc.cell_size);
    const float dhdz = (height_at(height, x, up, vertical_scale) -
                        height_at(height, x, down, vertical_scale)) /
                       std::max(dz, desc.cell_size);
    return glm::normalize(cubey::math::Vec3{-dhdx, 1.0F, -dhdz});
}

[[nodiscard]] cubey::math::Vec3 mix(cubey::math::Vec3 a, cubey::math::Vec3 b, float t) {
    return a + ((b - a) * std::clamp(t, 0.0F, 1.0F));
}

[[nodiscard]] cubey::math::Vec3 height_ramp(float height_t) {
    const cubey::math::Vec3 low{0.21F, 0.24F, 0.15F};
    const cubey::math::Vec3 mid{0.42F, 0.36F, 0.23F};
    const cubey::math::Vec3 high{0.68F, 0.66F, 0.58F};
    const cubey::math::Vec3 peak{0.88F, 0.88F, 0.82F};
    if (height_t < 0.48F) {
        return mix(low, mid, height_t / 0.48F);
    }
    if (height_t < 0.82F) {
        return mix(mid, high, (height_t - 0.48F) / 0.34F);
    }
    return mix(high, peak, (height_t - 0.82F) / 0.18F);
}

[[nodiscard]] cubey::math::Vec3 channel_color(const TerrainRegionProduct& product,
                                              std::uint32_t x, std::uint32_t y) {
    const cubey::procedural::ScalarField2D* channel =
        optional_field(product, kTerrainFieldChannelWidth);
    const cubey::procedural::ScalarField2D* valley =
        optional_field(product, kTerrainFieldValleyWidth);
    const float channel_t = std::clamp(optional_at(channel, x, y) / 96.0F, 0.0F, 1.0F);
    const float valley_t = std::clamp(optional_at(valley, x, y) / 360.0F, 0.0F, 1.0F);
    cubey::math::Vec3 color = mix({0.18F, 0.19F, 0.18F}, {0.45F, 0.40F, 0.24F}, valley_t);
    return mix(color, {0.04F, 0.20F, 0.28F}, channel_t);
}

[[nodiscard]] cubey::math::Vec3 river_color(const TerrainRegionProduct& product, std::uint32_t x,
                                            std::uint32_t y) {
    const cubey::procedural::ScalarField2D* river = optional_field(product, kTerrainFieldRiverMask);
    const cubey::procedural::ScalarField2D* trunk = optional_field(product, kTerrainFieldRiverTrunk);
    const cubey::procedural::ScalarField2D* tributaries =
        optional_field(product, kTerrainFieldTributaries);
    const cubey::procedural::ScalarField2D* discharge =
        optional_field(product, kTerrainFieldRiverGraphDischarge);
    const float river_t = std::clamp(optional_at(river, x, y), 0.0F, 1.0F);
    const float trunk_t = std::clamp(optional_at(trunk, x, y), 0.0F, 1.0F);
    const float tributary_t = std::clamp(optional_at(tributaries, x, y), 0.0F, 1.0F);
    const float discharge_t = std::clamp(optional_at(discharge, x, y), 0.0F, 1.0F);
    cubey::math::Vec3 color = mix({0.12F, 0.13F, 0.12F}, {0.22F, 0.27F, 0.30F}, river_t);
    color = mix(color, {0.05F, 0.36F, 0.50F}, tributary_t * 0.75F);
    color = mix(color, {0.02F, 0.20F, 0.56F}, trunk_t);
    return mix(color, {0.78F, 0.86F, 0.92F}, discharge_t * 0.35F);
}

[[nodiscard]] cubey::render::PrimitiveVec3 review_color(
    const TerrainRegionProduct& product, const cubey::procedural::ScalarField2D& height,
    std::uint32_t x, std::uint32_t y, TerrainPreviewColorMode color_mode) {
    const float height_t = normalize01(height.at(x, y), product.summary.height.min,
                                       product.summary.height.max);
    cubey::math::Vec3 color = height_ramp(height_t);
    if (color_mode == TerrainPreviewColorMode::Height) {
        return cubey::render::srgb_to_linear_rgb(to_primitive(color));
    }
    if (color_mode == TerrainPreviewColorMode::River) {
        return cubey::render::srgb_to_linear_rgb(to_primitive(river_color(product, x, y)));
    }
    if (color_mode == TerrainPreviewColorMode::Channel) {
        return cubey::render::srgb_to_linear_rgb(to_primitive(channel_color(product, x, y)));
    }

    const cubey::procedural::ScalarField2D* rock = optional_field(product, kTerrainFieldMaterialRock);
    const cubey::procedural::ScalarField2D* soil = optional_field(product, kTerrainFieldMaterialSoil);
    const cubey::procedural::ScalarField2D* grass =
        optional_field(product, kTerrainFieldMaterialGrass);
    const cubey::procedural::ScalarField2D* wetness = optional_field(product, kTerrainFieldWetness);
    const cubey::procedural::ScalarField2D* river = optional_field(product, kTerrainFieldRiverMask);
    const cubey::procedural::ScalarField2D* peak =
        optional_field(product, kTerrainFieldMountainPeakProminence);
    const cubey::procedural::ScalarField2D* ridge =
        optional_field(product, kTerrainFieldMountainRidgeInfluence);

    const float rock_t = std::clamp(optional_at(rock, x, y), 0.0F, 1.0F);
    const float soil_t = std::clamp(optional_at(soil, x, y), 0.0F, 1.0F);
    const float grass_t = std::clamp(optional_at(grass, x, y), 0.0F, 1.0F);
    const float wet_t = std::clamp(optional_at(wetness, x, y), 0.0F, 1.0F);
    const float river_t = std::clamp(optional_at(river, x, y), 0.0F, 1.0F);
    const float peak_t = std::clamp(optional_at(peak, x, y), 0.0F, 1.0F);
    const float ridge_t = std::clamp(optional_at(ridge, x, y), 0.0F, 1.0F);

    color = mix(color, {0.46F, 0.45F, 0.39F}, rock_t * 0.32F);
    color = mix(color, {0.35F, 0.27F, 0.17F}, soil_t * 0.20F);
    color = mix(color, {0.24F, 0.34F, 0.17F}, grass_t * 0.26F);
    color = mix(color, {0.16F, 0.30F, 0.31F}, wet_t * 0.15F);
    color = mix(color, {0.80F, 0.81F, 0.76F}, peak_t * 0.24F);
    color = mix(color, {0.58F, 0.55F, 0.48F}, ridge_t * 0.18F);
    color = mix(color, {0.05F, 0.24F, 0.30F}, river_t * 0.58F);

    return cubey::render::srgb_to_linear_rgb(to_primitive(color));
}

} // namespace

cubey::render::MeshConfig TerrainPreviewMeshData::mesh_config() const {
    return cubey::render::indexed_mesh_config(
        std::span<const cubey::render::VertexPositionColorNormal>(vertices.data(),
                                                                  vertices.size()),
        std::span<const std::uint32_t>(indices.data(), indices.size()));
}

TerrainPreviewMeshData make_terrain_preview_mesh(const TerrainRegionProduct& product,
                                                 const TerrainPreviewConfig& config) {
    const cubey::procedural::ScalarField2D& height =
        terrain_product_field(product, kTerrainFieldHeightM);
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    if (desc.width < 2U || desc.height < 2U) {
        throw std::runtime_error("terrain preview mesh requires at least a 2x2 height field");
    }
    if (height.sample_count() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("terrain preview mesh exceeds uint32 vertex range");
    }
    if (!std::isfinite(config.vertical_scale) || config.vertical_scale <= 0.0F) {
        throw std::runtime_error("terrain preview vertical scale must be positive");
    }

    TerrainPreviewMeshData mesh;
    mesh.vertices.reserve(height.sample_count());
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        const float world_z = cubey::procedural::grid_sample_y(desc, y);
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float world_x = cubey::procedural::grid_sample_x(desc, x);
            mesh.vertices.push_back({
                .position =
                    {world_x, height_at(height, x, y, config.vertical_scale), world_z},
                .color = review_color(product, height, x, y, config.color_mode),
                .normal = to_primitive(normal_at(height, x, y, config.vertical_scale)),
            });
        }
    }

    const std::uint64_t quad_count = static_cast<std::uint64_t>(desc.width - 1U) *
                                     static_cast<std::uint64_t>(desc.height - 1U);
    if (quad_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max() / 6U)) {
        throw std::runtime_error("terrain preview mesh exceeds uint32 index range");
    }
    mesh.indices.reserve(static_cast<std::size_t>(quad_count * 6U));
    for (std::uint32_t y = 0; y + 1U < desc.height; ++y) {
        for (std::uint32_t x = 0; x + 1U < desc.width; ++x) {
            const std::uint32_t i00 = static_cast<std::uint32_t>(height.index(x, y));
            const std::uint32_t i10 = static_cast<std::uint32_t>(height.index(x + 1U, y));
            const std::uint32_t i01 = static_cast<std::uint32_t>(height.index(x, y + 1U));
            const std::uint32_t i11 = static_cast<std::uint32_t>(height.index(x + 1U, y + 1U));
            mesh.indices.insert(mesh.indices.end(), {i00, i10, i01, i10, i11, i01});
        }
    }

    return mesh;
}

std::uint32_t terrain_preview_triangle_count(const TerrainPreviewMeshData& mesh) {
    return cubey::render::mesh_index_count(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::terrain
