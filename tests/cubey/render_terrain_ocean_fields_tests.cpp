#include <cubey/render/terrain_ocean_fields.h>

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

struct TerrainOceanFieldFixture {
    cubey::render::TerrainOceanGridDesc desc{
        .width = 2,
        .height = 2,
        .cell_size_m = 3.0F,
        .sea_level_m = 0.0F,
        .origin_x_m = 10.0F,
        .origin_z_m = -6.0F,
    };
    std::array<float, 4> height{-1.0F, 0.5F, 2.0F, -4.0F};
    std::array<float, 4> water_depth{1.0F, 0.0F, 0.0F, 4.0F};
    std::array<float, 4> shore_sdf{-3.0F, 0.5F, 2.0F, -6.0F};
    std::array<float, 4> slope{0.2F, 0.0F, 1.5F, 0.4F};
    std::array<cubey::render::TerrainOceanMaterialMask, 4> material_masks{
        cubey::render::TerrainOceanMaterialMask{.sand = 0.7F, .rock = 0.2F, .sediment = 0.1F},
        cubey::render::TerrainOceanMaterialMask{.sand = 1.0F},
        cubey::render::TerrainOceanMaterialMask{.rock = 0.35F, .vegetation = 0.65F},
        cubey::render::TerrainOceanMaterialMask{.sediment = 1.0F},
    };

    [[nodiscard]] cubey::render::TerrainOceanFieldView view() const {
        return {
            .desc = desc,
            .height_m = height,
            .water_depth_m = water_depth,
            .shore_sdf_m = shore_sdf,
            .slope = slope,
            .material_masks = material_masks,
        };
    }
};

} // namespace

void test_terrain_ocean_fields_pack_channel_layout_and_ranges() {
    const TerrainOceanFieldFixture fixture{};
    const cubey::render::TerrainOceanPackedFields packed =
        cubey::render::pack_terrain_ocean_fields(fixture.view());

    require(packed.desc.origin_x_m == fixture.desc.origin_x_m &&
                packed.desc.origin_z_m == fixture.desc.origin_z_m,
            "packed terrain-ocean fields should preserve grid metadata");
    require(packed.rgba32f.size() == 16U,
            "packed terrain-ocean fields should use one RGBA32F texel per sample");
    require_near(
        packed.rgba32f[static_cast<std::uint32_t>(
            cubey::render::TerrainOceanFieldChannel::HeightMeters)],
        fixture.height[0], 0.001F, "packed terrain-ocean fields should store height in R");
    require_near(
        packed.rgba32f[static_cast<std::uint32_t>(
            cubey::render::TerrainOceanFieldChannel::WaterDepthMeters)],
        fixture.water_depth[0], 0.001F,
        "packed terrain-ocean fields should store water depth in G");
    require_near(
        packed.rgba32f[static_cast<std::uint32_t>(
            cubey::render::TerrainOceanFieldChannel::ShoreSignedDistanceMeters)],
        fixture.shore_sdf[0], 0.001F, "packed terrain-ocean fields should store shore SDF in B");
    require_near(
        packed.rgba32f[static_cast<std::uint32_t>(cubey::render::TerrainOceanFieldChannel::Slope)],
        fixture.slope[0], 0.001F, "packed terrain-ocean fields should store slope in A");

    require_near(packed.min_height_m, -4.0F, 0.001F,
                 "packed terrain-ocean fields should track min height");
    require_near(packed.max_height_m, 2.0F, 0.001F,
                 "packed terrain-ocean fields should track max height");
    require_near(packed.max_water_depth_m, 4.0F, 0.001F,
                 "packed terrain-ocean fields should track max water depth");
    require_near(packed.max_abs_shore_sdf_m, 6.0F, 0.001F,
                 "packed terrain-ocean fields should track max abs shore SDF");
    require_near(packed.max_slope, 1.5F, 0.001F,
                 "packed terrain-ocean fields should track max slope");
}

void test_terrain_ocean_fields_reject_invalid_contract_data() {
    require_throws(
        [] {
            TerrainOceanFieldFixture fixture{};
            fixture.desc.width = 0U;
            cubey::render::validate_terrain_ocean_field_view(fixture.view());
        },
        "terrain-ocean fields should reject zero dimensions");
    require_throws(
        [] {
            TerrainOceanFieldFixture fixture{};
            fixture.desc.cell_size_m = 0.0F;
            cubey::render::validate_terrain_ocean_field_view(fixture.view());
        },
        "terrain-ocean fields should reject non-positive cell size");
    require_throws(
        [] {
            TerrainOceanFieldFixture fixture{};
            fixture.height[0] = std::numeric_limits<float>::quiet_NaN();
            cubey::render::validate_terrain_ocean_field_view(fixture.view());
        },
        "terrain-ocean fields should reject non-finite scalar fields");
    require_throws(
        [] {
            TerrainOceanFieldFixture fixture{};
            fixture.water_depth[0] = -0.1F;
            cubey::render::validate_terrain_ocean_field_view(fixture.view());
        },
        "terrain-ocean fields should reject negative water depth");
    require_throws(
        [] {
            TerrainOceanFieldFixture fixture{};
            fixture.slope[0] = -0.1F;
            cubey::render::validate_terrain_ocean_field_view(fixture.view());
        },
        "terrain-ocean fields should reject negative slope");
    require_throws(
        [] {
            TerrainOceanFieldFixture fixture{};
            fixture.material_masks[0].sand = 1.2F;
            cubey::render::validate_terrain_ocean_field_view(fixture.view());
        },
        "terrain-ocean fields should reject out-of-range material masks");
    require_throws(
        [] {
            TerrainOceanFieldFixture fixture{};
            fixture.material_masks[0] =
                cubey::render::TerrainOceanMaterialMask{.sand = 0.4F, .rock = 0.4F};
            cubey::render::validate_terrain_ocean_field_view(fixture.view());
        },
        "terrain-ocean fields should reject non-normalized material masks");
}
