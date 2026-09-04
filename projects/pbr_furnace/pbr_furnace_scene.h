#pragma once

#include <cubey/core/math.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cubey::projects::pbr_furnace {

inline constexpr std::uint32_t kPbrFurnaceRowCount = 6;
inline constexpr std::uint32_t kPbrFurnaceColumnCount = 6;
inline constexpr std::size_t kPbrFurnaceMaterialCount =
    static_cast<std::size_t>(kPbrFurnaceRowCount) *
    static_cast<std::size_t>(kPbrFurnaceColumnCount);

struct PbrFurnaceMaterial {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    float metallic = 0.0F;
    float roughness = 1.0F;
    math::Vec3 specular_color_factor{1.0F, 1.0F, 1.0F};
    float specular_factor = 1.0F;
    float ior = 1.5F;
    math::Vec3 position{0.0F, 0.0F, 0.0F};
};

struct PbrFurnaceLayout {
    std::vector<PbrFurnaceMaterial> materials{};
    float camera_distance = 9.0F;
};

[[nodiscard]] std::array<PbrFurnaceMaterial, kPbrFurnaceMaterialCount> pbr_furnace_material_grid();
[[nodiscard]] PbrFurnaceLayout pbr_furnace_layout(std::string_view conformance_case);

} // namespace cubey::projects::pbr_furnace
