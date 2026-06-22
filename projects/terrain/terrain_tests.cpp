#include "terrain_debug_export.h"
#include "terrain_generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float value, float expected, float tolerance, const char* message) {
    require(std::abs(value - expected) <= tolerance, message);
}

template <typename Fn> void require_throws(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] cubey::projects::terrain::TerrainRegionConfig small_config() {
    return cubey::projects::terrain::TerrainRegionConfig{
        .grid_width = 65,
        .grid_height = 65,
        .cell_size_m = 48.0F,
        .seed = 1234U,
    };
}

[[nodiscard]] const cubey::procedural::ScalarField2D&
field(const cubey::projects::terrain::TerrainRegionProduct& product, std::string_view name) {
    return cubey::projects::terrain::terrain_product_field(product, name);
}

[[nodiscard]] std::size_t count_active_samples(const cubey::procedural::ScalarField2D& field,
                                               float threshold) {
    std::size_t count = 0U;
    for (const float value : field.values()) {
        if (value >= threshold) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] std::size_t count_active_samples_with_neighbor(
    const cubey::procedural::ScalarField2D& field, float threshold) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    std::size_t count = 0U;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            if (field.at(x, y) < threshold) {
                continue;
            }
            bool has_neighbor = false;
            for (int oy = -1; oy <= 1 && !has_neighbor; ++oy) {
                const int sy = static_cast<int>(y) + oy;
                if (sy < 0 || sy >= static_cast<int>(desc.height)) {
                    continue;
                }
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const int sx = static_cast<int>(x) + ox;
                    if (sx < 0 || sx >= static_cast<int>(desc.width)) {
                        continue;
                    }
                    if (field.at(static_cast<std::uint32_t>(sx),
                                 static_cast<std::uint32_t>(sy)) >= threshold) {
                        has_neighbor = true;
                        break;
                    }
                }
            }
            if (has_neighbor) {
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] std::size_t edge_touch_count(const cubey::procedural::ScalarField2D& field,
                                           float threshold) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        left = left || field.at(0U, y) >= threshold;
        right = right || field.at(desc.width - 1U, y) >= threshold;
    }
    for (std::uint32_t x = 0; x < desc.width; ++x) {
        top = top || field.at(x, 0U) >= threshold;
        bottom = bottom || field.at(x, desc.height - 1U) >= threshold;
    }
    return static_cast<std::size_t>(left) + static_cast<std::size_t>(right) +
           static_cast<std::size_t>(top) + static_cast<std::size_t>(bottom);
}

[[nodiscard]] std::size_t max_horizontal_active_run(const cubey::procedural::ScalarField2D& field,
                                                    float threshold, std::uint32_t margin = 0U) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    std::size_t best = 0U;
    const std::uint32_t x_begin = std::min(margin, desc.width);
    const std::uint32_t x_end = desc.width > margin ? desc.width - margin : x_begin;
    const std::uint32_t y_begin = std::min(margin, desc.height);
    const std::uint32_t y_end = desc.height > margin ? desc.height - margin : y_begin;
    for (std::uint32_t y = y_begin; y < y_end; ++y) {
        std::size_t current = 0U;
        for (std::uint32_t x = x_begin; x < x_end; ++x) {
            if (field.at(x, y) >= threshold) {
                ++current;
                best = std::max(best, current);
            } else {
                current = 0U;
            }
        }
    }
    return best;
}

[[nodiscard]] std::size_t max_vertical_active_run(const cubey::procedural::ScalarField2D& field,
                                                  float threshold, std::uint32_t margin = 0U) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    std::size_t best = 0U;
    const std::uint32_t x_begin = std::min(margin, desc.width);
    const std::uint32_t x_end = desc.width > margin ? desc.width - margin : x_begin;
    const std::uint32_t y_begin = std::min(margin, desc.height);
    const std::uint32_t y_end = desc.height > margin ? desc.height - margin : y_begin;
    for (std::uint32_t x = x_begin; x < x_end; ++x) {
        std::size_t current = 0U;
        for (std::uint32_t y = y_begin; y < y_end; ++y) {
            if (field.at(x, y) >= threshold) {
                ++current;
                best = std::max(best, current);
            } else {
                current = 0U;
            }
        }
    }
    return best;
}

