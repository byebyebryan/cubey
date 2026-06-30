#include "terrain_debug_export.h"
#include "terrain_generator.h"
#include "terrain_preview_config.h"
#include "terrain_preview_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

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

[[nodiscard]] float average_where(const cubey::procedural::ScalarField2D& values,
                                  const cubey::procedural::ScalarField2D& mask,
                                  float min_mask, float max_mask = 1.0F) {
    const cubey::procedural::Grid2DDesc& desc = values.desc();
    double sum = 0.0;
    std::size_t count = 0U;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float mask_value = mask.at(x, y);
            if (mask_value < min_mask || mask_value > max_mask) {
                continue;
            }
            sum += static_cast<double>(values.at(x, y));
            ++count;
        }
    }
    if (count == 0U) {
        throw std::runtime_error("average_where mask selected no samples");
    }
    return static_cast<float>(sum / static_cast<double>(count));
}

[[nodiscard]] std::size_t active_bounds_area(const cubey::procedural::ScalarField2D& field,
                                             float threshold) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    std::uint32_t min_x = desc.width;
    std::uint32_t min_y = desc.height;
    std::uint32_t max_x = 0U;
    std::uint32_t max_y = 0U;
    bool found = false;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            if (field.at(x, y) < threshold) {
                continue;
            }
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
            found = true;
        }
    }
    if (!found) {
        return 0U;
    }
    return static_cast<std::size_t>(max_x - min_x + 1U) *
           static_cast<std::size_t>(max_y - min_y + 1U);
}

[[nodiscard]] std::size_t count_active_coarse_tiles(
    const cubey::procedural::ScalarField2D& field, float threshold, std::uint32_t tile_count) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    if (tile_count == 0U) {
        return 0U;
    }

    std::size_t active_tiles = 0U;
    for (std::uint32_t tile_y = 0; tile_y < tile_count; ++tile_y) {
        const std::uint32_t y0 =
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(tile_y) * desc.height) /
                                       tile_count);
        const std::uint32_t y1 = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(tile_y + 1U) * desc.height) / tile_count);
        for (std::uint32_t tile_x = 0; tile_x < tile_count; ++tile_x) {
            const std::uint32_t x0 =
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(tile_x) * desc.width) /
                                           tile_count);
            const std::uint32_t x1 = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(tile_x + 1U) * desc.width) / tile_count);
            bool has_active_sample = false;
            for (std::uint32_t y = y0; y < y1 && !has_active_sample; ++y) {
                for (std::uint32_t x = x0; x < x1; ++x) {
                    if (field.at(x, y) >= threshold) {
                        has_active_sample = true;
                        break;
                    }
                }
            }
            active_tiles += has_active_sample ? 1U : 0U;
        }
    }
    return active_tiles;
}

[[nodiscard]] std::size_t max_active_window_samples(
    const cubey::procedural::ScalarField2D& field, float threshold, std::uint32_t window_size) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    if (window_size == 0U || window_size > desc.width || window_size > desc.height) {
        return 0U;
    }

    const std::uint32_t integral_width = desc.width + 1U;
    std::vector<std::uint32_t> integral(
        static_cast<std::size_t>(integral_width) * static_cast<std::size_t>(desc.height + 1U),
        0U);
    const auto integral_index = [integral_width](std::uint32_t x, std::uint32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(integral_width) +
               static_cast<std::size_t>(x);
    };
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        std::uint32_t row_count = 0U;
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            row_count += field.at(x, y) >= threshold ? 1U : 0U;
            integral[integral_index(x + 1U, y + 1U)] =
                integral[integral_index(x + 1U, y)] + row_count;
        }
    }

    std::size_t best = 0U;
    for (std::uint32_t y = 0; y + window_size <= desc.height; ++y) {
        for (std::uint32_t x = 0; x + window_size <= desc.width; ++x) {
            const std::uint32_t x1 = x + window_size;
            const std::uint32_t y1 = y + window_size;
            const std::uint32_t count =
                integral[integral_index(x1, y1)] - integral[integral_index(x, y1)] -
                integral[integral_index(x1, y)] + integral[integral_index(x, y)];
            best = std::max(best, static_cast<std::size_t>(count));
        }
    }
    return best;
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

[[nodiscard]] std::size_t count_active_endpoint_samples(
    const cubey::procedural::ScalarField2D& field, float threshold) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    std::size_t count = 0U;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            if (field.at(x, y) < threshold) {
                continue;
            }
            std::size_t neighbor_count = 0U;
            for (int oy = -1; oy <= 1; ++oy) {
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
                        ++neighbor_count;
                    }
                }
            }
            if (neighbor_count <= 1U) {
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] std::size_t edge_band_touch_count(const cubey::procedural::ScalarField2D& field,
                                                float threshold, std::uint32_t band_cells) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    const std::uint32_t band = std::min(band_cells, std::min(desc.width, desc.height) - 1U);
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x <= band; ++x) {
            left = left || field.at(x, y) >= threshold;
            right = right || field.at(desc.width - 1U - x, y) >= threshold;
        }
    }
    for (std::uint32_t x = 0; x < desc.width; ++x) {
        for (std::uint32_t y = 0; y <= band; ++y) {
            top = top || field.at(x, y) >= threshold;
            bottom = bottom || field.at(x, desc.height - 1U - y) >= threshold;
        }
    }
    return static_cast<std::size_t>(left) + static_cast<std::size_t>(right) +
           static_cast<std::size_t>(top) + static_cast<std::size_t>(bottom);
}

