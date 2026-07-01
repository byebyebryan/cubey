#include "terrain_debug_export.h"
#include "terrain_generator.h"
#include "terrain_preview_config.h"
#include "terrain_preview_mesh.h"
#include "terrain_process_fields.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <string_view>
#include <utility>
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

[[nodiscard]] nlohmann::json read_json_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    require(static_cast<bool>(input), "test should open JSON file");
    nlohmann::json document;
    input >> document;
    return document;
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

[[nodiscard]] float max_abs_difference(const cubey::procedural::ScalarField2D& lhs,
                                       const cubey::procedural::ScalarField2D& rhs) {
    require(lhs.desc().width == rhs.desc().width && lhs.desc().height == rhs.desc().height,
            "max_abs_difference requires matching grids");
    float max_delta = 0.0F;
    for (std::uint32_t y = 0; y < lhs.desc().height; ++y) {
        for (std::uint32_t x = 0; x < lhs.desc().width; ++x) {
            max_delta = std::max(max_delta, std::abs(lhs.at(x, y) - rhs.at(x, y)));
        }
    }
    return max_delta;
}

[[nodiscard]] float masked_low_gradient_fraction(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& include_mask,
    const cubey::procedural::ScalarField2D& exclude_mask, float include_threshold,
    float exclude_max, float gradient_threshold) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    std::size_t selected = 0U;
    std::size_t low_gradient = 0U;
    for (std::uint32_t y = 1U; y + 1U < desc.height; ++y) {
        for (std::uint32_t x = 1U; x + 1U < desc.width; ++x) {
            if (include_mask.at(x, y) < include_threshold || exclude_mask.at(x, y) > exclude_max) {
                continue;
            }
            const float dx =
                (height.at(x + 1U, y) - height.at(x - 1U, y)) / (2.0F * desc.cell_size);
            const float dy =
                (height.at(x, y + 1U) - height.at(x, y - 1U)) / (2.0F * desc.cell_size);
            const float gradient = std::sqrt((dx * dx) + (dy * dy));
            ++selected;
            if (gradient < gradient_threshold) {
                ++low_gradient;
            }
        }
    }
    if (selected == 0U) {
        throw std::runtime_error("masked_low_gradient_fraction selected no samples");
    }
    return static_cast<float>(low_gradient) / static_cast<float>(selected);
}

[[nodiscard]] std::size_t unsupported_high_summit_samples(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& summit, float high_height_threshold,
    float support_threshold, std::uint32_t radius_cells, std::size_t min_support_samples) {
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    std::size_t unsupported = 0U;
    const int radius = static_cast<int>(radius_cells);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            if (height.at(x, y) < high_height_threshold || summit.at(x, y) < support_threshold) {
                continue;
            }
            std::size_t support_count = 0U;
            for (int oy = -radius; oy <= radius; ++oy) {
                for (int ox = -radius; ox <= radius; ++ox) {
                    const int sx = static_cast<int>(x) + ox;
                    const int sy = static_cast<int>(y) + oy;
                    if (sx < 0 || sy < 0 || sx >= static_cast<int>(desc.width) ||
                        sy >= static_cast<int>(desc.height)) {
                        continue;
                    }
                    if (summit.at(static_cast<std::uint32_t>(sx),
                                  static_cast<std::uint32_t>(sy)) >= support_threshold) {
                        ++support_count;
                    }
                }
            }
            if (support_count < min_support_samples) {
                ++unsupported;
            }
        }
    }
    return unsupported;
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