[[nodiscard]] std::size_t max_axis_aligned_active_run(
    const cubey::procedural::ScalarField2D& field, float threshold, std::uint32_t margin = 0U) {
    return std::max(max_horizontal_active_run(field, threshold, margin),
                    max_vertical_active_run(field, threshold, margin));
}

[[nodiscard]] std::size_t max_direction_active_run(const cubey::procedural::ScalarField2D& field,
                                                   float threshold, int direction_x,
                                                   int direction_y,
                                                   std::uint32_t margin = 0U) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    const int x_begin = static_cast<int>(std::min(margin, desc.width));
    const int x_end = static_cast<int>(desc.width > margin ? desc.width - margin : margin);
    const int y_begin = static_cast<int>(std::min(margin, desc.height));
    const int y_end = static_cast<int>(desc.height > margin ? desc.height - margin : margin);
    const auto inside = [&](int x, int y) {
        return x >= x_begin && x < x_end && y >= y_begin && y < y_end;
    };
    const auto active = [&](int x, int y) {
        return field.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) >=
               threshold;
    };

    std::size_t best = 0U;
    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            if (!active(x, y)) {
                continue;
            }
            const int previous_x = x - direction_x;
            const int previous_y = y - direction_y;
            if (inside(previous_x, previous_y) && active(previous_x, previous_y)) {
                continue;
            }

            std::size_t current = 0U;
            int sx = x;
            int sy = y;
            while (inside(sx, sy) && active(sx, sy)) {
                ++current;
                sx += direction_x;
                sy += direction_y;
            }
            best = std::max(best, current);
        }
    }
    return best;
}

[[nodiscard]] std::size_t max_diagonal_active_run(
    const cubey::procedural::ScalarField2D& field, float threshold, std::uint32_t margin = 0U) {
    return std::max(max_direction_active_run(field, threshold, 1, 1, margin),
                    max_direction_active_run(field, threshold, 1, -1, margin));
}

void test_terrain_region_config_defaults() {
    const cubey::projects::terrain::TerrainRegionConfig config{};
    require(config.grid_width == cubey::projects::terrain::kTerrainDefaultGridSize,
            "terrain default grid width should stay stable");
    require(config.grid_height == cubey::projects::terrain::kTerrainDefaultGridSize,
            "terrain default grid height should stay stable");
    require_near(config.cell_size_m, cubey::projects::terrain::kTerrainDefaultCellSizeM, 0.001F,
                 "terrain default cell size should stay stable");
    cubey::projects::terrain::validate_terrain_region_config(config);

    require_throws(
        [] {
            cubey::projects::terrain::TerrainRegionConfig invalid{};
            invalid.grid_width = 0;
            cubey::projects::terrain::validate_terrain_region_config(invalid);
        },
        "terrain config should reject invalid grid dimensions");
}

