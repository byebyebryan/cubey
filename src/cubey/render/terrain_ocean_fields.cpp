#include <cubey/render/terrain_ocean_fields.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::render {
namespace {

constexpr float kTerrainOceanMaskTolerance = 0.001F;

void validate_finite(float value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(message);
    }
}

void validate_material_mask(const TerrainOceanMaterialMask& mask) {
    validate_finite(mask.sand, "terrain-ocean material masks must be finite");
    validate_finite(mask.rock, "terrain-ocean material masks must be finite");
    validate_finite(mask.vegetation, "terrain-ocean material masks must be finite");
    validate_finite(mask.sediment, "terrain-ocean material masks must be finite");
    if (mask.sand < 0.0F || mask.sand > 1.0F || mask.rock < 0.0F || mask.rock > 1.0F ||
        mask.vegetation < 0.0F || mask.vegetation > 1.0F || mask.sediment < 0.0F ||
        mask.sediment > 1.0F) {
        throw std::runtime_error("terrain-ocean material masks must be normalized weights");
    }
    const float sum = mask.sand + mask.rock + mask.vegetation + mask.sediment;
    if (std::abs(sum - 1.0F) > kTerrainOceanMaskTolerance) {
        throw std::runtime_error("terrain-ocean material masks must sum to one");
    }
}

} // namespace

std::size_t terrain_ocean_sample_count(const TerrainOceanGridDesc& desc) {
    return static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height);
}

std::size_t terrain_ocean_field_texel_count(const TerrainOceanFieldView& fields) {
    validate_terrain_ocean_field_view(fields);
    return terrain_ocean_sample_count(fields.desc);
}

void validate_terrain_ocean_field_view(const TerrainOceanFieldView& fields) {
    if (fields.desc.width == 0U || fields.desc.height == 0U) {
        throw std::runtime_error("terrain-ocean fields require nonzero dimensions");
    }
    validate_finite(fields.desc.cell_size_m, "terrain-ocean fields require finite grid metadata");
    validate_finite(fields.desc.sea_level_m, "terrain-ocean fields require finite grid metadata");
    validate_finite(fields.desc.origin_x_m, "terrain-ocean fields require finite grid metadata");
    validate_finite(fields.desc.origin_z_m, "terrain-ocean fields require finite grid metadata");
    if (fields.desc.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain-ocean fields require a positive cell size");
    }
    const std::size_t count = terrain_ocean_sample_count(fields.desc);
    if (fields.height_m.size() != count || fields.water_depth_m.size() != count ||
        fields.shore_sdf_m.size() != count || fields.slope.size() != count) {
        throw std::runtime_error("terrain-ocean field spans must match grid dimensions");
    }
    if (!fields.material_masks.empty() && fields.material_masks.size() != count) {
        throw std::runtime_error("terrain-ocean material mask span must be empty or match grid dimensions");
    }
    for (std::size_t index = 0; index < count; ++index) {
        const float height = fields.height_m[index];
        const float water_depth = fields.water_depth_m[index];
        const float shore_sdf = fields.shore_sdf_m[index];
        const float slope = fields.slope[index];
        validate_finite(height, "terrain-ocean fields must be finite");
        validate_finite(water_depth, "terrain-ocean fields must be finite");
        validate_finite(shore_sdf, "terrain-ocean fields must be finite");
        validate_finite(slope, "terrain-ocean fields must be finite");
        if (water_depth < 0.0F) {
            throw std::runtime_error("terrain-ocean water depth must be non-negative");
        }
        if (slope < 0.0F) {
            throw std::runtime_error("terrain-ocean slope must be non-negative");
        }
        if (!fields.material_masks.empty()) {
            validate_material_mask(fields.material_masks[index]);
        }
    }
}

TerrainOceanPackedFields pack_terrain_ocean_fields(const TerrainOceanFieldView& fields) {
    validate_terrain_ocean_field_view(fields);
    TerrainOceanPackedFields packed{
        .desc = fields.desc,
    };

    const std::size_t count = terrain_ocean_sample_count(fields.desc);
    packed.rgba32f.resize(count * 4U);
    if (count == 0U) {
        return packed;
    }

    packed.min_height_m = fields.height_m[0];
    packed.max_height_m = fields.height_m[0];
    for (std::size_t index = 0; index < count; ++index) {
        const float height = fields.height_m[index];
        const float water_depth = fields.water_depth_m[index];
        const float shore_sdf = fields.shore_sdf_m[index];
        const float slope = fields.slope[index];

        packed.rgba32f[(index * 4U) + static_cast<std::uint32_t>(
                                           TerrainOceanFieldChannel::HeightMeters)] = height;
        packed.rgba32f[(index * 4U) + static_cast<std::uint32_t>(
                                           TerrainOceanFieldChannel::WaterDepthMeters)] =
            water_depth;
        packed.rgba32f[(index * 4U) + static_cast<std::uint32_t>(
                                           TerrainOceanFieldChannel::ShoreSignedDistanceMeters)] =
            shore_sdf;
        packed.rgba32f[(index * 4U) + static_cast<std::uint32_t>(
                                           TerrainOceanFieldChannel::Slope)] = slope;

        packed.min_height_m = std::min(packed.min_height_m, height);
        packed.max_height_m = std::max(packed.max_height_m, height);
        packed.max_water_depth_m = std::max(packed.max_water_depth_m, water_depth);
        packed.max_abs_shore_sdf_m = std::max(packed.max_abs_shore_sdf_m, std::abs(shore_sdf));
        packed.max_slope = std::max(packed.max_slope, slope);
    }
    return packed;
}

Texture2D create_uploaded_terrain_ocean_field_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const TerrainOceanPackedFields& fields) {
    if (fields.desc.width == 0U || fields.desc.height == 0U) {
        throw std::runtime_error("terrain-ocean packed texture requires nonzero dimensions");
    }
    const std::span<const std::uint8_t> bytes{
        reinterpret_cast<const std::uint8_t*>(fields.rgba32f.data()),
        fields.rgba32f.size() * sizeof(float),
    };
    return create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {fields.desc.width, fields.desc.height},
            .mip_levels = 1U,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .bytes = bytes,
            .create_sampler = true,
            .sampler =
                {
                    .min_filter = VK_FILTER_LINEAR,
                    .mag_filter = VK_FILTER_LINEAR,
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                    .max_lod = 0.0F,
                },
        });
}

} // namespace cubey::render