void test_terrain_process_field_helpers() {
    const cubey::procedural::Grid2DDesc desc{
        .width = 5,
        .height = 5,
        .cell_size = 10.0F,
    };
    cubey::procedural::ScalarField2D source(desc, 0.0F);
    source.at(2, 2) = 10.0F;

    const cubey::procedural::ScalarField2D one_step =
        cubey::projects::terrain::spread_max_decay_field(source, 1, 0.5F);
    require_near(source.at(1, 2), 0.0F, 0.001F,
                 "terrain process spread should not mutate the source field");
    require_near(one_step.at(2, 2), 10.0F, 0.001F,
                 "terrain process spread should preserve the source peak");
    require_near(one_step.at(1, 2), 5.0F, 0.001F,
                 "terrain process spread should decay to adjacent samples");
    require_near(one_step.at(1, 1), 5.0F, 0.001F,
                 "terrain process spread should use diagonal support");

    const cubey::procedural::ScalarField2D two_step =
        cubey::projects::terrain::spread_max_decay_field(source, 2, 0.5F);
    require_near(two_step.at(0, 2), 2.5F, 0.001F,
                 "terrain process spread should continue across iterations");
    require_near(two_step.at(0, 0), 2.5F, 0.001F,
                 "terrain process spread should continue diagonally across iterations");

    cubey::procedural::ScalarField2D primary(desc, 0.0F);
    cubey::procedural::ScalarField2D secondary(desc, 0.0F);
    cubey::procedural::ScalarField2D local_relief(desc, 0.0F);
    primary.at(2, 2) = 12.0F;
    secondary.at(2, 2) = 18.0F;
    local_relief.at(2, 2) = 10.0F;
    primary.at(1, 1) = 3.0F;
    secondary.at(1, 1) = 2.0F;

    const cubey::projects::terrain::TerrainProcessSplitLoweringFields lowering =
        cubey::projects::terrain::clamp_split_lowering_to_relief(
            primary, secondary, local_relief,
            cubey::projects::terrain::TerrainProcessLoweringLimit{
                .base_limit_m = 5.0F,
                .relief_fraction = 0.5F,
                .max_total_m = 20.0F,
            });
    require_near(lowering.primary.at(2, 2), 4.0F, 0.001F,
                 "terrain process clamp should preserve primary/secondary ratio");
    require_near(lowering.secondary.at(2, 2), 6.0F, 0.001F,
                 "terrain process clamp should preserve secondary/primary ratio");
    require_near(lowering.total.at(2, 2), 10.0F, 0.001F,
                 "terrain process clamp should limit against local relief");
    require_near(lowering.total.at(1, 1), 5.0F, 0.001F,
                 "terrain process clamp should allow values inside the base limit");

    cubey::procedural::ScalarField2D height(desc, 100.0F);
    const cubey::procedural::ScalarField2D lowered =
        cubey::projects::terrain::subtract_lowering_from_height(height, lowering.total);
    require_near(lowered.at(2, 2), 90.0F, 0.001F,
                 "terrain process lowering should subtract from height");
    require_near(lowered.at(1, 1), 95.0F, 0.001F,
                 "terrain process lowering should subtract unclamped samples");

    cubey::procedural::ScalarField2D slope(desc, 0.5F);
    cubey::procedural::ScalarField2D curvature(desc, 0.0F);
    cubey::procedural::ScalarField2D support(desc, 1.0F);
    curvature.at(2, 2) = -40.0F;
    const cubey::projects::terrain::TerrainProcessGullyDiagnosticFields gully =
        cubey::projects::terrain::compute_gully_erosion_diagnostic(
            height, slope, curvature, local_relief, support,
            cubey::projects::terrain::TerrainProcessGullyDiagnosticConfig{
                .support_start = 0.0F,
                .support_full = 0.5F,
                .slope_start = 0.0F,
                .slope_full = 0.5F,
                .relief_start_m = 1.0F,
                .relief_full_m = 10.0F,
                .mask_blur_iterations = 0,
                .mask_spread_iterations = 0,
                .base_delta_limit_m = 0.0F,
                .relief_delta_fraction = 1.0F,
                .max_delta_m = 20.0F,
            });
    require_near(gully.crease_proxy.at(2, 2), 1.0F, 0.001F,
                 "terrain gully diagnostic should expose a crease proxy");
    require_near(gully.gully_mask.at(2, 2), 1.0F, 0.001F,
                 "terrain gully diagnostic should expose a gully mask");
    require_near(gully.erosion_delta_m.at(2, 2), 10.0F, 0.001F,
                 "terrain gully diagnostic should clamp erosion by local relief");
    require_near(gully.post_erosion_height_m.at(2, 2), 90.0F, 0.001F,
                 "terrain gully diagnostic should publish a lowered review height");
    require_near(height.at(2, 2), 100.0F, 0.001F,
                 "terrain gully diagnostic should not mutate input height");

    const cubey::projects::terrain::TerrainProcessThermalTalusFields flat_talus =
        cubey::projects::terrain::compute_thermal_talus_diagnostic(
            height, slope, local_relief, support,
            cubey::projects::terrain::TerrainProcessThermalTalusConfig{
                .support_start = 0.0F,
                .support_full = 0.5F,
                .talus_slope = 0.50F,
                .iterations = 2,
            });
    require_near(flat_talus.thermal_erosion_delta_m.summarize().max, 0.0F, 0.001F,
                 "terrain thermal talus should leave flat heightfields unchanged");
    require_near(flat_talus.talus_deposition_m.summarize().max, 0.0F, 0.001F,
                 "terrain thermal talus should not deposit without over-steep samples");
    require_near(flat_talus.post_erosion_height_m.at(2, 2), 100.0F, 0.001F,
                 "terrain thermal talus should preserve flat post-process height");

    cubey::procedural::ScalarField2D talus_height(desc, 100.0F);
    cubey::procedural::ScalarField2D talus_slope(desc, 1.0F);
    cubey::procedural::ScalarField2D talus_relief(desc, 0.0F);
    cubey::procedural::ScalarField2D talus_support(desc, 1.0F);
    talus_height.at(2, 2) = 180.0F;
    talus_relief.at(2, 2) = 100.0F;
    const cubey::projects::terrain::TerrainProcessThermalTalusFields talus =
        cubey::projects::terrain::compute_thermal_talus_diagnostic(
            talus_height, talus_slope, talus_relief, talus_support,
            cubey::projects::terrain::TerrainProcessThermalTalusConfig{
                .support_start = 0.0F,
                .support_full = 0.5F,
                .talus_slope = 0.50F,
                .iterations = 1,
                .transfer_fraction = 0.50F,
                .base_transfer_limit_m = 0.0F,
                .relief_transfer_fraction = 0.10F,
                .max_total_erosion_m = 3.0F,
            });
    require(talus.thermal_erosion_delta_m.at(2, 2) > 0.0F,
            "terrain thermal talus should erode over-steep supported peaks");
    require(talus.thermal_erosion_delta_m.at(2, 2) <= 3.001F,
            "terrain thermal talus should clamp erosion by configured limits");
    require(talus.talus_deposition_m.at(1, 2) > 0.0F,
            "terrain thermal talus should deposit material into lower neighbors");
    require(talus.post_erosion_height_m.at(2, 2) < talus_height.at(2, 2),
            "terrain thermal talus should lower eroded peak samples");
    require(talus.post_erosion_height_m.at(1, 2) > talus_height.at(1, 2),
            "terrain thermal talus should raise deposition samples");
    require(talus.slope_instability.at(2, 2) > 0.0F &&
                talus.slope_instability.at(2, 2) <= 1.0F,
            "terrain thermal talus should publish bounded residual instability");

    require_throws(
        [&source] {
            static_cast<void>(
                cubey::projects::terrain::spread_max_decay_field(source, -1, 0.5F));
        },
        "terrain process spread should reject negative iterations");
    require_throws(
        [&source] {
            static_cast<void>(
                cubey::projects::terrain::spread_max_decay_field(source, 1, 1.5F));
        },
        "terrain process spread should reject invalid decay");
    require_throws(
        [&height, &slope, &curvature, &local_relief, &support] {
            cubey::projects::terrain::TerrainProcessGullyDiagnosticConfig invalid{};
            invalid.slope_start = 0.5F;
            invalid.slope_full = 0.5F;
            static_cast<void>(cubey::projects::terrain::compute_gully_erosion_diagnostic(
                height, slope, curvature, local_relief, support, invalid));
        },
        "terrain gully diagnostic should reject invalid config ranges");
    require_throws(
        [&height, &slope, &local_relief, &support] {
            cubey::projects::terrain::TerrainProcessThermalTalusConfig invalid{};
            invalid.talus_slope = 0.0F;
            static_cast<void>(cubey::projects::terrain::compute_thermal_talus_diagnostic(
                height, slope, local_relief, support, invalid));
        },
        "terrain thermal talus should reject invalid config ranges");

    const cubey::procedural::Grid2DDesc wider_desc{
        .width = 6,
        .height = 5,
        .cell_size = 10.0F,
    };
    const cubey::procedural::ScalarField2D mismatched(wider_desc, 0.0F);
    require_throws(
        [&primary, &secondary, &mismatched] {
            static_cast<void>(cubey::projects::terrain::clamp_split_lowering_to_relief(
                primary, secondary, mismatched,
                cubey::projects::terrain::TerrainProcessLoweringLimit{
                    .base_limit_m = 1.0F,
                    .relief_fraction = 0.0F,
                    .max_total_m = 1.0F,
                }));
        },
        "terrain process clamp should reject grid mismatches");
    require_throws(
        [&height, &slope, &curvature, &local_relief, &mismatched] {
            static_cast<void>(cubey::projects::terrain::compute_gully_erosion_diagnostic(
                height, slope, curvature, local_relief, mismatched,
                cubey::projects::terrain::TerrainProcessGullyDiagnosticConfig{}));
        },
        "terrain gully diagnostic should reject grid mismatches");
    require_throws(
        [&height, &slope, &local_relief, &mismatched] {
            static_cast<void>(cubey::projects::terrain::compute_thermal_talus_diagnostic(
                height, slope, local_relief, mismatched,
                cubey::projects::terrain::TerrainProcessThermalTalusConfig{}));
        },
        "terrain thermal talus should reject grid mismatches");
}