[[nodiscard]] std::size_t largest_active_component_size(
    const cubey::procedural::ScalarField2D& field, float threshold) {
    const cubey::procedural::Grid2DDesc& desc = field.desc();
    std::vector<bool> visited(field.sample_count(), false);
    std::size_t best = 0U;
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::size_t start = field.index(x, y);
            if (visited[start] || field.at(x, y) < threshold) {
                continue;
            }

            std::size_t count = 0U;
            std::vector<std::size_t> stack{start};
            visited[start] = true;
            while (!stack.empty()) {
                const std::size_t current = stack.back();
                stack.pop_back();
                ++count;
                const int cx = static_cast<int>(current % desc.width);
                const int cy = static_cast<int>(current / desc.width);
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }
                        const int nx = cx + ox;
                        const int ny = cy + oy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(desc.width) ||
                            ny >= static_cast<int>(desc.height)) {
                            continue;
                        }
                        const std::size_t neighbor = field.index(static_cast<std::uint32_t>(nx),
                                                                 static_cast<std::uint32_t>(ny));
                        if (visited[neighbor] ||
                            field.at(static_cast<std::uint32_t>(nx),
                                     static_cast<std::uint32_t>(ny)) < threshold) {
                            continue;
                        }
                        visited[neighbor] = true;
                        stack.push_back(neighbor);
                    }
                }
            }
            best = std::max(best, count);
        }
    }
    return best;
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

    const std::array<std::string_view, 43> required_fields{
        cubey::projects::terrain::kTerrainFieldHeightM,
        cubey::projects::terrain::kTerrainFieldPreProcessHeightM,
        cubey::projects::terrain::kTerrainFieldBaseElevation,
        cubey::projects::terrain::kTerrainFieldBroadRelief,
        cubey::projects::terrain::kTerrainFieldMountainRangeSpine,
        cubey::projects::terrain::kTerrainFieldMountainEnvelope,
        cubey::projects::terrain::kTerrainFieldMountainSupport,
        cubey::projects::terrain::kTerrainFieldMountainRidgeHierarchy,
        cubey::projects::terrain::kTerrainFieldRidgeSupport,
        cubey::projects::terrain::kTerrainFieldMountainPeakCandidates,
        cubey::projects::terrain::kTerrainFieldMountainPeakAnchors,
        cubey::projects::terrain::kTerrainFieldMountainPeakProminence,
        cubey::projects::terrain::kTerrainFieldPeakSupport,
        cubey::projects::terrain::kTerrainFieldMountainRidgeSkeleton,
        cubey::projects::terrain::kTerrainFieldMountainRidgeInfluence,
        cubey::projects::terrain::kTerrainFieldMountainUplift,
        cubey::projects::terrain::kTerrainFieldRidgeUplift,
        cubey::projects::terrain::kTerrainFieldPeakUplift,
        cubey::projects::terrain::kTerrainFieldDetailResidual,
        cubey::projects::terrain::kTerrainFieldSlope,
        cubey::projects::terrain::kTerrainFieldCurvature,
        cubey::projects::terrain::kTerrainFieldLocalRelief,
        cubey::projects::terrain::kTerrainFieldDrainagePotential,
        cubey::projects::terrain::kTerrainFieldRoutingFillDelta,
        cubey::projects::terrain::kTerrainFieldFlowDirection,
        cubey::projects::terrain::kTerrainFieldFlowAccumulation,
        cubey::projects::terrain::kTerrainFieldStreamOrder,
        cubey::projects::terrain::kTerrainFieldRiverMask,
        cubey::projects::terrain::kTerrainFieldRiverTrunk,
        cubey::projects::terrain::kTerrainFieldTributaries,
        cubey::projects::terrain::kTerrainFieldRiverGraphPlan,
        cubey::projects::terrain::kTerrainFieldRiverGraphDischarge,
        cubey::projects::terrain::kTerrainFieldSinkMask,
        cubey::projects::terrain::kTerrainFieldChannelWidth,
        cubey::projects::terrain::kTerrainFieldValleyWidth,
        cubey::projects::terrain::kTerrainFieldChannelIncision,
        cubey::projects::terrain::kTerrainFieldValleyIncision,
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
    const auto& fill_delta = field(product, cubey::projects::terrain::kTerrainFieldRoutingFillDelta);
    const cubey::procedural::ScalarFieldStats fill_delta_stats = fill_delta.summarize();
    const cubey::procedural::ScalarFieldStats sink =
        field(product, cubey::projects::terrain::kTerrainFieldSinkMask).summarize();
    require(flow.max > flow.min, "flow accumulation should vary");
    require(stream_order.max >= 3.0F, "stream order should identify larger drainage trunks");
    require(drainage.max > drainage.min, "drainage potential should vary");
    require(fill_delta_stats.min >= 0.0F, "routing fill delta should be non-negative");
    const std::size_t repaired_count = count_active_samples(fill_delta, 0.001F);
    require(repaired_count > 0U, "routing repair should expose at least one repaired sample");
    require(repaired_count * 100U < fill_delta.sample_count() * 75U,
            "routing repair should not fill most of the visible patch");
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

void test_terrain_stress_recipe_expands_river_network() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 257;
    config.grid_height = 257;
    const cubey::projects::terrain::TerrainRegionProduct baseline =
        cubey::projects::terrain::generate_terrain_region(config);

    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRiverStress);
    const cubey::projects::terrain::TerrainRegionProduct stress =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& baseline_river =
        field(baseline, cubey::projects::terrain::kTerrainFieldRiverMask);
    const auto& stress_river = field(stress, cubey::projects::terrain::kTerrainFieldRiverMask);
    const std::size_t baseline_samples = count_active_samples(baseline_river, 0.30F);
    const std::size_t stress_samples = count_active_samples(stress_river, 0.30F);

    require(stress.config.recipe_id ==
                cubey::projects::terrain::kTerrainRecipeTemperateMountainRiverStress,
            "terrain stress recipe should preserve the requested recipe id");
    require(stress.summary.content_hash != baseline.summary.content_hash,
            "terrain stress recipe should produce a distinct product hash");
    if (stress_samples * 100U <= baseline_samples * 125U) {
        throw std::runtime_error(
            "terrain stress recipe should expand active river network coverage: baseline=" +
            std::to_string(baseline_samples) + " stress=" + std::to_string(stress_samples));
    }
    require(stress.summary.river_coverage > baseline.summary.river_coverage,
            "terrain stress recipe should increase high-strength river coverage");
    require(stress.summary.river_coverage < 0.25F,
            "terrain stress recipe should not flood the whole review patch");
}