void test_terrain_product_emits_required_fields() {
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(small_config());

    const std::array<std::string_view, 24> required_fields{
        cubey::projects::terrain::kTerrainFieldHeightM,
        cubey::projects::terrain::kTerrainFieldBaseElevation,
        cubey::projects::terrain::kTerrainFieldBroadRelief,
        cubey::projects::terrain::kTerrainFieldRidgeUplift,
        cubey::projects::terrain::kTerrainFieldDetailResidual,
        cubey::projects::terrain::kTerrainFieldSlope,
        cubey::projects::terrain::kTerrainFieldCurvature,
        cubey::projects::terrain::kTerrainFieldLocalRelief,
        cubey::projects::terrain::kTerrainFieldDrainagePotential,
        cubey::projects::terrain::kTerrainFieldFlowDirection,
        cubey::projects::terrain::kTerrainFieldFlowAccumulation,
        cubey::projects::terrain::kTerrainFieldStreamOrder,
        cubey::projects::terrain::kTerrainFieldRiverMask,
        cubey::projects::terrain::kTerrainFieldRiverTrunk,
        cubey::projects::terrain::kTerrainFieldTributaries,
        cubey::projects::terrain::kTerrainFieldSinkMask,
        cubey::projects::terrain::kTerrainFieldChannelWidth,
        cubey::projects::terrain::kTerrainFieldValleyWidth,
        cubey::projects::terrain::kTerrainFieldWetness,
        cubey::projects::terrain::kTerrainFieldDeposition,
        cubey::projects::terrain::kTerrainFieldMaterialRock,
        cubey::projects::terrain::kTerrainFieldMaterialSoil,
        cubey::projects::terrain::kTerrainFieldMaterialGrass,
        cubey::projects::terrain::kTerrainFieldVegetationPotential,
    };
    for (const std::string_view name : required_fields) {
        require(product.fields.has_field(name), "terrain product should emit required field");
    }
    require(product.fields.field_count() == required_fields.size(),
            "terrain product should emit only the expected first-batch fields");
}

void test_terrain_product_has_useful_ranges() {
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(small_config());

    require(product.summary.height.sample_count ==
                product.fields.desc().width * product.fields.desc().height,
            "terrain summary should cover height samples");
    require(product.summary.height.span > 200.0F, "terrain source fields should produce relief");
    require(product.summary.slope.max > 0.01F, "terrain should have nonzero slope");
    require(product.summary.river_coverage > 0.001F,
            "terrain river product should contain visible river samples");
    require(product.summary.river_coverage < 0.35F,
            "terrain river product should not flood the whole patch");
    require(product.summary.max_channel_width_m > 8.0F,
            "terrain river product should derive channel widths");
    require(product.summary.wetness.max > product.summary.wetness.min,
            "terrain river product should vary wetness");
    require(std::isfinite(product.summary.height.min) && std::isfinite(product.summary.height.max),
            "terrain summary should be finite");

    const cubey::procedural::ScalarFieldStats flow =
        field(product, cubey::projects::terrain::kTerrainFieldFlowAccumulation).summarize();
    const cubey::procedural::ScalarFieldStats stream_order =
        field(product, cubey::projects::terrain::kTerrainFieldStreamOrder).summarize();
    const cubey::procedural::ScalarFieldStats drainage =
        field(product, cubey::projects::terrain::kTerrainFieldDrainagePotential).summarize();
    const cubey::procedural::ScalarFieldStats sink =
        field(product, cubey::projects::terrain::kTerrainFieldSinkMask).summarize();
    require(flow.max > flow.min, "flow accumulation should vary");
    require(stream_order.max >= 3.0F, "stream order should identify larger drainage trunks");
    require(drainage.max > drainage.min, "drainage potential should vary");
    require(sink.max > 0.0F && sink.max <= 1.0F, "sink mask should identify terminal cells");
}

void test_terrain_product_is_deterministic() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    const cubey::projects::terrain::TerrainRegionProduct first =
        cubey::projects::terrain::generate_terrain_region(config);
    const cubey::projects::terrain::TerrainRegionProduct repeat =
        cubey::projects::terrain::generate_terrain_region(config);
    require(first.summary.content_hash == repeat.summary.content_hash,
            "terrain product hash should repeat for fixed seed/config");

    config.seed += 1U;
    const cubey::projects::terrain::TerrainRegionProduct changed =
        cubey::projects::terrain::generate_terrain_region(config);
    require(first.summary.content_hash != changed.summary.content_hash,
            "terrain product hash should change when seed changes");
}