void test_terrain_product_emits_required_fields() {
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(small_config());

    const std::array<std::string_view, 55> required_fields{
        cubey::projects::terrain::kTerrainFieldHeightM,
        cubey::projects::terrain::kTerrainFieldPreProcessHeightM,
        cubey::projects::terrain::kTerrainFieldBaseElevation,
        cubey::projects::terrain::kTerrainFieldBroadRelief,
        cubey::projects::terrain::kTerrainFieldMountainProfileHeightM,
        cubey::projects::terrain::kTerrainFieldMountainRangeSpine,
        cubey::projects::terrain::kTerrainFieldMountainEnvelope,
        cubey::projects::terrain::kTerrainFieldMountainMass,
        cubey::projects::terrain::kTerrainFieldMountainShoulder,
        cubey::projects::terrain::kTerrainFieldMountainSummitCore,
        cubey::projects::terrain::kTerrainFieldMountainSaddleGate,
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
        cubey::projects::terrain::kTerrainFieldErosionDeltaM,
        cubey::projects::terrain::kTerrainFieldGullyMask,
        cubey::projects::terrain::kTerrainFieldCreaseProxy,
        cubey::projects::terrain::kTerrainFieldThermalErosionDeltaM,
        cubey::projects::terrain::kTerrainFieldTalusDepositionM,
        cubey::projects::terrain::kTerrainFieldSlopeInstability,
        cubey::projects::terrain::kTerrainFieldPostErosionHeightM,
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
    const cubey::procedural::ScalarFieldStats channel_incision =
        field(product, cubey::projects::terrain::kTerrainFieldChannelIncision).summarize();
    const cubey::procedural::ScalarFieldStats valley_incision =
        field(product, cubey::projects::terrain::kTerrainFieldValleyIncision).summarize();
    const auto& fill_delta = field(product, cubey::projects::terrain::kTerrainFieldRoutingFillDelta);
    const cubey::procedural::ScalarFieldStats fill_delta_stats = fill_delta.summarize();
    const cubey::procedural::ScalarFieldStats sink =
        field(product, cubey::projects::terrain::kTerrainFieldSinkMask).summarize();
    require(flow.max > flow.min, "flow accumulation should vary");
    require(stream_order.max >= 3.0F, "stream order should identify larger drainage trunks");
    require(drainage.max > drainage.min, "drainage potential should vary");
    require(fill_delta_stats.min >= 0.0F, "routing fill delta should be non-negative");
    require(channel_incision.max > 4.0F, "river carving should emit channel incision");
    require(valley_incision.max > 8.0F, "river carving should emit valley incision");
    require(channel_incision.min >= 0.0F && valley_incision.min >= 0.0F,
            "river incision fields should be non-negative");
    const std::size_t repaired_count = count_active_samples(fill_delta, 0.001F);
    require(repaired_count > 0U, "routing repair should expose at least one repaired sample");
    require(repaired_count * 100U < fill_delta.sample_count() * 75U,
            "routing repair should not fill most of the visible patch");
    require(sink.max > 0.0F && sink.max <= 1.0F, "sink mask should identify terminal cells");
}

void test_terrain_river_carves_height_product() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 129;
    config.grid_height = 129;
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& source_height =
        field(product, cubey::projects::terrain::kTerrainFieldPreProcessHeightM);
    const auto& final_height = field(product, cubey::projects::terrain::kTerrainFieldHeightM);
    const auto& river = field(product, cubey::projects::terrain::kTerrainFieldRiverMask);
    const auto& channel_incision =
        field(product, cubey::projects::terrain::kTerrainFieldChannelIncision);
    const auto& valley_incision =
        field(product, cubey::projects::terrain::kTerrainFieldValleyIncision);

    double active_drop_sum = 0.0;
    double shoulder_drop_sum = 0.0;
    std::size_t active_count = 0U;
    std::size_t shoulder_count = 0U;
    std::size_t exact_samples = 0U;
    constexpr std::array<std::pair<int, int>, 4> kShoulderOffsets{
        std::pair<int, int>{4, 0},
        std::pair<int, int>{-4, 0},
        std::pair<int, int>{0, 4},
        std::pair<int, int>{0, -4},
    };
    const cubey::procedural::Grid2DDesc& desc = final_height.desc();
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float channel_drop = channel_incision.at(x, y);
            const float valley_drop = valley_incision.at(x, y);
            const float total_drop = channel_drop + valley_drop;
            require_near(source_height.at(x, y) - final_height.at(x, y), total_drop, 0.002F,
                         "final height should equal source height minus river incision");
            ++exact_samples;

            if (river.at(x, y) < 0.45F || total_drop < 8.0F) {
                continue;
            }
            active_drop_sum += total_drop;
            ++active_count;
            for (const auto [offset_x, offset_y] : kShoulderOffsets) {
                const int sx = static_cast<int>(x) + offset_x;
                const int sy = static_cast<int>(y) + offset_y;
                if (sx < 0 || sy < 0 || sx >= static_cast<int>(desc.width) ||
                    sy >= static_cast<int>(desc.height)) {
                    continue;
                }
                const std::uint32_t shoulder_x = static_cast<std::uint32_t>(sx);
                const std::uint32_t shoulder_y = static_cast<std::uint32_t>(sy);
                if (river.at(shoulder_x, shoulder_y) > 0.18F) {
                    continue;
                }
                shoulder_drop_sum += channel_incision.at(shoulder_x, shoulder_y) +
                                     valley_incision.at(shoulder_x, shoulder_y);
                ++shoulder_count;
            }
        }
    }

    require(exact_samples == final_height.sample_count(),
            "river carving test should inspect every sample");
    require(active_count > 16U, "river carving should find active channel samples");
    require(shoulder_count > 16U, "river carving should find nearby low-river shoulders");
    const double active_average = active_drop_sum / static_cast<double>(active_count);
    const double shoulder_average = shoulder_drop_sum / static_cast<double>(shoulder_count);
    require(active_average > shoulder_average + 8.0,
            "active river samples should be carved lower than nearby shoulders");
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
    const auto& baseline_profile =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainProfileHeightM);
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
    const auto& baseline_saddle_gate =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainSaddleGate);
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
    const auto& saddle_gate =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainSaddleGate);
    const auto& mountain_uplift =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainUplift);
    const auto& ridge_uplift =
        field(mountain, cubey::projects::terrain::kTerrainFieldRidgeUplift);
    const auto& peak_uplift =
        field(mountain, cubey::projects::terrain::kTerrainFieldPeakUplift);
    const auto& profile_height =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainProfileHeightM);
    const auto& pre_process_height =
        field(mountain, cubey::projects::terrain::kTerrainFieldPreProcessHeightM);
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
    require(baseline_profile.summarize().max == 0.0F,
            "default terrain recipe should keep mountain profile height inactive");
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
    require(baseline_saddle_gate.summarize().max == 0.0F,
            "default terrain recipe should keep mountain saddle gate disabled");

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
    const std::size_t ridge_body_samples = count_active_samples(ridge_influence, 0.16F);
    const std::size_t saddle_gate_samples = count_active_samples(saddle_gate, 0.35F);
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
    if (ridge_body_samples <= ridge_skeleton_samples * 4U) {
        throw std::runtime_error(
            "terrain ridge influence should expose a broad body around narrow crests: body=" +
            std::to_string(ridge_body_samples) + " skeleton=" +
            std::to_string(ridge_skeleton_samples));
    }
    if (saddle_gate_samples * 100U < total_samples * 4U ||
        saddle_gate_samples * 100U > total_samples * 58U) {
        throw std::runtime_error(
            "terrain saddle gate should suppress some highland mass without filling the map: samples=" +
            std::to_string(saddle_gate_samples) + " total=" + std::to_string(total_samples));
    }

    require(mountain.summary.height.span > 1200.0F,
            "terrain mountain recipe should maintain kilometer-scale height relief");
    require(mountain_uplift.summarize().max > 160.0F,
            "terrain mountain recipe should emit broad mountain uplift");
    require(ridge_uplift.summarize().max > 220.0F,
            "terrain mountain recipe should emit stronger ridge uplift");
    require(peak_uplift.summarize().max > 160.0F,
            "terrain mountain recipe should emit peak attribution");
    require(profile_height.summarize().span > 1100.0F,
            "terrain mountain recipe should emit a coherent profile height");
    require(max_abs_difference(pre_process_height, profile_height) < 32.0F,
            "terrain mountain pre-process height should stay close to profile plus detail");

    const float lowland_average_height = average_where(height, mountain_support, 0.0F, 0.12F);
    const float mountain_average_height = average_where(height, mountain_support, 0.42F);
    const float peak_average_height = average_where(height, peak_support, 0.36F);
    const float crest_average_height = average_where(height, ridge_skeleton, 0.24F);
    const float ridge_body_average_height = average_where(height, ridge_influence, 0.28F);
    const float saddle_average_height = average_where(height, saddle_gate, 0.48F);
    require(mountain_average_height > lowland_average_height + 220.0F,
            "terrain mountain support should build above lowland samples");
    require(peak_average_height > mountain_average_height + 80.0F,
            "terrain peak support should build above broad mountain samples");
    if (peak_average_height >= mountain_average_height + 620.0F) {
        throw std::runtime_error(
            "terrain peak support should not become unsupported summit bulges: peak=" +
            std::to_string(peak_average_height) + " mountain=" +
            std::to_string(mountain_average_height));
    }
    if (crest_average_height >= ridge_body_average_height + 230.0F) {
        throw std::runtime_error(
            "terrain crest support should sharpen broad ridge bodies without becoming fins: crest=" +
            std::to_string(crest_average_height) + " body=" +
            std::to_string(ridge_body_average_height));
    }
    require(crest_average_height > saddle_average_height + 65.0F,
            "terrain crest support should rise above saddle-gated highland mass");
    require(peak_average_height > saddle_average_height + 160.0F,
            "terrain peak support should rise above saddle-gated highland mass");
}