void test_terrain_mountain_range_stress_recipe_exposes_mountain_driver() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 257;
    config.grid_height = 257;
    const cubey::projects::terrain::TerrainRegionProduct baseline =
        cubey::projects::terrain::generate_terrain_region(config);

    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress);
    const cubey::projects::terrain::TerrainRegionProduct mountain =
        cubey::projects::terrain::generate_terrain_region(config);
    const cubey::projects::terrain::TerrainRegionProduct repeat =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& baseline_mountain_uplift =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainUplift);
    const auto& baseline_peak_uplift =
        field(baseline, cubey::projects::terrain::kTerrainFieldPeakUplift);
    const auto& baseline_range_spine =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainRangeSpine);
    const auto& baseline_envelope =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainEnvelope);
    const auto& baseline_peak_candidates =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainPeakCandidates);
    const auto& baseline_peak_anchors =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainPeakAnchors);
    const auto& baseline_peak_prominence =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainPeakProminence);
    const auto& baseline_ridge_skeleton =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainRidgeSkeleton);
    const auto& baseline_ridge_influence =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainRidgeInfluence);
    const auto& range_spine =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainRangeSpine);
    const auto& envelope =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainEnvelope);
    const auto& mountain_support =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainSupport);
    const auto& ridge_hierarchy =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainRidgeHierarchy);
    const auto& ridge_support =
        field(mountain, cubey::projects::terrain::kTerrainFieldRidgeSupport);
    const auto& peak_candidates =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainPeakCandidates);
    const auto& peak_anchors =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainPeakAnchors);
    const auto& peak_prominence =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainPeakProminence);
    const auto& peak_support =
        field(mountain, cubey::projects::terrain::kTerrainFieldPeakSupport);
    const auto& ridge_skeleton =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainRidgeSkeleton);
    const auto& ridge_influence =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainRidgeInfluence);
    const auto& mountain_uplift =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainUplift);
    const auto& ridge_uplift =
        field(mountain, cubey::projects::terrain::kTerrainFieldRidgeUplift);
    const auto& peak_uplift =
        field(mountain, cubey::projects::terrain::kTerrainFieldPeakUplift);
    const auto& height = field(mountain, cubey::projects::terrain::kTerrainFieldHeightM);

    require(mountain.config.recipe_id ==
                cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress,
            "terrain mountain recipe should preserve the requested recipe id");
    require(mountain.summary.content_hash == repeat.summary.content_hash,
            "terrain mountain recipe should be deterministic");
    require(mountain.summary.content_hash != baseline.summary.content_hash,
            "terrain mountain recipe should produce a distinct product hash");
    require(baseline_mountain_uplift.summarize().max == 0.0F,
            "default terrain recipe should keep broad mountain uplift disabled");
    require(baseline_peak_uplift.summarize().max == 0.0F,
            "default terrain recipe should keep peak uplift disabled");
    require(baseline_range_spine.summarize().max == 0.0F,
            "default terrain recipe should keep mountain range spine disabled");
    require(baseline_envelope.summarize().max == 0.0F,
            "default terrain recipe should keep mountain envelope disabled");
    require(baseline_peak_candidates.summarize().max == 0.0F,
            "default terrain recipe should keep mountain peak candidates disabled");
    require(baseline_peak_anchors.summarize().max == 0.0F,
            "default terrain recipe should keep mountain peak anchors disabled");
    require(baseline_peak_prominence.summarize().max == 0.0F,
            "default terrain recipe should keep mountain peak prominence disabled");
    require(baseline_ridge_skeleton.summarize().max == 0.0F,
            "default terrain recipe should keep mountain ridge skeleton disabled");
    require(baseline_ridge_influence.summarize().max == 0.0F,
            "default terrain recipe should keep mountain ridge influence disabled");

    const std::size_t total_samples = mountain_support.sample_count();
    const std::size_t range_spine_samples = count_active_samples(range_spine, 0.30F);
    const std::size_t envelope_samples = count_active_samples(envelope, 0.30F);
    const std::size_t mountain_support_samples = count_active_samples(mountain_support, 0.30F);
    const std::size_t ridge_hierarchy_samples = count_active_samples(ridge_hierarchy, 0.30F);
    const std::size_t ridge_support_samples = count_active_samples(ridge_support, 0.30F);
    const std::size_t peak_candidate_samples = count_active_samples(peak_candidates, 0.20F);
    const std::size_t peak_anchor_samples = count_active_samples(peak_anchors, 0.80F);
    const std::size_t peak_prominence_samples = count_active_samples(peak_prominence, 0.20F);
    const std::size_t peak_support_samples = count_active_samples(peak_support, 0.20F);
    const std::size_t ridge_skeleton_samples = count_active_samples(ridge_skeleton, 0.30F);
    const std::size_t ridge_influence_samples = count_active_samples(ridge_influence, 0.20F);
    if (range_spine_samples * 100U < total_samples * 6U ||
        range_spine_samples * 100U > total_samples * 62U) {
        throw std::runtime_error(
            "terrain range spine should be broad but not full-map: samples=" +
            std::to_string(range_spine_samples) + " total=" + std::to_string(total_samples));
    }
    if (envelope_samples * 100U < total_samples * 10U ||
        envelope_samples * 100U > total_samples * 82U) {
        throw std::runtime_error(
            "terrain mountain envelope should be broad but bounded: samples=" +
            std::to_string(envelope_samples) + " total=" + std::to_string(total_samples));
    }
    if (mountain_support_samples * 100U < total_samples * 12U ||
        mountain_support_samples * 100U > total_samples * 78U) {
        throw std::runtime_error(
            "terrain mountain support should be broad but bounded: samples=" +
            std::to_string(mountain_support_samples) + " total=" + std::to_string(total_samples));
    }
    if (ridge_hierarchy_samples * 100U < total_samples * 3U ||
        ridge_hierarchy_samples * 100U > total_samples * 52U) {
        throw std::runtime_error(
            "terrain ridge hierarchy should be visible but ranked: samples=" +
            std::to_string(ridge_hierarchy_samples) + " total=" + std::to_string(total_samples));
    }
    if (ridge_support_samples * 100U < total_samples * 3U ||
        ridge_support_samples * 100U > total_samples * 58U) {
        throw std::runtime_error(
            "terrain ridge support should be visible but not full-map: samples=" +
            std::to_string(ridge_support_samples) + " total=" + std::to_string(total_samples));
    }
    if (peak_candidate_samples < 32U || peak_candidate_samples * 100U > total_samples * 30U) {
        throw std::runtime_error(
            "terrain peak candidates should be sparse and attached: samples=" +
            std::to_string(peak_candidate_samples) + " total=" + std::to_string(total_samples));
    }
    if (peak_anchor_samples < 3U || peak_anchor_samples > 160U) {
        throw std::runtime_error(
            "terrain peak anchors should be sparse deterministic points: samples=" +
            std::to_string(peak_anchor_samples));
    }
    if (peak_prominence_samples * 100U < total_samples * 2U ||
        peak_prominence_samples * 100U > total_samples * 42U) {
        throw std::runtime_error(
            "terrain peak prominence should build around anchors without filling the map: samples=" +
            std::to_string(peak_prominence_samples) + " total=" + std::to_string(total_samples));
    }
    if (peak_support_samples < 32U || peak_support_samples * 100U > total_samples * 34U) {
        throw std::runtime_error(
            "terrain peak support should produce localized summit accents: samples=" +
            std::to_string(peak_support_samples) + " total=" + std::to_string(total_samples));
    }
    if (ridge_skeleton_samples * 100U < total_samples / 2U ||
        ridge_skeleton_samples * 100U > total_samples * 14U) {
        throw std::runtime_error(
            "terrain ridge skeleton should be sparse structural lines: samples=" +
            std::to_string(ridge_skeleton_samples) + " total=" + std::to_string(total_samples));
    }
    if (ridge_influence_samples * 100U < total_samples * 2U ||
        ridge_influence_samples * 100U > total_samples * 62U) {
        throw std::runtime_error(
            "terrain ridge influence should broaden skeletons without filling the map: samples=" +
            std::to_string(ridge_influence_samples) + " total=" + std::to_string(total_samples));
    }

    require(mountain.summary.height.span > baseline.summary.height.span,
            "terrain mountain recipe should increase height relief");
    require(mountain_uplift.summarize().max > 160.0F,
            "terrain mountain recipe should emit broad mountain uplift");
    require(ridge_uplift.summarize().max > 220.0F,
            "terrain mountain recipe should emit stronger ridge uplift");
    require(peak_uplift.summarize().max > 160.0F,
            "terrain mountain recipe should emit dominant peak uplift");

    const float lowland_average_height = average_where(height, mountain_support, 0.0F, 0.12F);
    const float mountain_average_height = average_where(height, mountain_support, 0.42F);
    const float peak_average_height = average_where(height, peak_support, 0.36F);
    require(mountain_average_height > lowland_average_height + 220.0F,
            "terrain mountain support should build above lowland samples");
    require(peak_average_height > mountain_average_height + 120.0F,
            "terrain peak support should build above broad mountain samples");
}

