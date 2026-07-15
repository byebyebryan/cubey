#include "terrain_source_study.h"

#include "terrain_backdrop_stage.h"

#include <cubey/core/jobs.h>
#include <cubey/engine/capture_queue.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr float kExtentM = 16'384.0F;
constexpr float kDisplayMaximumHeightM = 5'000.0F;
constexpr float kDisplayMaximumSlope = 1.5F;
constexpr std::uint32_t kDefaultGridSize = 1'024U;
constexpr std::array<std::uint64_t, 3> kSeeds{0U, 9012U, 12345U};
constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

struct ReportOptions {
    std::filesystem::path output_dir = "outputs/terrain/source-model-study-v1";
    std::uint32_t grid_size = kDefaultGridSize;
    std::optional<cubey::projects::terrain::TerrainSourceStudyRecipe> recipe{};
    std::optional<std::uint64_t> seed{};
};

struct Distribution {
    float minimum = 0.0F;
    float p01 = 0.0F;
    float p05 = 0.0F;
    float median = 0.0F;
    float p95 = 0.0F;
    float p99 = 0.0F;
    float maximum = 0.0F;
    double mean = 0.0;
};

[[nodiscard]] ReportOptions parse_options(int argc, char** argv) {
    ReportOptions result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--output-dir" && index + 1 < argc) {
            result.output_dir = argv[++index];
        } else if (option == "--grid-size" && index + 1 < argc) {
            result.grid_size = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        } else if (option == "--recipe" && index + 1 < argc) {
            result.recipe =
                cubey::projects::terrain::terrain_source_study_recipe_from_name(argv[++index]);
        } else if (option == "--seed" && index + 1 < argc) {
            result.seed = std::stoull(argv[++index]);
        } else {
            throw std::runtime_error("usage: terrain_source_study_report [--output-dir path] "
                                     "[--grid-size 64..2048] [--recipe id] [--seed integer]");
        }
    }
    if (result.output_dir.empty() || result.grid_size < 64U || result.grid_size > 2'048U) {
        throw std::runtime_error("invalid terrain source study report options");
    }
    if (result.seed.has_value() &&
        std::find(kSeeds.begin(), kSeeds.end(), result.seed.value()) == kSeeds.end()) {
        throw std::runtime_error("terrain source study report seed must be 0, 9012, or 12345");
    }
    return result;
}

[[nodiscard]] float quantile(const std::vector<float>& sorted, float q) {
    const float index = q * static_cast<float>(sorted.size() - 1U);
    const std::size_t lower = static_cast<std::size_t>(std::floor(index));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(index));
    return std::lerp(sorted[lower], sorted[upper], index - static_cast<float>(lower));
}

[[nodiscard]] Distribution distribution(const std::vector<float>& values) {
    if (values.empty()) {
        throw std::runtime_error("cannot summarize an empty terrain source field");
    }
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (const float value : values) {
        sum += value;
    }
    return {
        .minimum = sorted.front(),
        .p01 = quantile(sorted, 0.01F),
        .p05 = quantile(sorted, 0.05F),
        .median = quantile(sorted, 0.50F),
        .p95 = quantile(sorted, 0.95F),
        .p99 = quantile(sorted, 0.99F),
        .maximum = sorted.back(),
        .mean = sum / static_cast<double>(values.size()),
    };
}

[[nodiscard]] nlohmann::json distribution_json(const Distribution& value) {
    return {
        {"min", value.minimum}, {"p01", value.p01}, {"p05", value.p05},     {"p50", value.median},
        {"p95", value.p95},     {"p99", value.p99}, {"max", value.maximum}, {"mean", value.mean},
    };
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) {
    for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
    }
}

[[nodiscard]] std::uint64_t field_hash(const std::vector<float>& values) {
    std::uint64_t hash = kFnvOffset;
    for (const float value : values) {
        hash_u32(hash, std::bit_cast<std::uint32_t>(value));
    }
    return hash;
}

