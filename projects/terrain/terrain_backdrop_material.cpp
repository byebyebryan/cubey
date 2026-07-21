#include "terrain_backdrop_material.h"

#include <cubey/procedural/seed.h>
#include <cubey/render/generated_texture.h>

#include <cstddef>
#include <span>

namespace cubey::projects::terrain {
namespace {

static_assert(terrain_backdrop_material_texture_bytes() == 5'592'404U);

struct TerrainBackdropMaterialPushConstants {
    std::uint32_t seed = 0U;
};

} // namespace

TerrainBackdropMaterialTexture create_terrain_backdrop_material_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const std::filesystem::path& compute_shader, std::uint64_t seed) {
    const std::uint64_t derived =
        cubey::procedural::derive_seed(seed, "terrain.backdrop.filtered-detail.v3");
    const TerrainBackdropMaterialPushConstants push{
        .seed = static_cast<std::uint32_t>(derived ^ (derived >> 32U)),
    };
    return {
        .detail = cubey::render::create_compute_generated_texture_2d(
            device, gpu,
            {
                .label = "terrain backdrop filtered detail",
                .extent = {kTerrainBackdropMaterialExtent, kTerrainBackdropMaterialExtent},
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .mip_levels = kTerrainBackdropMaterialMipLevels,
                .shader = cubey::render::compute_shader_file(compute_shader),
                .group_size_x = 8U,
                .group_size_y = 8U,
                .push_constants = std::as_bytes(std::span{&push, 1U}),
                .sampler =
                    {
                        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                        .max_lod = static_cast<float>(kTerrainBackdropMaterialMipLevels - 1U),
                        .max_anisotropy = 8.0F,
                    },
            }),
    };
}

} // namespace cubey::projects::terrain