void test_terrain_review_river_coverage_is_meaningful() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 513;
    config.grid_height = 513;
    const cubey::projects::terrain::TerrainRegionProduct baseline =
        cubey::projects::terrain::generate_terrain_region(config);

    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRiverStress);
    const cubey::projects::terrain::TerrainRegionProduct stress =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& baseline_river =
        field(baseline, cubey::projects::terrain::kTerrainFieldRiverMask);
    const auto& stress_river = field(stress, cubey::projects::terrain::kTerrainFieldRiverMask);
    const auto& baseline_trunk =
        field(baseline, cubey::projects::terrain::kTerrainFieldRiverTrunk);
    const auto& stress_trunk = field(stress, cubey::projects::terrain::kTerrainFieldRiverTrunk);
    const auto& stress_tributaries =
        field(stress, cubey::projects::terrain::kTerrainFieldTributaries);
    const auto& stress_graph_plan =
        field(stress, cubey::projects::terrain::kTerrainFieldRiverGraphPlan);
    const auto& stress_graph_discharge =
        field(stress, cubey::projects::terrain::kTerrainFieldRiverGraphDischarge);
    constexpr float kVisibleNetworkThreshold = 0.001F;
    const std::size_t baseline_samples = count_active_samples(baseline_river, 0.30F);
    const std::size_t stress_samples = count_active_samples(stress_river, 0.30F);
    const std::size_t baseline_trunk_samples = count_active_samples(baseline_trunk, 0.30F);
    const std::size_t stress_trunk_samples = count_active_samples(stress_trunk, 0.30F);
    const std::size_t stress_tributary_samples = count_active_samples(stress_tributaries, 0.30F);
    const std::size_t stress_graph_samples = count_active_samples(stress_graph_plan, 0.10F);
    const std::size_t stress_endpoint_samples =
        count_active_endpoint_samples(stress_tributaries, 0.30F);
    const std::size_t stress_trunk_largest_component =
        largest_active_component_size(stress_trunk, 0.30F);
    const std::size_t baseline_footprint =
        active_bounds_area(baseline_river, kVisibleNetworkThreshold);
    const std::size_t stress_footprint =
        active_bounds_area(stress_river, kVisibleNetworkThreshold);
    const std::size_t stress_largest_component =
        largest_active_component_size(stress_river, kVisibleNetworkThreshold);
    const std::size_t stress_edge_touches =
        edge_band_touch_count(stress_river, kVisibleNetworkThreshold, 8U);
    const std::size_t stress_trunk_edge_touches = edge_band_touch_count(stress_trunk, 0.30F, 8U);
    const std::size_t stress_coarse_tiles =
        count_active_coarse_tiles(stress_river, kVisibleNetworkThreshold, 5U);
    const std::size_t stress_trunk_coarse_tiles =
        count_active_coarse_tiles(stress_trunk, 0.30F, 5U);
    const std::size_t stress_graph_coarse_tiles =
        count_active_coarse_tiles(stress_graph_plan, 0.10F, 5U);
    const std::size_t total_samples = baseline_river.sample_count();

    require(baseline_samples >= 1'200U,
            "terrain default review river should cover more than a tiny center segment");
    if (stress_samples * 100U < baseline_samples * 175U) {
        throw std::runtime_error(
            "terrain stress river should substantially expand high-strength network coverage: "
            "baseline=" +
            std::to_string(baseline_samples) + " stress=" + std::to_string(stress_samples));
    }
    if (stress_graph_samples < 96U) {
        throw std::runtime_error(
            "terrain stress graph plan should expose non-trivial source topology: samples=" +
            std::to_string(stress_graph_samples));
    }
    if (stress_graph_coarse_tiles < 8U) {
        throw std::runtime_error(
            "terrain stress graph plan should span multiple coarse regions: tiles=" +
            std::to_string(stress_graph_coarse_tiles));
    }
    const cubey::procedural::ScalarFieldStats graph_discharge_stats =
        stress_graph_discharge.summarize();
    require(graph_discharge_stats.max > graph_discharge_stats.min,
            "terrain stress graph discharge should vary");
    require(graph_discharge_stats.max <= 1.0F,
            "terrain stress graph discharge should stay normalized");
    if (stress_footprint * 100U < total_samples * 5U) {
        throw std::runtime_error(
            "terrain stress review river should span a non-tiny network footprint: baseline=" +
            std::to_string(baseline_footprint) + " stress=" +
            std::to_string(stress_footprint) + " total=" + std::to_string(total_samples));
    }
    const std::size_t stress_visible_network_samples =
        count_active_samples(stress_river, kVisibleNetworkThreshold);
    if (stress_largest_component * 100U < stress_visible_network_samples * 80U) {
        throw std::runtime_error(
            "terrain stress review river should be dominated by one connected network: largest=" +
            std::to_string(stress_largest_component) + " active=" +
            std::to_string(stress_visible_network_samples) + " footprint=" +
            std::to_string(stress_footprint));
    }
    if (stress_trunk_largest_component * 100U < stress_trunk_samples * 90U) {
        throw std::runtime_error(
            "terrain stress trunk should be dominated by one continuous trunk component: largest=" +
            std::to_string(stress_trunk_largest_component) + " active=" +
            std::to_string(stress_trunk_samples));
    }
    if (stress_trunk_edge_touches < 1U) {
        throw std::runtime_error(
            "terrain stress trunk should reach a visible crop edge without disconnecting: edges=" +
            std::to_string(stress_trunk_edge_touches));
    }
    if (stress_trunk_coarse_tiles < 4U) {
        throw std::runtime_error(
            "terrain stress trunk major channels should occupy multiple coarse basin regions: "
            "tiles=" +
            std::to_string(stress_trunk_coarse_tiles));
    }
    if (stress_edge_touches < 2U) {
        throw std::runtime_error(
            "terrain stress river should reach multiple visible crop edges: edges=" +
            std::to_string(stress_edge_touches));
    }
    if (stress_coarse_tiles < 7U) {
        throw std::runtime_error(
            "terrain stress river should occupy a broad coarse basin footprint: tiles=" +
            std::to_string(stress_coarse_tiles));
    }
    require(baseline_samples * 100U < total_samples * 8U,
            "terrain default review river should not flood the patch");
    require(stress_samples * 100U < total_samples * 12U,
            "terrain stress review river should not flood the patch");
    if (stress_trunk_samples * 100U < baseline_trunk_samples * 90U) {
        throw std::runtime_error(
            "terrain stress trunk should retain most baseline high-order trunk coverage: baseline=" +
            std::to_string(baseline_trunk_samples) + " stress=" +
            std::to_string(stress_trunk_samples));
    }
    if (stress_trunk_samples * 100U < stress_samples * 35U) {
        throw std::runtime_error(
            "terrain stress trunk should carry a visible major-channel share of active network: "
            "trunk=" +
            std::to_string(stress_trunk_samples) + " river=" +
            std::to_string(stress_samples));
    }
    if (stress_trunk_samples * 100U < stress_tributary_samples * 25U) {
        throw std::runtime_error(
            "terrain stress trunk should stay visible within the broader tributary network: "
            "trunk=" +
            std::to_string(stress_trunk_samples) + " tributaries=" +
            std::to_string(stress_tributary_samples));
    }
    const std::size_t stress_trunk_max_window =
        max_active_window_samples(stress_trunk, 0.30F, 64U);
    if (stress_trunk_max_window * 100U > stress_trunk_samples * 60U) {
        throw std::runtime_error(
            "terrain stress trunk should not pack most promoted branches into one corridor: "
            "window=" +
            std::to_string(stress_trunk_max_window) + " trunk=" +
            std::to_string(stress_trunk_samples));
    }
    if (stress_endpoint_samples * 100U > stress_tributary_samples * 8U) {
        throw std::runtime_error(
            "terrain stress tributaries should not collapse into hairy endpoints: endpoints=" +
            std::to_string(stress_endpoint_samples) + " active=" +
            std::to_string(stress_tributary_samples));
    }
    const std::size_t stress_tributary_axis_run =
        max_axis_aligned_active_run(stress_tributaries, 0.45F, 16U);
    const std::size_t stress_tributary_diagonal_run =
        max_diagonal_active_run(stress_tributaries, 0.45F, 16U);
    if (stress_tributary_axis_run > 44U) {
        throw std::runtime_error(
            "terrain stress tributaries should avoid long straight mid-strength runs: axis=" +
            std::to_string(stress_tributary_axis_run));
    }
    if (stress_tributary_diagonal_run > 50U) {
        throw std::runtime_error(
            "terrain stress tributaries should avoid long diagonal mid-strength runs: diagonal=" +
            std::to_string(stress_tributary_diagonal_run));
    }
    const std::size_t stress_axis_run =
        max_axis_aligned_active_run(stress_river, 0.80F, 16U);
    const std::size_t stress_diagonal_run =
        max_diagonal_active_run(stress_river, 0.80F, 16U);
    if (stress_axis_run > 36U) {
        throw std::runtime_error(
            "terrain stress river should avoid long straight high-strength runs: axis=" +
            std::to_string(stress_axis_run));
    }
    if (stress_diagonal_run > 42U) {
        throw std::runtime_error(
            "terrain stress river should avoid long diagonal high-strength runs: diagonal=" +
            std::to_string(stress_diagonal_run));
    }
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
    const std::size_t largest_river_component =
        largest_active_component_size(river_mask, 0.30F);
    require(connected_trunk_count * 100U >= trunk_count * 90U,
            "terrain river trunk samples should be locally continuous");
    require(connected_river_count * 100U >= river_count * 80U,
            "terrain river mask samples should be locally continuous");
    if (largest_river_component * 100U < river_count * 80U) {
        throw std::runtime_error(
            "terrain river mask should be dominated by one connected component: largest=" +
            std::to_string(largest_river_component) + " active=" + std::to_string(river_count));
    }

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
    const std::size_t trunk_axis_run =
        max_axis_aligned_active_run(trunk, 0.80F, kInteriorMargin);
    const std::size_t mask_axis_run =
        max_axis_aligned_active_run(river_mask, 0.80F, kInteriorMargin);
    const std::size_t trunk_diagonal_run =
        max_diagonal_active_run(trunk, 0.80F, kInteriorMargin);
    const std::size_t mask_diagonal_run =
        max_diagonal_active_run(river_mask, 0.80F, kInteriorMargin);
    if (trunk_axis_run > 18U) {
        throw std::runtime_error(
            "terrain river trunk should avoid long straight core runs: axis=" +
            std::to_string(trunk_axis_run));
    }
    if (mask_axis_run > 24U) {
        throw std::runtime_error(
            "terrain river mask should avoid long straight high-strength runs: axis=" +
            std::to_string(mask_axis_run));
    }
    if (trunk_diagonal_run > 24U) {
        throw std::runtime_error(
            "terrain river trunk should avoid long diagonal core runs: diagonal=" +
            std::to_string(trunk_diagonal_run));
    }
    if (mask_diagonal_run > 30U) {
        throw std::runtime_error(
            "terrain river mask should avoid long diagonal high-strength runs: diagonal=" +
            std::to_string(mask_diagonal_run));
    }
}

