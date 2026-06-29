#include "terrain_debug_export.h"

#include <cubey/core/image_io.h>
#include <cubey/procedural/operators.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace cubey::projects::terrain {
namespace {

struct Rgb {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

struct DebugViewName {
    TerrainDebugView view = TerrainDebugView::Final;
    std::string_view name{};
};

struct FieldNormalization {
    float min_value = 0.0F;
    float max_value = 1.0F;
    bool log_scale = false;
};

inline constexpr std::array<DebugViewName, 26> kDebugViewNames{
    DebugViewName{TerrainDebugView::Final, "final"},
    DebugViewName{TerrainDebugView::MountainRelief, "mountain-relief"},
    DebugViewName{TerrainDebugView::Height, "height"},
    DebugViewName{TerrainDebugView::Slope, "slope"},
    DebugViewName{TerrainDebugView::MountainSupport, "mountain-support"},
    DebugViewName{TerrainDebugView::RidgeSupport, "ridge-support"},
    DebugViewName{TerrainDebugView::PeakSupport, "peak-support"},
    DebugViewName{TerrainDebugView::MountainUplift, "mountain-uplift"},
    DebugViewName{TerrainDebugView::RidgeUplift, "ridge-uplift"},
    DebugViewName{TerrainDebugView::PeakUplift, "peak-uplift"},
    DebugViewName{TerrainDebugView::DrainagePotential, "drainage-potential"},
    DebugViewName{TerrainDebugView::RoutingFillDelta, "routing-fill-delta"},
    DebugViewName{TerrainDebugView::FlowDirection, "flow-direction"},
    DebugViewName{TerrainDebugView::FlowAccumulation, "flow-accumulation"},
    DebugViewName{TerrainDebugView::StreamOrder, "stream-order"},
    DebugViewName{TerrainDebugView::RiverMask, "river-mask"},
    DebugViewName{TerrainDebugView::RiverTrunk, "river-trunk"},
    DebugViewName{TerrainDebugView::Tributaries, "tributaries"},
    DebugViewName{TerrainDebugView::RiverGraphPlan, "river-graph-plan"},
    DebugViewName{TerrainDebugView::RiverGraphDischarge, "river-graph-discharge"},
    DebugViewName{TerrainDebugView::SinkMask, "sink-mask"},
    DebugViewName{TerrainDebugView::ChannelWidth, "channel-width"},
    DebugViewName{TerrainDebugView::Wetness, "wetness"},
    DebugViewName{TerrainDebugView::Deposition, "deposition"},
    DebugViewName{TerrainDebugView::Material, "material"},
    DebugViewName{TerrainDebugView::Vegetation, "vegetation"},
};

inline constexpr std::array<TerrainDebugView, 26> kTerrainDebugReviewViews{
    TerrainDebugView::Final,
    TerrainDebugView::MountainRelief,
    TerrainDebugView::Height,
    TerrainDebugView::Slope,
    TerrainDebugView::MountainSupport,
    TerrainDebugView::RidgeSupport,
    TerrainDebugView::PeakSupport,
    TerrainDebugView::MountainUplift,
    TerrainDebugView::RidgeUplift,
    TerrainDebugView::PeakUplift,
    TerrainDebugView::DrainagePotential,
    TerrainDebugView::RoutingFillDelta,
    TerrainDebugView::FlowDirection,
    TerrainDebugView::FlowAccumulation,
    TerrainDebugView::StreamOrder,
    TerrainDebugView::RiverMask,
    TerrainDebugView::RiverTrunk,
    TerrainDebugView::Tributaries,
    TerrainDebugView::RiverGraphPlan,
    TerrainDebugView::RiverGraphDischarge,
    TerrainDebugView::SinkMask,
    TerrainDebugView::ChannelWidth,
    TerrainDebugView::Wetness,
    TerrainDebugView::Deposition,
    TerrainDebugView::Material,
    TerrainDebugView::Vegetation,
};

[[nodiscard]] bool name_matches(std::string_view value, std::string_view canonical) {
    if (value.size() != canonical.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char lhs = value[index] == '_' ? '-' : value[index];
        if (lhs != canonical[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(
        std::round(cubey::procedural::saturate(value) * 255.0F));
}

[[nodiscard]] Rgb lerp_rgb(Rgb a, Rgb b, float t) {
    return {
        .r = cubey::procedural::lerp(a.r, b.r, t),
        .g = cubey::procedural::lerp(a.g, b.g, t),
        .b = cubey::procedural::lerp(a.b, b.b, t),
    };
}

void put_pixel(std::vector<std::uint8_t>& pixels, std::size_t index, Rgb color) {
    pixels[index + 0U] = byte(color.r);
    pixels[index + 1U] = byte(color.g);
    pixels[index + 2U] = byte(color.b);
    pixels[index + 3U] = 255U;
}

[[nodiscard]] FieldNormalization
make_field_normalization(const cubey::procedural::ScalarField2D& field, bool log_scale) {
    const cubey::procedural::ScalarFieldStats stats = field.summarize();
    if (stats.span <= 0.0F) {
        return {.log_scale = log_scale};
    }
    const float min_value = log_scale ? std::log1p(std::max(stats.min, 0.0F)) : stats.min;
    const float max_value = log_scale ? std::log1p(std::max(stats.max, 0.0F)) : stats.max;
    return {
        .min_value = min_value,
        .max_value = max_value,
        .log_scale = log_scale,
    };
}

[[nodiscard]] float normalized_field_value(const cubey::procedural::ScalarField2D& field,
                                           std::uint32_t x, std::uint32_t y,
                                           const FieldNormalization& normalization) {
    if (normalization.min_value == normalization.max_value) {
        return 0.0F;
    }
    const float value = normalization.log_scale ? std::log1p(std::max(field.at(x, y), 0.0F))
                                                : field.at(x, y);
    return cubey::procedural::saturate((value - normalization.min_value) /
                                       (normalization.max_value - normalization.min_value));
}

[[nodiscard]] Rgb terrain_ramp(float value) {
    const Rgb low{0.20F, 0.18F, 0.13F};
    const Rgb mid{0.38F, 0.48F, 0.28F};
    const Rgb high{0.72F, 0.70F, 0.62F};
    if (value < 0.55F) {
        return lerp_rgb(low, mid, value / 0.55F);
    }
    return lerp_rgb(mid, high, (value - 0.55F) / 0.45F);
}

[[nodiscard]] Rgb scalar_color(const cubey::procedural::ScalarField2D& field, std::uint32_t x,
                               std::uint32_t y,
                               const FieldNormalization& normalization) {
    const float value = normalized_field_value(field, x, y, normalization);
    return lerp_rgb(Rgb{0.04F, 0.07F, 0.12F}, Rgb{0.95F, 0.86F, 0.45F}, value);
}

[[nodiscard]] Rgb final_color(const TerrainRegionProduct& product, std::uint32_t x,
                              std::uint32_t y,
                              const FieldNormalization& height_normalization) {
    const auto& height = terrain_product_field(product, kTerrainFieldHeightM);
    const auto& slope = terrain_product_field(product, kTerrainFieldSlope);
    const auto& rock = terrain_product_field(product, kTerrainFieldMaterialRock);
    const auto& soil = terrain_product_field(product, kTerrainFieldMaterialSoil);
    const auto& grass = terrain_product_field(product, kTerrainFieldMaterialGrass);
    const auto& river = terrain_product_field(product, kTerrainFieldRiverMask);
    const float h = normalized_field_value(height, x, y, height_normalization);
    const float shade = 1.0F - (cubey::procedural::smoothstep(0.06F, 0.46F, slope.at(x, y)) * 0.34F);
    const Rgb terrain = terrain_ramp(h);
    Rgb color{
        .r = ((rock.at(x, y) * 0.50F) + (soil.at(x, y) * 0.38F) +
              (grass.at(x, y) * 0.20F) + (terrain.r * 0.35F)) *
             shade,
        .g = ((rock.at(x, y) * 0.48F) + (soil.at(x, y) * 0.34F) +
              (grass.at(x, y) * 0.52F) + (terrain.g * 0.35F)) *
             shade,
        .b = ((rock.at(x, y) * 0.44F) + (soil.at(x, y) * 0.22F) +
              (grass.at(x, y) * 0.22F) + (terrain.b * 0.35F)) *
             shade,
    };
    color = lerp_rgb(color, Rgb{0.06F, 0.22F, 0.48F}, river.at(x, y) * 0.82F);
    return color;
}

[[nodiscard]] float field_at_clamped(const cubey::procedural::ScalarField2D& field, int x, int y) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    const int clamped_x = std::clamp(x, 0, static_cast<int>(desc.width) - 1);
    const int clamped_y = std::clamp(y, 0, static_cast<int>(desc.height) - 1);
    return field.at(static_cast<std::uint32_t>(clamped_x),
                    static_cast<std::uint32_t>(clamped_y));
}

[[nodiscard]] float hillshade(const cubey::procedural::ScalarField2D& height, std::uint32_t x,
                              std::uint32_t y) {
    const float cell_size = std::max(height.desc().cell_size, 1.0F);
    const int ix = static_cast<int>(x);
    const int iy = static_cast<int>(y);
    const float dzdx =
        (field_at_clamped(height, ix + 1, iy) - field_at_clamped(height, ix - 1, iy)) /
        (cell_size * 2.0F);
    const float dzdy =
        (field_at_clamped(height, ix, iy + 1) - field_at_clamped(height, ix, iy - 1)) /
        (cell_size * 2.0F);

    constexpr float kVerticalExaggeration = 1.45F;
    const float nx = -dzdx * kVerticalExaggeration;
    const float ny = -dzdy * kVerticalExaggeration;
    constexpr float nz = 1.0F;
    const float inv_length = 1.0F / std::sqrt((nx * nx) + (ny * ny) + (nz * nz));

    constexpr float kLightX = -0.42F;
    constexpr float kLightY = -0.52F;
    constexpr float kLightZ = 0.74F;
    const float lit = std::max(0.0F, ((nx * inv_length) * kLightX) +
                                        ((ny * inv_length) * kLightY) +
                                        ((nz * inv_length) * kLightZ));
    return 0.32F + (lit * 0.78F);
}

[[nodiscard]] Rgb mountain_relief_color(const TerrainRegionProduct& product, std::uint32_t x,
                                        std::uint32_t y,
                                        const FieldNormalization& height_normalization,
                                        const FieldNormalization& relief_normalization) {
    const auto& height = terrain_product_field(product, kTerrainFieldHeightM);
    const auto& local_relief = terrain_product_field(product, kTerrainFieldLocalRelief);
    const auto& mountain_support = terrain_product_field(product, kTerrainFieldMountainSupport);
    const auto& ridge_support = terrain_product_field(product, kTerrainFieldRidgeSupport);
    const auto& peak_support = terrain_product_field(product, kTerrainFieldPeakSupport);

    const float h = normalized_field_value(height, x, y, height_normalization);
    const float relief = normalized_field_value(local_relief, x, y, relief_normalization);
    const float mountain = cubey::procedural::saturate(mountain_support.at(x, y));
    const float ridge = cubey::procedural::saturate(ridge_support.at(x, y));
    const float peak = cubey::procedural::saturate(peak_support.at(x, y));

    Rgb color = terrain_ramp(h);
    color = lerp_rgb(color, Rgb{0.36F, 0.35F, 0.30F}, mountain * 0.28F);
    color = lerp_rgb(color, Rgb{0.78F, 0.72F, 0.56F}, ridge * 0.34F);
    color = lerp_rgb(color, Rgb{0.92F, 0.90F, 0.82F}, peak * 0.54F);

    const float shade = hillshade(height, x, y);
    const float relief_boost = 0.86F + (cubey::procedural::smoothstep(0.18F, 0.86F, relief) * 0.34F);
    color.r *= shade * relief_boost;
    color.g *= shade * relief_boost;
    color.b *= shade * relief_boost;
    return color;
}

[[nodiscard]] Rgb material_color(const TerrainRegionProduct& product, std::uint32_t x,
                                 std::uint32_t y) {
    const auto& rock = terrain_product_field(product, kTerrainFieldMaterialRock);
    const auto& soil = terrain_product_field(product, kTerrainFieldMaterialSoil);
    const auto& grass = terrain_product_field(product, kTerrainFieldMaterialGrass);
    return {
        .r = (rock.at(x, y) * 0.62F) + (soil.at(x, y) * 0.44F) + (grass.at(x, y) * 0.16F),
        .g = (rock.at(x, y) * 0.58F) + (soil.at(x, y) * 0.31F) + (grass.at(x, y) * 0.48F),
        .b = (rock.at(x, y) * 0.54F) + (soil.at(x, y) * 0.18F) + (grass.at(x, y) * 0.18F),
    };
}

[[nodiscard]] const cubey::procedural::ScalarField2D&
field_for_debug_view(const TerrainRegionProduct& product, TerrainDebugView view) {
    switch (view) {
    case TerrainDebugView::Height:
        return terrain_product_field(product, kTerrainFieldHeightM);
    case TerrainDebugView::Slope:
        return terrain_product_field(product, kTerrainFieldSlope);
    case TerrainDebugView::MountainSupport:
        return terrain_product_field(product, kTerrainFieldMountainSupport);
    case TerrainDebugView::RidgeSupport:
        return terrain_product_field(product, kTerrainFieldRidgeSupport);
    case TerrainDebugView::PeakSupport:
        return terrain_product_field(product, kTerrainFieldPeakSupport);
    case TerrainDebugView::MountainUplift:
        return terrain_product_field(product, kTerrainFieldMountainUplift);
    case TerrainDebugView::RidgeUplift:
        return terrain_product_field(product, kTerrainFieldRidgeUplift);
    case TerrainDebugView::PeakUplift:
        return terrain_product_field(product, kTerrainFieldPeakUplift);
    case TerrainDebugView::DrainagePotential:
        return terrain_product_field(product, kTerrainFieldDrainagePotential);
    case TerrainDebugView::RoutingFillDelta:
        return terrain_product_field(product, kTerrainFieldRoutingFillDelta);
    case TerrainDebugView::FlowDirection:
        return terrain_product_field(product, kTerrainFieldFlowDirection);
    case TerrainDebugView::FlowAccumulation:
        return terrain_product_field(product, kTerrainFieldFlowAccumulation);
    case TerrainDebugView::StreamOrder:
        return terrain_product_field(product, kTerrainFieldStreamOrder);
    case TerrainDebugView::RiverMask:
        return terrain_product_field(product, kTerrainFieldRiverMask);
    case TerrainDebugView::RiverTrunk:
        return terrain_product_field(product, kTerrainFieldRiverTrunk);
    case TerrainDebugView::Tributaries:
        return terrain_product_field(product, kTerrainFieldTributaries);
    case TerrainDebugView::RiverGraphPlan:
        return terrain_product_field(product, kTerrainFieldRiverGraphPlan);
    case TerrainDebugView::RiverGraphDischarge:
        return terrain_product_field(product, kTerrainFieldRiverGraphDischarge);
    case TerrainDebugView::SinkMask:
        return terrain_product_field(product, kTerrainFieldSinkMask);
    case TerrainDebugView::ChannelWidth:
        return terrain_product_field(product, kTerrainFieldChannelWidth);
    case TerrainDebugView::Wetness:
        return terrain_product_field(product, kTerrainFieldWetness);
    case TerrainDebugView::Deposition:
        return terrain_product_field(product, kTerrainFieldDeposition);
    case TerrainDebugView::Vegetation:
        return terrain_product_field(product, kTerrainFieldVegetationPotential);
    case TerrainDebugView::Final:
    case TerrainDebugView::MountainRelief:
    case TerrainDebugView::Material:
        break;
    }
    throw std::runtime_error("terrain debug view does not map to a scalar field");
}

[[nodiscard]] std::vector<std::uint8_t> render_debug_view(const TerrainRegionProduct& product,
                                                          TerrainDebugView view) {
    const cubey::procedural::Grid2DDesc& desc = product.fields.desc();
    const cubey::procedural::ScalarField2D* scalar_field = nullptr;
    FieldNormalization scalar_normalization{};
    FieldNormalization height_normalization{};
    FieldNormalization relief_normalization{};
    if (view == TerrainDebugView::Final || view == TerrainDebugView::MountainRelief) {
        height_normalization =
            make_field_normalization(terrain_product_field(product, kTerrainFieldHeightM), false);
        if (view == TerrainDebugView::MountainRelief) {
            relief_normalization =
                make_field_normalization(terrain_product_field(product, kTerrainFieldLocalRelief),
                                         false);
        }
    } else if (view != TerrainDebugView::Material) {
        scalar_field = &field_for_debug_view(product, view);
        scalar_normalization =
            make_field_normalization(*scalar_field, view == TerrainDebugView::FlowAccumulation);
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(desc.width) *
                                     static_cast<std::size_t>(desc.height) * 4U);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            Rgb color{};
            if (view == TerrainDebugView::Final) {
                color = final_color(product, x, y, height_normalization);
            } else if (view == TerrainDebugView::MountainRelief) {
                color = mountain_relief_color(product, x, y, height_normalization,
                                              relief_normalization);
            } else if (view == TerrainDebugView::Material) {
                color = material_color(product, x, y);
            } else {
                color = scalar_color(*scalar_field, x, y, scalar_normalization);
            }
            const std::size_t flipped_y = static_cast<std::size_t>(desc.height - 1U - y);
            const std::size_t index =
                ((flipped_y * static_cast<std::size_t>(desc.width)) + x) * 4U;
            put_pixel(pixels, index, color);
        }
    }
    return pixels;
}

} // namespace

std::string_view terrain_debug_view_name(TerrainDebugView view) {
    for (const DebugViewName entry : kDebugViewNames) {
        if (entry.view == view) {
            return entry.name;
        }
    }
    return "final";
}

TerrainDebugView terrain_debug_view_from_name(std::string_view name) {
    if (name.empty()) {
        return TerrainDebugView::Final;
    }
    for (const DebugViewName entry : kDebugViewNames) {
        if (name_matches(name, entry.name)) {
            return entry.view;
        }
    }
    throw std::runtime_error("unknown terrain debug view: " + std::string(name));
}

std::span<const TerrainDebugView> terrain_debug_review_views() {
    return kTerrainDebugReviewViews;
}

void write_terrain_debug_png(const TerrainRegionProduct& product, TerrainDebugView view,
                             const std::filesystem::path& output_path) {
    if (output_path.empty()) {
        throw std::runtime_error("terrain debug PNG output path must be non-empty");
    }
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    const cubey::procedural::Grid2DDesc& desc = product.fields.desc();
    const std::vector<std::uint8_t> pixels = render_debug_view(product, view);
    cubey::write_png_rgba8(output_path, desc.width, desc.height, pixels);
}

void write_terrain_debug_review_pngs(const TerrainRegionProduct& product,
                                     const std::filesystem::path& output_dir) {
    if (output_dir.empty()) {
        throw std::runtime_error("terrain debug PNG output directory must be non-empty");
    }
    std::filesystem::create_directories(output_dir);
    for (const TerrainDebugView view : terrain_debug_review_views()) {
        const std::filesystem::path output_path =
            output_dir / (std::string(terrain_debug_view_name(view)) + ".png");
        write_terrain_debug_png(product, view, output_path);
    }
}

} // namespace cubey::projects::terrain
