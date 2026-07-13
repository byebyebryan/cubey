#pragma once

#include <cubey/render/material_instance.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace cubey::projects::terrain {

inline constexpr std::uint32_t kTerrainMaterialTileExtent = 1024U;
inline constexpr float kTerrainMaterialTilePeriodM = 256.0F;
inline constexpr std::uint32_t kTerrainMaterialTileCount = 4U;
inline constexpr std::uint32_t kTerrainLayeredMaterialTextureCount = 8U;

struct TerrainMaterialTiles {
    cubey::render::Texture2D ground;
    cubey::render::Texture2D scree;
    cubey::render::Texture2D rock;
    cubey::render::Texture2D snow;
};

struct TerrainMaterialLayerTextures {
    cubey::render::Texture2D albedo_height;
    cubey::render::Texture2D normal_roughness;
};

struct TerrainLayeredMaterialTextures {
    TerrainMaterialLayerTextures ground;
    TerrainMaterialLayerTextures scree;
    TerrainMaterialLayerTextures rock;
    TerrainMaterialLayerTextures snow;
};

[[nodiscard]] TerrainMaterialTiles
create_terrain_material_tiles(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                              const std::filesystem::path& compute_shader, std::uint64_t seed);
[[nodiscard]] std::vector<cubey::render::SampledImageMaterialBinding>
terrain_material_tile_bindings(const TerrainMaterialTiles& tiles);

[[nodiscard]] TerrainLayeredMaterialTextures create_terrain_layered_material_textures(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const std::filesystem::path& compute_shader, std::uint64_t seed);
[[nodiscard]] std::vector<cubey::render::SampledImageMaterialBinding>
terrain_layered_material_bindings(const TerrainLayeredMaterialTextures& textures);

} // namespace cubey::projects::terrain