void test_terrain_mountain_macro_fields_are_hierarchical() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 257;
    config.grid_height = 257;
    const cubey::projects::terrain::TerrainRegionProduct baseline =
        cubey::projects::terrain::generate_terrain_region(config);

    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress);
    const cubey::projects::terrain::TerrainRegionProduct mountain =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& baseline_mass =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainMass);
    const auto& baseline_shoulder =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainShoulder);
    const auto& baseline_summit =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainSummitCore);
    const auto& baseline_saddle =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainSaddleGate);
    const auto& baseline_profile =
        field(baseline, cubey::projects::terrain::kTerrainFieldMountainProfileHeightM);
    require(baseline_mass.summarize().max == 0.0F,
            "default terrain recipe should keep mountain mass inactive");
    require(baseline_shoulder.summarize().max == 0.0F,
            "default terrain recipe should keep mountain shoulder inactive");
    require(baseline_summit.summarize().max == 0.0F,
            "default terrain recipe should keep mountain summit core inactive");
    require(baseline_saddle.summarize().max == 0.0F,
            "default terrain recipe should keep mountain saddle gate inactive");
    require(baseline_profile.summarize().max == 0.0F,
            "default terrain recipe should keep mountain profile height inactive");

    const auto& profile =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainProfileHeightM);
    const auto& mass = field(mountain, cubey::projects::terrain::kTerrainFieldMountainMass);
    const auto& shoulder =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainShoulder);
    const auto& summit =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainSummitCore);
    const auto& saddle =
        field(mountain, cubey::projects::terrain::kTerrainFieldMountainSaddleGate);

    const cubey::procedural::ScalarFieldStats mass_stats = mass.summarize();
    const cubey::procedural::ScalarFieldStats shoulder_stats = shoulder.summarize();
    const cubey::procedural::ScalarFieldStats summit_stats = summit.summarize();
    const cubey::procedural::ScalarFieldStats saddle_stats = saddle.summarize();
    require(mass_stats.max > 0.95F && shoulder_stats.max > 0.60F,
            "mountain mass and shoulder fields should expose active stress sources");
    require(summit_stats.max > 0.55F,
            "mountain summit core should stay active after profile softening");
    require(saddle_stats.max > 0.60F,
            "mountain saddle gate should expose highland negative space");

    const std::size_t total_samples = mass.sample_count();
    const std::size_t mass_samples = count_active_samples(mass, 0.35F);
    const std::size_t shoulder_samples = count_active_samples(shoulder, 0.30F);
    const std::size_t summit_samples = count_active_samples(summit, 0.25F);
    const std::size_t saddle_samples = count_active_samples(saddle, 0.35F);
    if (mass_samples * 100U < total_samples * 15U ||
        mass_samples * 100U > total_samples * 70U) {
        throw std::runtime_error(
            "mountain mass should be broad but bounded: samples=" +
            std::to_string(mass_samples) + " total=" + std::to_string(total_samples));
    }
    if (shoulder_samples * 100U < total_samples * 10U ||
        shoulder_samples * 100U > total_samples * 68U) {
        throw std::runtime_error(
            "mountain shoulder should build foothills without filling the patch: samples=" +
            std::to_string(shoulder_samples) + " total=" + std::to_string(total_samples));
    }
    if (summit_samples < 8U || summit_samples * 100U > total_samples * 12U) {
        throw std::runtime_error(
            "mountain summit core should be sparse: samples=" +
            std::to_string(summit_samples) + " total=" + std::to_string(total_samples));
    }
    if (saddle_samples * 100U < total_samples * 4U ||
        saddle_samples * 100U > total_samples * 58U) {
        throw std::runtime_error(
            "mountain saddle gate should stay bounded between crests: samples=" +
            std::to_string(saddle_samples) + " total=" + std::to_string(total_samples));
    }

    const float lowland_average_height = average_where(profile, mass, 0.0F, 0.10F);
    const float mass_average_height = average_where(profile, mass, 0.42F);
    const float shoulder_average_height = average_where(profile, shoulder, 0.42F);
    const float summit_average_height = average_where(profile, summit, 0.32F);
    const float saddle_average_height = average_where(profile, saddle, 0.48F);
    require(mass_average_height > lowland_average_height + 240.0F,
            "mountain mass should lift above lowland samples");
    require(shoulder_average_height > lowland_average_height + 260.0F,
            "mountain shoulders should participate in the broad uphill profile");
    require(summit_average_height > mass_average_height + 80.0F,
            "mountain summit core should build above broad mountain mass");
    if (summit_average_height >= mass_average_height + 650.0F) {
        throw std::runtime_error(
            "mountain summit core should sharpen mass without creating isolated bulges: summit=" +
            std::to_string(summit_average_height) + " mass=" +
            std::to_string(mass_average_height));
    }
    require(summit_average_height > saddle_average_height + 160.0F,
            "mountain summits should build above saddle-gated highland mass");

    const float shoulder_flat_fraction =
        masked_low_gradient_fraction(profile, shoulder, summit, 0.42F, 0.22F, 0.030F);
    require(shoulder_flat_fraction < 0.62F,
            "mountain shoulder regions should not be dominated by flat shelves");

    const cubey::procedural::ScalarFieldStats profile_stats = profile.summarize();
    const float high_threshold = profile_stats.max - (profile_stats.span * 0.035F);
    require(unsupported_high_summit_samples(profile, summit, high_threshold, 0.24F, 4U, 18U) ==
                0U,
            "mountain high points should have surrounding summit support");
}