void test_terrain_materials_and_vegetation_are_bounded() {
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(small_config());
    const auto& rock = field(product, cubey::projects::terrain::kTerrainFieldMaterialRock);
    const auto& soil = field(product, cubey::projects::terrain::kTerrainFieldMaterialSoil);
    const auto& grass = field(product, cubey::projects::terrain::kTerrainFieldMaterialGrass);
    const auto& vegetation =
        field(product, cubey::projects::terrain::kTerrainFieldVegetationPotential);

    for (std::uint32_t y = 0; y < product.fields.desc().height; y += 8U) {
        for (std::uint32_t x = 0; x < product.fields.desc().width; x += 8U) {
            require_near(rock.at(x, y) + soil.at(x, y) + grass.at(x, y), 1.0F, 0.001F,
                         "terrain material masks should normalize");
            require(vegetation.at(x, y) >= 0.0F && vegetation.at(x, y) <= 1.0F,
                    "terrain vegetation potential should stay in [0, 1]");
        }
    }
}

void test_terrain_river_network_has_continuous_active_channels() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 129;
    config.grid_height = 129;
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& trunk = field(product, cubey::projects::terrain::kTerrainFieldRiverTrunk);
    const auto& tributaries = field(product, cubey::projects::terrain::kTerrainFieldTributaries);
    const auto& river_mask = field(product, cubey::projects::terrain::kTerrainFieldRiverMask);

    const std::size_t trunk_count = count_active_samples(trunk, 0.45F);
    const std::size_t tributary_count = count_active_samples(tributaries, 0.30F);
    const std::size_t river_count = count_active_samples(river_mask, 0.30F);
    require(trunk_count >= 24U, "terrain river trunk should include a meaningful active path");
    require(tributary_count >= 8U, "terrain tributary field should include active branches");
    require(river_count >= trunk_count,
            "terrain river mask should include the active trunk samples");

    const std::size_t connected_trunk_count = count_active_samples_with_neighbor(trunk, 0.45F);
    const std::size_t connected_river_count = count_active_samples_with_neighbor(river_mask, 0.30F);
    require(connected_trunk_count * 100U >= trunk_count * 90U,
            "terrain river trunk samples should be locally continuous");
    require(connected_river_count * 100U >= river_count * 80U,
            "terrain river mask samples should be locally continuous");

    const std::size_t trunk_core_count = count_active_samples(trunk, 0.80F);
    const std::size_t trunk_soft_count = count_active_samples(trunk, 0.30F);
    require(trunk_core_count > 0U, "terrain river trunk should retain a high-strength core");
    require(trunk_soft_count * 10U >= trunk_core_count * 14U,
            "terrain river trunk should rasterize as a soft channel band");
}

void test_terrain_river_core_avoids_long_grid_aligned_runs() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 513;
    config.grid_height = 513;
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& trunk = field(product, cubey::projects::terrain::kTerrainFieldRiverTrunk);
    const auto& river_mask = field(product, cubey::projects::terrain::kTerrainFieldRiverMask);
    constexpr std::uint32_t kInteriorMargin = 16U;
    require(max_axis_aligned_active_run(trunk, 0.80F, kInteriorMargin) <= 18U,
            "terrain river trunk should avoid long straight core runs");
    require(max_axis_aligned_active_run(river_mask, 0.80F, kInteriorMargin) <= 24U,
            "terrain river mask should avoid long straight high-strength runs");
    require(max_diagonal_active_run(trunk, 0.80F, kInteriorMargin) <= 24U,
            "terrain river trunk should avoid long diagonal core runs");
    require(max_diagonal_active_run(river_mask, 0.80F, kInteriorMargin) <= 30U,
            "terrain river mask should avoid long diagonal high-strength runs");
}