void test_terrain_river_uses_larger_routing_context() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 257;
    config.grid_height = 257;
    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRiverStress);
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& river_mask = field(product, cubey::projects::terrain::kTerrainFieldRiverMask);
    const auto& sink_mask = field(product, cubey::projects::terrain::kTerrainFieldSinkMask);

    require(edge_band_touch_count(river_mask, 0.08F, 8U) >= 2U,
            "terrain river mask should enter or leave multiple visible crop edges");
    require(edge_band_touch_count(river_mask, 0.20F, 8U) >= 1U,
            "terrain high-strength river mask should connect to a visible crop edge");

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
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain-relief") ==
                cubey::projects::terrain::TerrainDebugView::MountainRelief,
            "terrain debug view should parse mountain relief");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_relief") ==
                cubey::projects::terrain::TerrainDebugView::MountainRelief,
            "terrain debug view should parse mountain relief aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_range_spine") ==
                cubey::projects::terrain::TerrainDebugView::MountainRangeSpine,
            "terrain debug view should parse mountain range spine aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_envelope") ==
                cubey::projects::terrain::TerrainDebugView::MountainEnvelope,
            "terrain debug view should parse mountain envelope aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_support") ==
                cubey::projects::terrain::TerrainDebugView::MountainSupport,
            "terrain debug view should parse mountain support aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_ridge_hierarchy") ==
                cubey::projects::terrain::TerrainDebugView::MountainRidgeHierarchy,
            "terrain debug view should parse mountain ridge hierarchy aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("ridge_support") ==
                cubey::projects::terrain::TerrainDebugView::RidgeSupport,
            "terrain debug view should parse ridge support aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_peak_candidates") ==
                cubey::projects::terrain::TerrainDebugView::MountainPeakCandidates,
            "terrain debug view should parse mountain peak candidates aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_peak_anchors") ==
                cubey::projects::terrain::TerrainDebugView::MountainPeakAnchors,
            "terrain debug view should parse mountain peak anchors aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_peak_prominence") ==
                cubey::projects::terrain::TerrainDebugView::MountainPeakProminence,
            "terrain debug view should parse mountain peak prominence aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("peak_support") ==
                cubey::projects::terrain::TerrainDebugView::PeakSupport,
            "terrain debug view should parse peak support aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_ridge_skeleton") ==
                cubey::projects::terrain::TerrainDebugView::MountainRidgeSkeleton,
            "terrain debug view should parse mountain ridge skeleton aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_ridge_influence") ==
                cubey::projects::terrain::TerrainDebugView::MountainRidgeInfluence,
            "terrain debug view should parse mountain ridge influence aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_uplift") ==
                cubey::projects::terrain::TerrainDebugView::MountainUplift,
            "terrain debug view should parse mountain uplift aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("peak_uplift") ==
                cubey::projects::terrain::TerrainDebugView::PeakUplift,
            "terrain debug view should parse peak uplift aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("flow_accumulation") ==
                cubey::projects::terrain::TerrainDebugView::FlowAccumulation,
            "terrain debug view should accept underscore aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("flow_direction") ==
                cubey::projects::terrain::TerrainDebugView::FlowDirection,
            "terrain debug view should parse flow direction aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("drainage_potential") ==
                cubey::projects::terrain::TerrainDebugView::DrainagePotential,
            "terrain debug view should parse drainage potential aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("routing_fill_delta") ==
                cubey::projects::terrain::TerrainDebugView::RoutingFillDelta,
            "terrain debug view should parse routing fill delta aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("river_trunk") ==
                cubey::projects::terrain::TerrainDebugView::RiverTrunk,
            "terrain debug view should parse river trunk aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("tributaries") ==
                cubey::projects::terrain::TerrainDebugView::Tributaries,
            "terrain debug view should parse tributaries");
    require(cubey::projects::terrain::terrain_debug_view_from_name("river_graph_plan") ==
                cubey::projects::terrain::TerrainDebugView::RiverGraphPlan,
            "terrain debug view should parse river graph plan aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("river_graph_discharge") ==
                cubey::projects::terrain::TerrainDebugView::RiverGraphDischarge,
            "terrain debug view should parse river graph discharge aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("sink_mask") ==
                cubey::projects::terrain::TerrainDebugView::SinkMask,
            "terrain debug view should parse sink mask aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("channel_width") ==
                cubey::projects::terrain::TerrainDebugView::ChannelWidth,
            "terrain debug view should parse channel width aliases");
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

    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress);
    const cubey::projects::terrain::TerrainRegionProduct mountain_product =
        cubey::projects::terrain::generate_terrain_region(config);
    const std::filesystem::path mountain_output =
        std::filesystem::temp_directory_path() / "cubey_terrain_mountain_relief_export_test.png";
    std::filesystem::remove(mountain_output);
    cubey::projects::terrain::write_terrain_debug_png(
        mountain_product, cubey::projects::terrain::TerrainDebugView::MountainRelief,
        mountain_output);
    require(std::filesystem::file_size(mountain_output) > 64U,
            "terrain mountain relief debug export should write a non-empty PNG");
    std::filesystem::remove(mountain_output);
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