void test_terrain_mountain_gully_diagnostic_is_bounded() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 257;
    config.grid_height = 257;
    const cubey::projects::terrain::TerrainRegionProduct baseline =
        cubey::projects::terrain::generate_terrain_region(config);

    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress);
    const cubey::projects::terrain::TerrainRegionProduct mountain =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& baseline_height =
        field(baseline, cubey::projects::terrain::kTerrainFieldHeightM);
    const auto& baseline_delta =
        field(baseline, cubey::projects::terrain::kTerrainFieldErosionDeltaM);
    const auto& baseline_gully =
        field(baseline, cubey::projects::terrain::kTerrainFieldGullyMask);
    const auto& baseline_crease =
        field(baseline, cubey::projects::terrain::kTerrainFieldCreaseProxy);
    const auto& baseline_post =
        field(baseline, cubey::projects::terrain::kTerrainFieldPostErosionHeightM);
    require(baseline_delta.summarize().max == 0.0F,
            "default terrain recipe should keep gully erosion inactive");
    require(baseline_gully.summarize().max == 0.0F,
            "default terrain recipe should keep gully mask inactive");
    require(baseline_crease.summarize().max == 0.0F,
            "default terrain recipe should keep crease proxy inactive");
    for (std::uint32_t y = 0; y < baseline_height.desc().height; ++y) {
        for (std::uint32_t x = 0; x < baseline_height.desc().width; ++x) {
            require_near(baseline_post.at(x, y), baseline_height.at(x, y), 0.001F,
                         "inactive post-erosion height should equal final height");
        }
    }

    const auto& height = field(mountain, cubey::projects::terrain::kTerrainFieldHeightM);
    const auto& erosion_delta =
        field(mountain, cubey::projects::terrain::kTerrainFieldErosionDeltaM);
    const auto& gully_mask = field(mountain, cubey::projects::terrain::kTerrainFieldGullyMask);
    const auto& crease_proxy =
        field(mountain, cubey::projects::terrain::kTerrainFieldCreaseProxy);
    const cubey::procedural::ScalarFieldStats erosion_stats = erosion_delta.summarize();
    const cubey::procedural::ScalarFieldStats gully_stats = gully_mask.summarize();
    const cubey::procedural::ScalarFieldStats crease_stats = crease_proxy.summarize();
    require(erosion_stats.max > 1.0F,
            "mountain stress recipe should emit visible gully erosion diagnostics");
    require(erosion_stats.max <= 78.001F,
            "mountain gully erosion diagnostic should stay bounded");
    require(gully_stats.max > 0.05F && gully_stats.max <= 1.0F,
            "mountain gully mask should stay normalized and active");
    require(crease_stats.max > 0.05F && crease_stats.max <= 1.0F,
            "mountain crease proxy should stay normalized and active");
    require(count_active_samples(gully_mask, 0.20F) > gully_mask.sample_count() / 200U,
            "mountain gully mask should cover enough samples to review");
    require(count_active_samples(gully_mask, 0.20F) < gully_mask.sample_count() * 70U / 100U,
            "mountain gully mask should not fill most of the patch");

    std::size_t lowered_samples = 0U;
    for (std::uint32_t y = 0; y < height.desc().height; ++y) {
        for (std::uint32_t x = 0; x < height.desc().width; ++x) {
            const float delta = erosion_delta.at(x, y);
            require(delta >= 0.0F, "mountain gully erosion should be non-negative");
            if (delta > 1.0F) {
                ++lowered_samples;
            }
        }
    }
    require(lowered_samples > 32U,
            "mountain gully diagnostic should produce reviewable lowered samples");
}