void test_terrain_river_uses_larger_routing_context() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 257;
    config.grid_height = 257;
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& trunk = field(product, cubey::projects::terrain::kTerrainFieldRiverTrunk);
    const auto& river_mask = field(product, cubey::projects::terrain::kTerrainFieldRiverMask);
    const auto& sink_mask = field(product, cubey::projects::terrain::kTerrainFieldSinkMask);

    require(edge_touch_count(river_mask, 0.25F) >= 2U,
            "terrain river mask should enter or leave multiple visible crop edges");
    require(edge_touch_count(trunk, 0.45F) >= 1U,
            "terrain river trunk should connect to a visible crop edge");

    const std::size_t sink_count = count_active_samples(sink_mask, 0.5F);
    const std::size_t total_count = sink_mask.sample_count();
    require(sink_count > 0U, "terrain sink mask should mark visible crop outlets");
    require(sink_count * 100U < total_count * 8U,
            "terrain sink mask should stay bounded to outlets instead of filling the patch");
}

void test_terrain_debug_export_writes_png() {
    require(cubey::projects::terrain::terrain_debug_view_from_name("final") ==
                cubey::projects::terrain::TerrainDebugView::Final,
            "terrain debug view should parse final");
    require(cubey::projects::terrain::terrain_debug_view_from_name("flow_accumulation") ==
                cubey::projects::terrain::TerrainDebugView::FlowAccumulation,
            "terrain debug view should accept underscore aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("flow_direction") ==
                cubey::projects::terrain::TerrainDebugView::FlowDirection,
            "terrain debug view should parse flow direction aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("drainage_potential") ==
                cubey::projects::terrain::TerrainDebugView::DrainagePotential,
            "terrain debug view should parse drainage potential aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("river_trunk") ==
                cubey::projects::terrain::TerrainDebugView::RiverTrunk,
            "terrain debug view should parse river trunk aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("tributaries") ==
                cubey::projects::terrain::TerrainDebugView::Tributaries,
            "terrain debug view should parse tributaries");
    require(cubey::projects::terrain::terrain_debug_view_from_name("sink_mask") ==
                cubey::projects::terrain::TerrainDebugView::SinkMask,
            "terrain debug view should parse sink mask aliases");
    require_throws(
        [] {
            static_cast<void>(
                cubey::projects::terrain::terrain_debug_view_from_name("watershed-fixture"));
        },
        "terrain debug view should reject unknown names");

    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 33;
    config.grid_height = 33;
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_terrain_debug_export_test.png";
    std::filesystem::remove(output);
    cubey::projects::terrain::write_terrain_debug_png(
        product, cubey::projects::terrain::TerrainDebugView::Final, output);
    require(std::filesystem::file_size(output) > 64U,
            "terrain debug export should write a non-empty PNG");
    std::filesystem::remove(output);
}

void test_terrain_debug_export_writes_review_set() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 129;
    config.grid_height = 129;
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "cubey_terrain_debug_review_set_test";
    std::filesystem::remove_all(output_dir);
    cubey::projects::terrain::write_terrain_debug_review_pngs(product, output_dir);

    require(!cubey::projects::terrain::terrain_debug_review_views().empty(),
            "terrain debug review views should be listed");
    for (const cubey::projects::terrain::TerrainDebugView view :
         cubey::projects::terrain::terrain_debug_review_views()) {
        const std::filesystem::path output =
            output_dir / (std::string(cubey::projects::terrain::terrain_debug_view_name(view)) +
                          ".png");
        require(std::filesystem::file_size(output) > 64U,
                "terrain debug review export should write each PNG");
    }

    std::filesystem::remove_all(output_dir);
}

} // namespace

int main() {
    test_terrain_region_config_defaults();
    test_terrain_product_emits_required_fields();
    test_terrain_product_has_useful_ranges();
    test_terrain_product_is_deterministic();
    test_terrain_materials_and_vegetation_are_bounded();
    test_terrain_river_network_has_continuous_active_channels();
    test_terrain_river_core_avoids_long_grid_aligned_runs();
    test_terrain_river_uses_larger_routing_context();
    test_terrain_debug_export_writes_png();
    test_terrain_debug_export_writes_review_set();
    return 0;
}
