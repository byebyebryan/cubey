#pragma once

#include <cubey/render/texture.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <cstdint>
#include <filesystem>

namespace cubey::projects::terrain {

inline constexpr std::uint32_t kTerrainBackdropMaterialExtent = 1'024U;
inline constexpr std::uint32_t kTerrainBackdropMaterialMipLevels = 11U;
inline constexpr float kTerrainBackdropMaterialMacroPeriodM = 32'768.0F;
inline constexpr float kTerrainBackdropMaterialLocalPeriodM = 2'048.0F;

struct TerrainBackdropMaterialTexture {
    cubey::render::Texture2D detail;
};

[[nodiscard]] constexpr std::uint64_t terrain_backdrop_material_texture_bytes() noexcept {
    std::uint64_t bytes = 0U;
    std::uint32_t extent = kTerrainBackdropMaterialExtent;
    for (std::uint32_t mip = 0U; mip < kTerrainBackdropMaterialMipLevels; ++mip) {
        bytes += static_cast<std::uint64_t>(extent) * extent * 4U;
        extent = extent > 1U ? extent / 2U : 1U;
    }
    return bytes;
}

[[nodiscard]] TerrainBackdropMaterialTexture create_terrain_backdrop_material_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const std::filesystem::path& compute_shader, std::uint64_t seed);

} // namespace cubey::projects::terrain
