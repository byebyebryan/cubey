#pragma once

#include <cubey/render/texture.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cubey::render {

struct TerrainOceanGridDesc {
    std::uint32_t version = 1;
    std::uint64_t seed = 0;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    float cell_size_m = 1.0F;
    float sea_level_m = 0.0F;
    float origin_x_m = 0.0F;
    float origin_z_m = 0.0F;
};

struct TerrainOceanMaterialMask {
    float sand = 0.0F;
    float rock = 0.0F;
    float vegetation = 0.0F;
    float sediment = 0.0F;
};

enum class TerrainOceanFieldChannel : std::uint32_t {
    HeightMeters = 0,
    WaterDepthMeters = 1,
    ShoreSignedDistanceMeters = 2,
    Slope = 3,
};

struct TerrainOceanFieldView {
    TerrainOceanGridDesc desc{};
    std::span<const float> height_m{};
    std::span<const float> water_depth_m{};
    std::span<const float> shore_sdf_m{};
    std::span<const float> slope{};
    std::span<const TerrainOceanMaterialMask> material_masks{};
};

struct TerrainOceanPackedFields {
    TerrainOceanGridDesc desc{};
    std::vector<float> rgba32f{};
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    float max_water_depth_m = 0.0F;
    float max_abs_shore_sdf_m = 0.0F;
    float max_slope = 0.0F;
};

[[nodiscard]] std::size_t terrain_ocean_sample_count(const TerrainOceanGridDesc& desc);
[[nodiscard]] std::size_t terrain_ocean_field_texel_count(const TerrainOceanFieldView& fields);
void validate_terrain_ocean_field_view(const TerrainOceanFieldView& fields);
[[nodiscard]] TerrainOceanPackedFields pack_terrain_ocean_fields(
    const TerrainOceanFieldView& fields);
[[nodiscard]] Texture2D create_uploaded_terrain_ocean_field_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const TerrainOceanPackedFields& fields);

} // namespace cubey::render
