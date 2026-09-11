#include "pbr_furnace_scene.h"

#include <stdexcept>

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

[[nodiscard]] float metallic_for_row(std::uint32_t row) {
    if (kPbrFurnaceRowCount == 1U) {
        return 0.0F;
    }
    return static_cast<float>(row) / static_cast<float>(kPbrFurnaceRowCount - 1U);
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
        const float metallic = metallic_for_row(row);
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

PbrFurnaceLayout pbr_furnace_layout(std::string_view conformance_case) {
    if (conformance_case == "none") {
        const auto grid = pbr_furnace_material_grid();
        return {
            .materials = std::vector<PbrFurnaceMaterial>(grid.begin(), grid.end()),
            .camera_distance = 9.0F,
        };
    }
    if (conformance_case == "ior") {
        // The four left-to-right cells are IOR 1.0, 1.5, 2.42, and the
        // glTF IOR-zero compatibility sentinel. All other factors are fixed.
        return {
            .materials =
                {
                    {.row = 0,
                     .column = 0,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .ior = 1.0F,
                     .position = {-3.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 1,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .ior = 1.5F,
                     .position = {-1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 2,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .ior = 2.42F,
                     .position = {1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 3,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .ior = 0.0F,
                     .position = {3.0F, 0.0F, 0.0F}},
                },
            .camera_distance = 8.0F,
        };
    }
    if (conformance_case == "specular") {
        // The first pair isolates factor zero versus one. The second pair
        // isolates red and blue specular-color factors under the same IBL.
        return {
            .materials =
                {
                    {.row = 0,
                     .column = 0,
                     .metallic = 0.0F,
                     .roughness = 0.24F,
                     .specular_factor = 0.0F,
                     .position = {-3.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 1,
                     .metallic = 0.0F,
                     .roughness = 0.24F,
                     .specular_factor = 1.0F,
                     .position = {-1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 2,
                     .metallic = 0.0F,
                     .roughness = 0.24F,
                     .specular_color_factor = {1.0F, 0.05F, 0.05F},
                     .ior = 0.0F,
                     .position = {1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 3,
                     .metallic = 0.0F,
                     .roughness = 0.24F,
                     .specular_color_factor = {0.05F, 0.05F, 1.0F},
                     .ior = 0.0F,
                     .position = {3.0F, 0.0F, 0.0F}},
                },
            .camera_distance = 8.0F,
        };
    }
    if (conformance_case == "clearcoat") {
        // The first pair differs only in an ignored roughness while the coat
        // factor is zero. The second pair keeps the same underlying dielectric
        // while varying an enabled coat's roughness.
        return {
            .materials =
                {
                    {.row = 0,
                     .column = 0,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .clearcoat_factor = 0.0F,
                     .clearcoat_roughness = 0.04F,
                     .position = {-3.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 1,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .clearcoat_factor = 0.0F,
                     .clearcoat_roughness = 1.0F,
                     .position = {-1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 2,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .clearcoat_factor = 1.0F,
                     .clearcoat_roughness = 0.04F,
                     .position = {1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 3,
                     .metallic = 0.0F,
                     .roughness = 0.32F,
                     .clearcoat_factor = 1.0F,
                     .clearcoat_roughness = 1.0F,
                     .position = {3.0F, 0.0F, 0.0F}},
                },
            .camera_distance = 8.0F,
        };
    }
    if (conformance_case == "anisotropy") {
        // The left pair is a zero-strength control: rotation must be inert.
        // The right pair enables the same material at orthogonal
        // rotations under fixed directional light, exposing the anisotropic
        // direct-BRDF response while retaining white IBL as the baseline.
        return {
            .materials =
                {
                    {.row = 0,
                     .column = 0,
                     .metallic = 1.0F,
                     .roughness = 0.32F,
                     .anisotropy_strength = 0.0F,
                     .anisotropy_rotation = 0.0F,
                     .position = {-3.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 1,
                     .metallic = 1.0F,
                     .roughness = 0.32F,
                     .anisotropy_strength = 0.0F,
                     .anisotropy_rotation = 1.570796F,
                     .position = {-1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 2,
                     .metallic = 1.0F,
                     .roughness = 0.32F,
                     .anisotropy_strength = 0.85F,
                     .anisotropy_rotation = 0.0F,
                     .position = {1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 3,
                     .metallic = 1.0F,
                     .roughness = 0.32F,
                     .anisotropy_strength = 0.85F,
                     .anisotropy_rotation = 1.570796F,
                     .position = {3.0F, 0.0F, 0.0F}},
                },
            .camera_distance = 8.0F,
        };
    }
    if (conformance_case == "iridescence") {
        // The left pair is a factor-zero control whose varying thickness must
        // be inert. The enabled right pair differs in dielectric/metal base and
        // film thickness, making the thin-film chromatic response inspectable
        // under the fixed white-IBL and directional conformance fixture.
        return {
            .materials =
                {
                    {.row = 0,
                     .column = 0,
                     .base_color_factor = {0.08F, 0.08F, 0.08F},
                     .metallic = 0.0F,
                     .roughness = 0.24F,
                     .iridescence_factor = 0.0F,
                     .iridescence_thickness_minimum = 100.0F,
                     .iridescence_thickness_maximum = 100.0F,
                     .position = {-3.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 1,
                     .base_color_factor = {0.08F, 0.08F, 0.08F},
                     .metallic = 0.0F,
                     .roughness = 0.24F,
                     .iridescence_factor = 0.0F,
                     .iridescence_thickness_minimum = 800.0F,
                     .iridescence_thickness_maximum = 800.0F,
                     .position = {-1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 2,
                     .base_color_factor = {0.08F, 0.08F, 0.08F},
                     .metallic = 0.0F,
                     .roughness = 0.24F,
                     .iridescence_factor = 1.0F,
                     .iridescence_ior = 1.3F,
                     .iridescence_thickness_minimum = 240.0F,
                     .iridescence_thickness_maximum = 240.0F,
                     .position = {1.0F, 0.0F, 0.0F}},
                    {.row = 0,
                     .column = 3,
                     .base_color_factor = {0.08F, 0.08F, 0.08F},
                     .metallic = 1.0F,
                     .roughness = 0.24F,
                     .iridescence_factor = 1.0F,
                     .iridescence_ior = 1.3F,
                     .iridescence_thickness_minimum = 620.0F,
                     .iridescence_thickness_maximum = 620.0F,
                     .position = {3.0F, 0.0F, 0.0F}},
                },
            .camera_distance = 8.0F,
        };
    }
    throw std::invalid_argument("unknown PBR furnace conformance case");
}

} // namespace cubey::projects::pbr_furnace