void test_terrain_mountain_thermal_talus_diagnostic_is_bounded() {
    cubey::projects::terrain::TerrainRegionConfig config = small_config();
    config.grid_width = 257;
    config.grid_height = 257;
    const cubey::projects::terrain::TerrainRegionProduct baseline =
        cubey::projects::terrain::generate_terrain_region(config);

    config.recipe_id =
        std::string(cubey::projects::terrain::kTerrainRecipeTemperateMountainRangeStress);
    const cubey::projects::terrain::TerrainRegionProduct mountain =
        cubey::projects::terrain::generate_terrain_region(config);

    const auto& baseline_height =
        field(baseline, cubey::projects::terrain::kTerrainFieldHeightM);
    const auto& baseline_thermal =
        field(baseline, cubey::projects::terrain::kTerrainFieldThermalErosionDeltaM);
    const auto& baseline_talus =
        field(baseline, cubey::projects::terrain::kTerrainFieldTalusDepositionM);
    const auto& baseline_instability =
        field(baseline, cubey::projects::terrain::kTerrainFieldSlopeInstability);
    const auto& baseline_post =
        field(baseline, cubey::projects::terrain::kTerrainFieldPostErosionHeightM);
    require(baseline_thermal.summarize().max == 0.0F,
            "default terrain recipe should keep thermal erosion inactive");
    require(baseline_talus.summarize().max == 0.0F,
            "default terrain recipe should keep talus deposition inactive");
    require(baseline_instability.summarize().max == 0.0F,
            "default terrain recipe should keep slope instability inactive");
    for (std::uint32_t y = 0; y < baseline_height.desc().height; ++y) {
        for (std::uint32_t x = 0; x < baseline_height.desc().width; ++x) {
            require_near(baseline_post.at(x, y), baseline_height.at(x, y), 0.001F,
                         "inactive thermal talus post height should equal final height");
        }
    }

    const auto& height = field(mountain, cubey::projects::terrain::kTerrainFieldHeightM);
    const auto& gully_delta =
        field(mountain, cubey::projects::terrain::kTerrainFieldErosionDeltaM);
    const auto& thermal_delta =
        field(mountain, cubey::projects::terrain::kTerrainFieldThermalErosionDeltaM);
    const auto& talus_deposition =
        field(mountain, cubey::projects::terrain::kTerrainFieldTalusDepositionM);
    const auto& instability =
        field(mountain, cubey::projects::terrain::kTerrainFieldSlopeInstability);
    const auto& post_erosion_height =
        field(mountain, cubey::projects::terrain::kTerrainFieldPostErosionHeightM);
    const cubey::procedural::ScalarFieldStats thermal_stats = thermal_delta.summarize();
    const cubey::procedural::ScalarFieldStats talus_stats = talus_deposition.summarize();
    const cubey::procedural::ScalarFieldStats instability_stats = instability.summarize();
    require(thermal_stats.max > 1.0F,
            "mountain stress recipe should emit visible thermal erosion diagnostics");
    require(thermal_stats.max <= 96.001F,
            "mountain thermal erosion diagnostic should stay bounded");
    require(talus_stats.max > 1.0F,
            "mountain stress recipe should emit visible talus deposition diagnostics");
    require(instability_stats.max > 0.01F && instability_stats.max <= 1.0F,
            "mountain slope instability should stay normalized and active");
    require(count_active_samples(thermal_delta, 0.50F) > thermal_delta.sample_count() / 500U,
            "mountain thermal erosion should cover enough samples to review");
    require(count_active_samples(thermal_delta, 0.50F) < thermal_delta.sample_count() / 2U,
            "mountain thermal erosion should not affect most of the patch");
    require(count_active_samples(talus_deposition, 0.25F) >
                talus_deposition.sample_count() / 700U,
            "mountain talus deposition should cover enough samples to review");

    std::size_t changed_samples = 0U;
    for (std::uint32_t y = 0; y < height.desc().height; ++y) {
        for (std::uint32_t x = 0; x < height.desc().width; ++x) {
            const float expected = height.at(x, y) - gully_delta.at(x, y) -
                                   thermal_delta.at(x, y) + talus_deposition.at(x, y);
            require_near(post_erosion_height.at(x, y), expected, 0.003F,
                         "post-erosion height should combine gully and talus diagnostics");
            if (std::abs(post_erosion_height.at(x, y) - height.at(x, y)) > 1.0F) {
                ++changed_samples;
            }
        }
    }
    require(changed_samples > 32U,
            "mountain thermal talus diagnostic should produce reviewable height changes");
}

