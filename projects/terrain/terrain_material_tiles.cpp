#include "terrain_material_tiles.h"

#include <cubey/procedural/seed.h>
#include <cubey/render/generated_texture.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace cubey::projects::terrain {
namespace {

struct TerrainMaterialTilePushConstants {
    std::uint32_t seed = 0;
    std::uint32_t material = 0;
};

[[nodiscard]] cubey::render::Texture2D create_tile(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuRuntime& gpu,
                                                   const std::filesystem::path& compute_shader,
                                                   std::uint64_t seed, std::uint32_t material) {
    const std::uint64_t derived = cubey::procedural::derive_seed(
        seed, "terrain.quality.material." + std::to_string(material));
    const TerrainMaterialTilePushConstants push{
        .seed = static_cast<std::uint32_t>(derived ^ (derived >> 32U)),
        .material = material,
    };
    constexpr std::uint32_t mip_levels = 11U;
    return cubey::render::create_compute_generated_texture_2d(
        device, gpu,
        {
            .label = "terrain material tile " + std::to_string(material),
            .extent = {kTerrainMaterialTileExtent, kTerrainMaterialTileExtent},
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .mip_levels = mip_levels,
            .shader = cubey::render::compute_shader_file(compute_shader),
            .group_size_x = 8U,
            .group_size_y = 8U,
            .push_constants = std::as_bytes(std::span{&push, 1U}),
            .sampler =
                {
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                    .max_lod = static_cast<float>(mip_levels - 1U),
                },
        });
}

[[nodiscard]] cubey::render::SampledImageMaterialBinding
binding(std::uint32_t index, const cubey::render::Texture2D& texture) {
    return {
        .binding = index,
        .sampler = texture.sampler().handle(),
        .image_view = texture.view(),
    };
}

} // namespace

TerrainMaterialTiles create_terrain_material_tiles(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuRuntime& gpu,
                                                   const std::filesystem::path& compute_shader,
                                                   std::uint64_t seed) {
    return {
        .ground = create_tile(device, gpu, compute_shader, seed, 0U),
        .scree = create_tile(device, gpu, compute_shader, seed, 1U),
        .rock = create_tile(device, gpu, compute_shader, seed, 2U),
        .snow = create_tile(device, gpu, compute_shader, seed, 3U),
    };
}

std::vector<cubey::render::SampledImageMaterialBinding>
terrain_material_tile_bindings(const TerrainMaterialTiles& tiles) {
    return {
        binding(0U, tiles.ground),
        binding(1U, tiles.scree),
        binding(2U, tiles.rock),
        binding(3U, tiles.snow),
    };
}

} // namespace cubey::projects::terrain
