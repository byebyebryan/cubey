#pragma once

#include <cubey/core/math.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace cubey::projects::pbr_furnace {

inline constexpr std::uint32_t kPbrFurnaceRowCount = 2;
inline constexpr std::uint32_t kPbrFurnaceColumnCount = 6;
inline constexpr std::size_t kPbrFurnaceMaterialCount =
    static_cast<std::size_t>(kPbrFurnaceRowCount) *
    static_cast<std::size_t>(kPbrFurnaceColumnCount);

struct PbrFurnaceMaterial {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    float metallic = 0.0F;
    float roughness = 1.0F;
    math::Vec3 position{0.0F, 0.0F, 0.0F};
};

[[nodiscard]] std::array<PbrFurnaceMaterial, kPbrFurnaceMaterialCount>
pbr_furnace_material_grid();

} // namespace cubey::projects::pbr_furnace