void test_terrain_review_river_coverage_is_meaningful() {
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
    config.grid_width = 257;
    config.grid_height = 257;
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
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_mass") ==
                cubey::projects::terrain::TerrainDebugView::MountainMass,
            "terrain debug view should parse mountain mass aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_shoulder") ==
                cubey::projects::terrain::TerrainDebugView::MountainShoulder,
            "terrain debug view should parse mountain shoulder aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_summit_core") ==
                cubey::projects::terrain::TerrainDebugView::MountainSummitCore,
            "terrain debug view should parse mountain summit core aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_saddle_gate") ==
                cubey::projects::terrain::TerrainDebugView::MountainSaddleGate,
            "terrain debug view should parse mountain saddle gate aliases");
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
    require(cubey::projects::terrain::terrain_debug_view_from_name("pre_process_height") ==
                cubey::projects::terrain::TerrainDebugView::PreProcessHeight,
            "terrain debug view should parse pre-process height aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("mountain_profile_height") ==
                cubey::projects::terrain::TerrainDebugView::MountainProfileHeight,
            "terrain debug view should parse mountain profile height aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("erosion_delta") ==
                cubey::projects::terrain::TerrainDebugView::ErosionDelta,
            "terrain debug view should parse erosion delta aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("gully_mask") ==
                cubey::projects::terrain::TerrainDebugView::GullyMask,
            "terrain debug view should parse gully mask aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("post_erosion_height") ==
                cubey::projects::terrain::TerrainDebugView::PostErosionHeight,
            "terrain debug view should parse post-erosion height aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("thermal_erosion_delta") ==
                cubey::projects::terrain::TerrainDebugView::ThermalErosionDelta,
            "terrain debug view should parse thermal erosion aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("talus_deposition") ==
                cubey::projects::terrain::TerrainDebugView::TalusDeposition,
            "terrain debug view should parse talus deposition aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("slope_instability") ==
                cubey::projects::terrain::TerrainDebugView::SlopeInstability,
            "terrain debug view should parse slope instability aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("channel_incision") ==
                cubey::projects::terrain::TerrainDebugView::ChannelIncision,
            "terrain debug view should parse channel incision aliases");
    require(cubey::projects::terrain::terrain_debug_view_from_name("valley_incision") ==
                cubey::projects::terrain::TerrainDebugView::ValleyIncision,
            "terrain debug view should parse valley incision aliases");
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

    const std::filesystem::path gully_output =
        std::filesystem::temp_directory_path() / "cubey_terrain_gully_mask_export_test.png";
    std::filesystem::remove(gully_output);
    cubey::projects::terrain::write_terrain_debug_png(
        mountain_product, cubey::projects::terrain::TerrainDebugView::GullyMask, gully_output);
    require(std::filesystem::file_size(gully_output) > 64U,
            "terrain gully debug export should write a non-empty PNG");
    std::filesystem::remove(gully_output);

    const std::filesystem::path talus_output =
        std::filesystem::temp_directory_path() / "cubey_terrain_talus_export_test.png";
    std::filesystem::remove(talus_output);
    cubey::projects::terrain::write_terrain_debug_png(
        mountain_product, cubey::projects::terrain::TerrainDebugView::TalusDeposition,
        talus_output);
    require(std::filesystem::file_size(talus_output) > 64U,
            "terrain talus debug export should write a non-empty PNG");
    std::filesystem::remove(talus_output);
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

    const std::filesystem::path manifest_path = output_dir / "manifest.json";
    require(std::filesystem::file_size(manifest_path) > 64U,
            "terrain debug review export should write a non-empty manifest");
    const nlohmann::json manifest = read_json_file(manifest_path);
    require(manifest.at("schema") == "cubey.terrain.scalar_capture.v1",
            "terrain debug manifest should identify its schema");
    require(manifest.at("recipe_id") == product.config.recipe_id,
            "terrain debug manifest should record the recipe");
    require(manifest.at("generator_revision") == product.config.generator_revision,
            "terrain debug manifest should record the generator revision");
    require(manifest.at("seed_hex").get<std::string>().starts_with("0x"),
            "terrain debug manifest should record a hex seed");
    require(manifest.at("summary").at("content_hash_hex").get<std::string>().starts_with("0x"),
            "terrain debug manifest should record a hex content hash");
    require(manifest.at("grid").at("width") == product.fields.desc().width,
            "terrain debug manifest should record grid width");
    require(manifest.at("grid").at("height") == product.fields.desc().height,
            "terrain debug manifest should record grid height");
    require(manifest.at("field_count") == product.fields.field_count(),
            "terrain debug manifest should record field count");
    require(manifest.at("views").size() ==
                cubey::projects::terrain::terrain_debug_review_views().size(),
            "terrain debug manifest should record review view count");
    require(manifest.at("outputs").size() ==
                cubey::projects::terrain::terrain_debug_review_views().size(),
            "terrain debug manifest should record review output count");
    const std::vector<std::string> outputs =
        manifest.at("outputs").get<std::vector<std::string>>();
    require(std::find(outputs.begin(), outputs.end(), "final.png") != outputs.end(),
            "terrain debug manifest should list final PNG output");
    require(std::find(outputs.begin(), outputs.end(), "channel-incision.png") != outputs.end(),
            "terrain debug manifest should list incision PNG output");
    require(std::find(outputs.begin(), outputs.end(), "erosion-delta.png") != outputs.end(),
            "terrain debug manifest should list erosion delta PNG output");
    require(std::find(outputs.begin(), outputs.end(), "post-erosion-height.png") != outputs.end(),
            "terrain debug manifest should list post-erosion height PNG output");
    require(std::find(outputs.begin(), outputs.end(), "thermal-erosion-delta.png") !=
                outputs.end(),
            "terrain debug manifest should list thermal erosion PNG output");
    require(std::find(outputs.begin(), outputs.end(), "talus-deposition.png") != outputs.end(),
            "terrain debug manifest should list talus deposition PNG output");
    require(std::find(outputs.begin(), outputs.end(), "slope-instability.png") != outputs.end(),
            "terrain debug manifest should list slope instability PNG output");
    require(std::find(outputs.begin(), outputs.end(), "mountain-profile-height.png") !=
                outputs.end(),
            "terrain debug manifest should list mountain profile height PNG output");
    require(std::find(outputs.begin(), outputs.end(), "mountain-mass.png") != outputs.end(),
            "terrain debug manifest should list mountain mass PNG output");
    require(std::find(outputs.begin(), outputs.end(), "mountain-summit-core.png") !=
                outputs.end(),
            "terrain debug manifest should list mountain summit core PNG output");
    require(std::find(outputs.begin(), outputs.end(), "mountain-saddle-gate.png") !=
                outputs.end(),
            "terrain debug manifest should list mountain saddle gate PNG output");

    const nlohmann::json& height_stats =
        manifest.at("fields").at(std::string(cubey::projects::terrain::kTerrainFieldHeightM));
    require(height_stats.at("sample_count") ==
                product.fields.desc().width * product.fields.desc().height,
            "terrain debug manifest should record field sample counts");
    require_near(height_stats.at("span").get<float>(), product.summary.height.span, 0.01F,
                 "terrain debug manifest should record height stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldChannelIncision)),
            "terrain debug manifest should include channel incision stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldValleyIncision)),
            "terrain debug manifest should include valley incision stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldErosionDeltaM)),
            "terrain debug manifest should include erosion delta stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldPostErosionHeightM)),
            "terrain debug manifest should include post-erosion height stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldThermalErosionDeltaM)),
            "terrain debug manifest should include thermal erosion stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldTalusDepositionM)),
            "terrain debug manifest should include talus deposition stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldSlopeInstability)),
            "terrain debug manifest should include slope instability stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldMountainProfileHeightM)),
            "terrain debug manifest should include mountain profile height stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldMountainMass)),
            "terrain debug manifest should include mountain mass stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldMountainSummitCore)),
            "terrain debug manifest should include mountain summit core stats");
    require(manifest.at("fields").contains(
                std::string(cubey::projects::terrain::kTerrainFieldMountainSaddleGate)),
            "terrain debug manifest should include mountain saddle gate stats");

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
    require(preview.surface == cubey::projects::terrain::TerrainPreviewSurface::Height,
            "terrain preview should default to final height surface");
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
    run_config.terrain.preview_surface = "post-erosion";
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
    require(preview.surface == cubey::projects::terrain::TerrainPreviewSurface::PostErosion,
            "terrain preview should parse the post-erosion surface");
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
    run_config.terrain.preview_color = "height";
    run_config.terrain.preview_surface = "bedrock";
    require_throws(
        [&run_config] {
            static_cast<void>(
                cubey::projects::terrain::terrain_preview_config_from_run_config(run_config));
        },
        "terrain preview should reject unknown surfaces");
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

    cubey::projects::terrain::TerrainPreviewConfig post_preview = preview;
    post_preview.surface = cubey::projects::terrain::TerrainPreviewSurface::PostErosion;
    post_preview.color_mode = cubey::projects::terrain::TerrainPreviewColorMode::Height;
    const cubey::projects::terrain::TerrainPreviewMeshData post_mesh =
        cubey::projects::terrain::make_terrain_preview_mesh(product, post_preview);
    require(post_mesh.vertices.size() == mesh.vertices.size(),
            "terrain post-erosion preview mesh should preserve vertex count");
    float max_height_delta = 0.0F;
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        max_height_delta = std::max(
            max_height_delta,
            std::abs(mesh.vertices[index].position[1] - post_mesh.vertices[index].position[1]));
    }
    require(max_height_delta > 0.1F,
            "terrain post-erosion preview surface should change mesh vertex heights");

    cubey::projects::terrain::TerrainPreviewConfig pre_preview = preview;
    pre_preview.surface = cubey::projects::terrain::TerrainPreviewSurface::PreProcess;
    const cubey::projects::terrain::TerrainPreviewMeshData pre_mesh =
        cubey::projects::terrain::make_terrain_preview_mesh(product, pre_preview);
    require(pre_mesh.vertices.size() == mesh.vertices.size(),
            "terrain pre-process preview mesh should preserve vertex count");
}

} // namespace

int main() {
    test_terrain_region_config_defaults();
    test_terrain_process_field_helpers();
    test_terrain_product_emits_required_fields();
    test_terrain_product_has_useful_ranges();
    test_terrain_river_carves_height_product();
    test_terrain_product_is_deterministic();
    test_terrain_stress_recipe_expands_river_network();
    test_terrain_mountain_range_stress_recipe_exposes_mountain_driver();
    test_terrain_mountain_macro_fields_are_hierarchical();
    test_terrain_mountain_gully_diagnostic_is_bounded();
    test_terrain_mountain_thermal_talus_diagnostic_is_bounded();
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