[[nodiscard]] std::vector<float> slopes_from_heights(const std::vector<float>& heights,
                                                     std::uint32_t size, float spacing_m) {
    std::vector<float> result(heights.size(), 0.0F);
    const auto index = [size](std::uint32_t x, std::uint32_t z) {
        return static_cast<std::size_t>(z) * size + x;
    };
    for (std::uint32_t z = 0U; z < size; ++z) {
        const std::uint32_t z0 = z == 0U ? z : z - 1U;
        const std::uint32_t z1 = std::min(z + 1U, size - 1U);
        for (std::uint32_t x = 0U; x < size; ++x) {
            const std::uint32_t x0 = x == 0U ? x : x - 1U;
            const std::uint32_t x1 = std::min(x + 1U, size - 1U);
            const float dx = (heights[index(x1, z)] - heights[index(x0, z)]) /
                             (static_cast<float>(x1 - x0) * spacing_m);
            const float dz = (heights[index(x, z1)] - heights[index(x, z0)]) /
                             (static_cast<float>(z1 - z0) * spacing_m);
            result[index(x, z)] = std::sqrt(dx * dx + dz * dz);
        }
    }
    return result;
}

[[nodiscard]] Distribution local_relief_distribution(const std::vector<float>& heights,
                                                     std::uint32_t size, float spacing_m) {
    const std::uint32_t block_size =
        std::max(1U, static_cast<std::uint32_t>(std::round(1'000.0F / spacing_m)));
    std::vector<float> relief;
    for (std::uint32_t begin_z = 0U; begin_z < size; begin_z += block_size) {
        for (std::uint32_t begin_x = 0U; begin_x < size; begin_x += block_size) {
            float minimum = std::numeric_limits<float>::infinity();
            float maximum = -std::numeric_limits<float>::infinity();
            const std::uint32_t end_z = std::min(begin_z + block_size, size);
            const std::uint32_t end_x = std::min(begin_x + block_size, size);
            for (std::uint32_t z = begin_z; z < end_z; ++z) {
                for (std::uint32_t x = begin_x; x < end_x; ++x) {
                    const float value = heights[static_cast<std::size_t>(z) * size + x];
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                }
            }
            relief.push_back(maximum - minimum);
        }
    }
    return distribution(relief);
}

[[nodiscard]] std::array<std::uint8_t, 3> height_color(float value) {
    const float t = std::clamp(value / kDisplayMaximumHeightM, 0.0F, 1.0F);
    const auto mix = [t](std::uint8_t low, std::uint8_t high) {
        return static_cast<std::uint8_t>(
            std::round(std::lerp(static_cast<float>(low), static_cast<float>(high), t)));
    };
    return {mix(25U, 242U), mix(31U, 240U), mix(36U, 235U)};
}

[[nodiscard]] std::array<std::uint8_t, 3> slope_color(float value) {
    const float t = std::clamp(value / kDisplayMaximumSlope, 0.0F, 1.0F);
    if (t < 0.5F) {
        const float u = t * 2.0F;
        return {
            static_cast<std::uint8_t>(std::lerp(31.0F, 224.0F, u)),
            static_cast<std::uint8_t>(std::lerp(87.0F, 198.0F, u)),
            static_cast<std::uint8_t>(std::lerp(121.0F, 70.0F, u)),
        };
    }
    const float u = (t - 0.5F) * 2.0F;
    return {
        static_cast<std::uint8_t>(std::lerp(224.0F, 170.0F, u)),
        static_cast<std::uint8_t>(std::lerp(198.0F, 45.0F, u)),
        static_cast<std::uint8_t>(std::lerp(70.0F, 45.0F, u)),
    };
}

template <typename ColorFn>
[[nodiscard]] std::vector<std::uint8_t> render_field(const std::vector<float>& values,
                                                     ColorFn color) {
    std::vector<std::uint8_t> pixels(values.size() * 4U);
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const std::array<std::uint8_t, 3> rgb = color(values[index]);
        pixels[index * 4U + 0U] = rgb[0];
        pixels[index * 4U + 1U] = rgb[1];
        pixels[index * 4U + 2U] = rgb[2];
        pixels[index * 4U + 3U] = 255U;
    }
    return pixels;
}

[[nodiscard]] nlohmann::json
stage_json(const cubey::projects::terrain::TerrainBackdropStagePlan& plan) {
    return {
        {"source_focus_xz_m", {plan.source_focus_xz.x, plan.source_focus_xz.y}},
        {"target_height_m", plan.target_height_m},
        {"terrain_vertical_offset_m", plan.terrain_vertical_offset_m},
        {"local_relief_m", plan.local_relief_m},
        {"local_p95_slope", plan.local_p95_slope},
        {"relief_sector_count", plan.relief_sector_count},
        {"lower_frame_clear_sector_count", plan.lower_frame_clear_sector_count},
        {"minimum_lower_frame_terrain_distance_m", plan.minimum_lower_frame_terrain_distance_m},
        {"showcase_yaw_radians", plan.showcase_yaw_radians},
        {"contract_satisfied", plan.contract_satisfied},
        {"score", plan.score},
    };
}

} // namespace

int main(int argc, char** argv) {
    using namespace cubey::projects::terrain;

    try {
        const ReportOptions options = parse_options(argc, argv);
        const float spacing_m = 2.0F * kExtentM / static_cast<float>(options.grid_size - 1U);
        std::filesystem::create_directories(options.output_dir / "fields");

        cubey::jobs::JobSystem sample_jobs(std::max(1U, std::thread::hardware_concurrency()));
        cubey::jobs::JobSystem encode_jobs(2U);
        cubey::CaptureQueue captures(encode_jobs);
        cubey::CaptureBacklog backlog(4U);
        nlohmann::json recipe_entries = nlohmann::json::array();

        for (const TerrainSourceStudyRecipeInfo& info : terrain_source_study_recipes()) {
            if (options.recipe.has_value() && options.recipe.value() != info.recipe) {
                continue;
            }
            const auto calibration_begin = std::chrono::steady_clock::now();
            const TerrainSourceStudyCalibration calibration =
                terrain_source_study_calibration(info.recipe);
            const auto calibration_end = std::chrono::steady_clock::now();
            nlohmann::json seed_entries = nlohmann::json::array();

            for (const std::uint64_t seed : kSeeds) {
                if (options.seed.has_value() && options.seed.value() != seed) {
                    continue;
                }
                const TerrainSourceStudySource source(info.recipe, seed, calibration);
                const std::size_t sample_count =
                    static_cast<std::size_t>(options.grid_size) * options.grid_size;
                std::vector<float> raw_heights(sample_count);
                std::vector<float> heights(sample_count);
                constexpr std::uint32_t kRowsPerJob = 16U;
                const std::uint32_t job_count =
                    (options.grid_size + kRowsPerJob - 1U) / kRowsPerJob;
                std::vector<cubey::jobs::JobHandle<void>> handles;
                handles.reserve(job_count);
                const auto sampling_begin = std::chrono::steady_clock::now();
                for (std::uint32_t job = 0U; job < job_count; ++job) {
                    const std::uint32_t begin_z = job * kRowsPerJob;
                    const std::uint32_t end_z = std::min(begin_z + kRowsPerJob, options.grid_size);
                    handles.push_back(sample_jobs.submit([&, begin_z, end_z] {
                        for (std::uint32_t z = begin_z; z < end_z; ++z) {
                            for (std::uint32_t x = 0U; x < options.grid_size; ++x) {
                                const std::size_t index =
                                    static_cast<std::size_t>(z) * options.grid_size + x;
                                const TerrainQuery query{
                                    .world_xz = {-kExtentM + static_cast<float>(x) * spacing_m,
                                                 -kExtentM + static_cast<float>(z) * spacing_m},
                                    .footprint_m = spacing_m,
                                };
                                raw_heights[index] = source.sample_raw_height(query);
                                heights[index] = source.sample_height(query);
                            }
                        }
                    }));
                }
                for (auto& handle : handles) {
                    handle.get();
                }
                const auto sampling_end = std::chrono::steady_clock::now();
                std::vector<float> slopes =
                    slopes_from_heights(heights, options.grid_size, spacing_m);

                const std::filesystem::path recipe_dir =
                    options.output_dir / "fields" / std::string(info.id);
                const std::filesystem::path height_path =
                    recipe_dir / ("seed-" + std::to_string(seed) + "-height.png");
                const std::filesystem::path slope_path =
                    recipe_dir / ("seed-" + std::to_string(seed) + "-slope.png");
                backlog.enqueue(captures.enqueue_png({
                    .output_path = height_path,
                    .width = options.grid_size,
                    .height = options.grid_size,
                    .rgba8 = render_field(heights, height_color),
                }));
                backlog.enqueue(captures.enqueue_png({
                    .output_path = slope_path,
                    .width = options.grid_size,
                    .height = options.grid_size,
                    .rgba8 = render_field(slopes, slope_color),
                }));

                const TerrainBackdropStagePlan stage = plan_terrain_backdrop_stage(
                    source, terrain_backdrop_stage_request(TerrainBackdropStageMode::Detached));
                const double sampling_ms =
                    std::chrono::duration<double, std::milli>(sampling_end - sampling_begin)
                        .count();
                seed_entries.push_back({
                    {"seed", seed},
                    {"raw_height", distribution_json(distribution(raw_heights))},
                    {"height_m", distribution_json(distribution(heights))},
                    {"slope", distribution_json(distribution(slopes))},
                    {"local_relief_1km_m", distribution_json(local_relief_distribution(
                                               heights, options.grid_size, spacing_m))},
                    {"content_hash", field_hash(heights)},
                    {"sampling_ms", sampling_ms},
                    {"samples_per_second",
                     static_cast<double>(sample_count) * 1'000.0 / sampling_ms},
                    {"stage", stage_json(stage)},
                    {"height_image",
                     height_path.lexically_relative(options.output_dir).generic_string()},
                    {"slope_image",
                     slope_path.lexically_relative(options.output_dir).generic_string()},
                });
            }

            recipe_entries.push_back({
                {"id", info.id},
                {"operator_family", info.operator_family},
                {"reference", info.reference},
                {"calibration",
                 {
                     {"raw_p05", calibration.raw_p05},
                     {"raw_p95", calibration.raw_p95},
                     {"scale_m", calibration.scale_m},
                     {"sample_count", calibration.sample_count},
                     {"elapsed_ms",
                      std::chrono::duration<double, std::milli>(calibration_end - calibration_begin)
                          .count()},
                 }},
                {"seeds", std::move(seed_entries)},
            });
        }

        backlog.finish_all();
        sample_jobs.shutdown();
        encode_jobs.shutdown();
        const nlohmann::json report{
            {"schema", "cubey.terrain.source-model-study.v1"},
            {"domain",
             {
                 {"extent_m", {-kExtentM, kExtentM}},
                 {"grid_size", options.grid_size},
                 {"spacing_m", spacing_m},
                 {"footprint_m", spacing_m},
             }},
            {"display",
             {
                 {"height_range_m", {0, kDisplayMaximumHeightM}},
                 {"slope_range", {0, kDisplayMaximumSlope}},
             }},
            {"calibration",
             {
                 {"seeds", kSeeds},
                 {"grid_size", 257},
                 {"extent_m", {-kExtentM, kExtentM}},
                 {"mapped_percentiles", {"p05", "p95"}},
                 {"mapped_range_m", {0, 3500}},
                 {"upper_clamped", false},
             }},
            {"recipes", std::move(recipe_entries)},
        };
        const std::filesystem::path report_path = options.output_dir / "source-report.json";
        std::ofstream output(report_path);
        if (!output) {
            throw std::runtime_error("failed to open terrain source study report: " +
                                     report_path.string());
        }
        output << report.dump(2) << '\n';
        std::cout << "terrain source study report: wrote " << report_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_source_study_report: " << error.what() << '\n';
        return 1;
    }
}