void test_terrain_preview_config_uses_run_config_controls() {
    cubey::RunConfig run_config;
    cubey::projects::terrain::TerrainPreviewConfig preview =
        cubey::projects::terrain::terrain_preview_config_from_run_config(run_config);
    require(preview.region.recipe_id ==
                cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress,
            "terrain preview should default to the mountain stress recipe");
    require(preview.camera_preset ==
                cubey::projects::terrain::TerrainPreviewCameraPreset::Oblique,
            "terrain preview should default to the oblique camera");
    require(preview.color_mode == cubey::projects::terrain::TerrainPreviewColorMode::Material,
            "terrain preview should default to material color");
    require(preview.vertical_scale == cubey::projects::terrain::kTerrainPreviewDefaultVerticalScale,
            "terrain preview should default to the documented vertical scale");

    run_config.grid.width = 33;
    run_config.grid.height = 65;
    run_config.terrain.seed = 99U;
    run_config.terrain.seed_set = true;
    run_config.terrain.cell_size = 64.0F;
    run_config.terrain.recipe =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRiverStress);
    run_config.terrain.camera_preset = "profile";
    run_config.terrain.preview_color = "height";
    run_config.terrain.vertical_scale = 0.55F;
    preview = cubey::projects::terrain::terrain_preview_config_from_run_config(run_config);

    require(preview.region.grid_width == 33 && preview.region.grid_height == 65,
            "terrain preview should use shared grid dimensions");
    require(preview.region.seed == 99U, "terrain preview should use terrain seed");
    require(preview.region.cell_size_m == 64.0F, "terrain preview should use terrain cell size");
    require(preview.region.recipe_id ==
                cubey::projects::terrain::kTerrainRecipeTemperateMountainRiverStress,
            "terrain preview should use the selected terrain recipe");
    require(preview.camera_preset ==
                cubey::projects::terrain::TerrainPreviewCameraPreset::Profile,
            "terrain preview should parse the profile camera");
    require(preview.color_mode == cubey::projects::terrain::TerrainPreviewColorMode::Height,
            "terrain preview should parse the height color mode");
    require(preview.vertical_scale == 0.55F,
            "terrain preview should use explicit vertical scale");

    run_config.terrain.camera_preset = "telephoto";
    require_throws(
        [&run_config] {
            static_cast<void>(
                cubey::projects::terrain::terrain_preview_config_from_run_config(run_config));
        },
        "terrain preview should reject unknown camera presets");
    run_config.terrain.camera_preset = "profile";
    run_config.terrain.preview_color = "thermal";
    require_throws(
        [&run_config] {
            static_cast<void>(
                cubey::projects::terrain::terrain_preview_config_from_run_config(run_config));
        },
        "terrain preview should reject unknown color modes");
}

