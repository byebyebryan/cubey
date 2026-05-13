#include "pbr_furnace_scene.h"

namespace cubey::projects::pbr_furnace {
namespace {

constexpr float kMinimumRoughness = 0.04F;
constexpr float kSphereSpacing = 1.25F;
constexpr float kRowSpacing = 1.25F;

[[nodiscard]] float roughness_for_column(std::uint32_t column) {
    if (kPbrFurnaceColumnCount == 1U) {
        return kMinimumRoughness;
    }
    const float t = static_cast<float>(column) / static_cast<float>(kPbrFurnaceColumnCount - 1U);
    return kMinimumRoughness + ((1.0F - kMinimumRoughness) * t);
}

[[nodiscard]] math::Vec3 position_for_cell(std::uint32_t row, std::uint32_t column) {
    const float centered_column =
        static_cast<float>(column) - (static_cast<float>(kPbrFurnaceColumnCount - 1U) * 0.5F);
    const float centered_row =
        (static_cast<float>(kPbrFurnaceRowCount - 1U) * 0.5F) - static_cast<float>(row);
    return {
        centered_column * kSphereSpacing,
        centered_row * kRowSpacing,
        0.0F,
    };
}

} // namespace

std::array<PbrFurnaceMaterial, kPbrFurnaceMaterialCount> pbr_furnace_material_grid() {
    std::array<PbrFurnaceMaterial, kPbrFurnaceMaterialCount> materials{};
    std::size_t index = 0;
    for (std::uint32_t row = 0; row < kPbrFurnaceRowCount; ++row) {
        const float metallic = row == 0U ? 1.0F : 0.0F;
        for (std::uint32_t column = 0; column < kPbrFurnaceColumnCount; ++column) {
            materials.at(index) = {
                .row = row,
                .column = column,
                .metallic = metallic,
                .roughness = roughness_for_column(column),
                .position = position_for_cell(row, column),
            };
            ++index;
        }
    }
    return materials;
}

} // namespace cubey::projects::pbr_furnace