void test_terrain_preview_mesh_represents_heightfield() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 33;
    config.grid_height = 33;
    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress);
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    cubey::projects::terrain::TerrainPreviewConfig preview;
    preview.region = config;
    preview.vertical_scale = 0.5F;
    const cubey::projects::terrain::TerrainPreviewMeshData mesh =
        cubey::projects::terrain::make_terrain_preview_mesh(product, preview);

    const std::size_t sample_count =
        static_cast<std::size_t>(config.grid_width) * static_cast<std::size_t>(config.grid_height);
    const std::size_t quad_count =
        static_cast<std::size_t>(config.grid_width - 1U) *
        static_cast<std::size_t>(config.grid_height - 1U);
    require(mesh.vertices.size() == sample_count,
            "terrain preview mesh should emit one vertex per height sample");
    require(mesh.indices.size() == quad_count * 6U,
            "terrain preview mesh should emit two indexed triangles per quad");
    require(cubey::projects::terrain::terrain_preview_triangle_count(mesh) == quad_count * 2U,
            "terrain preview mesh should report triangle count");

    const cubey::render::VertexPositionColorNormal& first = mesh.vertices.front();
    require(std::isfinite(first.position[0]) && std::isfinite(first.position[1]) &&
                std::isfinite(first.position[2]),
            "terrain preview mesh vertex positions should be finite");
    require(std::isfinite(first.normal[0]) && std::isfinite(first.normal[1]) &&
                std::isfinite(first.normal[2]),
            "terrain preview mesh normals should be finite");
    require(first.color[0] >= 0.0F && first.color[0] <= 1.0F && first.color[1] >= 0.0F &&
                first.color[1] <= 1.0F && first.color[2] >= 0.0F && first.color[2] <= 1.0F,
            "terrain preview mesh colors should be normalized");

    require_throws(
        [&product, &preview] {
            cubey::projects::terrain::TerrainPreviewConfig bad_preview = preview;
            bad_preview.vertical_scale = 0.0F;
            static_cast<void>(
                cubey::projects::terrain::make_terrain_preview_mesh(product, bad_preview));
        },
        "terrain preview mesh should reject invalid vertical scale");
}

} // namespace

int main() {
    test_terrain_region_config_defaults();
    test_terrain_product_emits_required_fields();
    test_terrain_product_has_useful_ranges();
    test_terrain_product_is_deterministic();
    test_terrain_stress_recipe_expands_river_network();
    test_terrain_mountain_range_stress_recipe_exposes_mountain_driver();
    test_terrain_review_river_coverage_is_meaningful();
    test_terrain_materials_and_vegetation_are_bounded();
    test_terrain_river_network_has_continuous_active_channels();
    test_terrain_river_core_avoids_long_grid_aligned_runs();
    test_terrain_river_uses_larger_routing_context();
    test_terrain_debug_export_writes_png();
    test_terrain_debug_export_writes_review_set();
    test_terrain_preview_config_uses_run_config_controls();
    test_terrain_preview_mesh_represents_heightfield();
    return 0;
}
