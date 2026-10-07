#include "fluid_25d_bank_controls.h"

#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/memory_barriers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#ifndef CUBEY_FLUID_25D_SHADER_DIR
#error "CUBEY_FLUID_25D_SHADER_DIR must be defined by the fluid_25d CMake target"
#endif

namespace cubey::projects::fluid::fluid_25d {
namespace {

constexpr std::uint32_t kGridWidth = 32U;
constexpr std::uint32_t kGridHeight = 20U;
constexpr std::size_t kCellCount = static_cast<std::size_t>(kGridWidth) * kGridHeight;
constexpr std::uint32_t kWorkgroupSize = 32U;
constexpr std::uint32_t kMeshSubdivision = 4U;
constexpr float kOrdinaryWetThreshold = 0.0001F;
constexpr float kBsplineNativeWetThreshold = 0.000001F;
constexpr float kVisibleDepthLevel = 0.026F;
constexpr float kTolerance = 2.0e-7F;
constexpr double kBsplineToleranceM = 2.0e-6;
constexpr double kLakeStageToleranceM = 2.0e-6;
constexpr double kLakeStageM = 0.5;
constexpr VkBufferUsageFlags kScratchBufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT;

struct BankControlPushConstants {
    std::array<std::uint32_t, 4> grid_query{};
};
static_assert(sizeof(BankControlPushConstants) == sizeof(std::uint32_t) * 4U);

struct BankControlCase {
    std::string_view label;
    std::array<float, 4> query{};
    std::array<float, 4> expected{};
};
static_assert(sizeof(std::array<float, 4>) == sizeof(float) * 4U);

enum class BsplineControlKind : std::uint8_t {
    Constant,
    Affine,
    Range,
    Boundary,
    Stream,
    Gap,
    ThinFilmGap,
    Junction,
    RecessionEarly,
    RecessionLate,
    LakeRest,
    PartialDryLake,
};

struct BsplineControlQuery {
    std::string_view label;
    std::array<float, 4> query{};
    BsplineControlKind kind = BsplineControlKind::Constant;
};
static_assert(sizeof(std::array<float, 12>) == sizeof(float) * 12U);

struct MaskRegion {
    std::string_view label;
    BsplineControlKind kind = BsplineControlKind::Constant;
    std::size_t first_query = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

struct BsplineMetrics {
    std::size_t constant_queries = 0U;
    std::size_t affine_queries = 0U;
    std::size_t range_queries = 0U;
    std::size_t boundary_queries = 0U;
    std::size_t mask_queries = 0U;
    std::size_t lake_rest_queries = 0U;
    std::size_t partial_dry_queries = 0U;
    std::size_t partial_dry_near_shore_queries = 0U;
    double max_cpu_oracle_error_m = 0.0;
    double max_affine_error_m = 0.0;
    double max_flat_lake_stage_error_m = 0.0;
    double partial_dry_max_stage_deviation_m = 0.0;
    std::size_t bilinear_visible_count = 0U;
    std::size_t bspline_visible_count = 0U;
    std::int64_t visible_level_count_delta = 0;
    std::size_t stream_components = 0U;
    std::size_t gap_components = 0U;
    std::size_t thin_film_gap_components = 0U;
    bool positive_film_gap_bridged = false;
    std::size_t junction_components = 0U;
    std::size_t recession_early_visible_count = 0U;
    std::size_t recession_late_visible_count = 0U;
    std::size_t recession_subset_violations = 0U;
};

[[noreturn]] void fail(std::string_view test, std::string_view detail) {
    throw std::runtime_error("fluid 2.5D bank GPU control failed: " + std::string(test) + ": " +
                             std::string(detail));
}

void require(bool condition, std::string_view test, std::string_view detail) {
    if (!condition) {
        fail(test, detail);
    }
}

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FLUID_25D_SHADER_DIR) / filename;
}

std::vector<std::uint8_t> bytes_of(const std::vector<float>& values) {
    std::vector<std::uint8_t> bytes(values.size() * sizeof(float));
    if (!bytes.empty()) {
        std::memcpy(bytes.data(), values.data(), bytes.size());
    }
    return bytes;
}

template <std::size_t Count>
std::vector<std::uint8_t> bytes_of(const std::vector<std::array<float, Count>>& values) {
    std::vector<std::uint8_t> bytes(values.size() * sizeof(values.front()));
    if (!bytes.empty()) {
        std::memcpy(bytes.data(), values.data(), bytes.size());
    }
    return bytes;
}

std::vector<std::uint8_t> read_buffer(cubey::ProjectGpuServices& gpu,
                                      const cubey::vulkan::Buffer& buffer, const char* label) {
    std::vector<std::uint8_t> bytes = gpu.readback_buffer(buffer.handle(), buffer.size(), label);
    if (bytes.size() != static_cast<std::size_t>(buffer.size())) {
        fail(label, "GPU readback has an unexpected byte size");
    }
    return bytes;
}

std::size_t cell_index(std::uint32_t x, std::uint32_t y) {
    return static_cast<std::size_t>(y) * kGridWidth + x;
}

void set_vertex(std::vector<float>& depths, std::uint32_t x, std::uint32_t y, float value) {
    require(x < kGridWidth && y < kGridHeight, "fixture construction",
            "vertex coordinate is outside the scratch grid");
    depths[cell_index(x, y)] = value;
}

void set_quad(std::vector<float>& depths, std::uint32_t x, std::uint32_t y,
              const std::array<float, 4>& values) {
    require(x + 1U < kGridWidth && y + 1U < kGridHeight, "fixture construction",
            "quad coordinate is outside the scratch grid");
    set_vertex(depths, x, y, values[0]);
    set_vertex(depths, x + 1U, y, values[1]);
    set_vertex(depths, x, y + 1U, values[2]);
    set_vertex(depths, x + 1U, y + 1U, values[3]);
}

std::uint32_t append_zero_plane(std::vector<float>& depths) {
    require(depths.size() % kCellCount == 0U, "fixture construction",
            "depth planes must contain a whole number of grids");
    const std::uint32_t plane = static_cast<std::uint32_t>(depths.size() / kCellCount);
    depths.resize(depths.size() + kCellCount, 0.0F);
    return plane;
}

std::uint32_t append_fixture_plane(std::vector<float>& depths, std::vector<float>& terrain) {
    require(depths.size() == terrain.size(), "fixture construction",
            "depth and terrain plane counts must match");
    const std::uint32_t plane = append_zero_plane(depths);
    require(append_zero_plane(terrain) == plane, "fixture construction",
            "depth and terrain plane indices diverged");
    return plane;
}

void set_plane_vertex(std::vector<float>& depths, std::uint32_t plane, std::uint32_t x,
                      std::uint32_t y, float value) {
    require(x < kGridWidth && y < kGridHeight &&
                (static_cast<std::size_t>(plane) + 1U) * kCellCount <= depths.size(),
            "fixture construction", "B-spline vertex is outside its scratch plane");
    depths[static_cast<std::size_t>(plane) * kCellCount + cell_index(x, y)] = value;
}

std::array<double, 4> cpu_bspline_weights(double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double one_minus_t = 1.0 - t;
    return {one_minus_t * one_minus_t * one_minus_t / 6.0, (3.0 * t3 - 6.0 * t2 + 4.0) / 6.0,
            (-3.0 * t3 + 3.0 * t2 + 3.0 * t + 1.0) / 6.0, t3 / 6.0};
}

enum class CpuField : std::uint8_t {
    Depth,
    Terrain,
    Stage
};

double cpu_field_value(const std::vector<float>& depths, const std::vector<float>& terrain,
                       std::uint32_t plane, std::size_t cell, CpuField field) {
    const float depth = depths[static_cast<std::size_t>(plane) * kCellCount + cell];
    const float bed = terrain[static_cast<std::size_t>(plane) * kCellCount + cell];
    switch (field) {
    case CpuField::Depth:
        return static_cast<double>(depth);
    case CpuField::Terrain:
        return static_cast<double>(bed);
    case CpuField::Stage:
        // Match the source field's float32 bed+depth value, then accumulate
        // the independent reference in double precision.
        return static_cast<double>(bed + depth);
    }
    return 0.0;
}

struct CpuBsplineSample {
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
};

CpuBsplineSample cpu_bspline_sample(const std::vector<float>& depths,
                                    const std::vector<float>& terrain, std::uint32_t plane,
                                    double x, double y, CpuField field) {
    const double bounded_x = std::clamp(x, 0.0, static_cast<double>(kGridWidth - 1U));
    const double bounded_y = std::clamp(y, 0.0, static_cast<double>(kGridHeight - 1U));
    const auto base_x = static_cast<std::int32_t>(std::floor(bounded_x));
    const auto base_y = static_cast<std::int32_t>(std::floor(bounded_y));
    const auto weights_x = cpu_bspline_weights(bounded_x - std::floor(bounded_x));
    const auto weights_y = cpu_bspline_weights(bounded_y - std::floor(bounded_y));
    CpuBsplineSample result{.value = 0.0,
                            .minimum = std::numeric_limits<double>::infinity(),
                            .maximum = -std::numeric_limits<double>::infinity()};
    double weight_sum = 0.0;
    for (std::int32_t j = 0; j < 4; ++j) {
        for (std::int32_t i = 0; i < 4; ++i) {
            const auto cell_x = static_cast<std::uint32_t>(
                std::clamp(base_x + i - 1, 0, static_cast<std::int32_t>(kGridWidth - 1U)));
            const auto cell_y = static_cast<std::uint32_t>(
                std::clamp(base_y + j - 1, 0, static_cast<std::int32_t>(kGridHeight - 1U)));
            const std::size_t cell = cell_index(cell_x, cell_y);
            const double sample = cpu_field_value(depths, terrain, plane, cell, field);
            const double weight =
                weights_x[static_cast<std::size_t>(i)] * weights_y[static_cast<std::size_t>(j)];
            result.value += sample * weight;
            weight_sum += weight;
            if (field == CpuField::Depth) {
                result.minimum = std::min(result.minimum, sample);
                result.maximum = std::max(result.maximum, sample);
            }
        }
    }
    require(std::abs(weight_sum - 1.0) <= 1.0e-12, "CPU B-spline oracle",
            "cubic B-spline tensor weights do not sum to one");
    if (field != CpuField::Depth) {
        result.minimum = result.value;
        result.maximum = result.value;
    }
    return result;
}

double cpu_mesh_bspline_sample(const std::vector<float>& depths, const std::vector<float>& terrain,
                               std::uint32_t plane, double x, double y, CpuField field) {
    const double bounded_x = std::clamp(x, 0.0, static_cast<double>(kGridWidth - 1U));
    const double bounded_y = std::clamp(y, 0.0, static_cast<double>(kGridHeight - 1U));
    const double subdivision = static_cast<double>(kMeshSubdivision);
    const double lo_x = std::floor(bounded_x * subdivision) / subdivision;
    const double lo_y = std::floor(bounded_y * subdivision) / subdivision;
    const double hi_x = std::min(lo_x + 1.0 / subdivision, static_cast<double>(kGridWidth - 1U));
    const double hi_y = std::min(lo_y + 1.0 / subdivision, static_cast<double>(kGridHeight - 1U));
    const double fraction_x = bounded_x * subdivision - std::floor(bounded_x * subdivision);
    const double fraction_y = bounded_y * subdivision - std::floor(bounded_y * subdivision);
    const double a = cpu_bspline_sample(depths, terrain, plane, lo_x, lo_y, field).value;
    const double b = cpu_bspline_sample(depths, terrain, plane, hi_x, lo_y, field).value;
    const double c = cpu_bspline_sample(depths, terrain, plane, lo_x, hi_y, field).value;
    const double d = cpu_bspline_sample(depths, terrain, plane, hi_x, hi_y, field).value;
    return fraction_x >= fraction_y
               ? a * (1.0 - fraction_x) + b * (fraction_x - fraction_y) + d * fraction_y
               : a * (1.0 - fraction_y) + c * (fraction_y - fraction_x) + d * fraction_x;
}

double cpu_wet_support(const std::vector<float>& depths, std::uint32_t plane, double x, double y,
                       double wet_threshold) {
    const double bounded_x = std::clamp(x, 0.0, static_cast<double>(kGridWidth - 1U));
    const double bounded_y = std::clamp(y, 0.0, static_cast<double>(kGridHeight - 1U));
    const auto lower_x = static_cast<std::uint32_t>(std::floor(bounded_x));
    const auto lower_y = static_cast<std::uint32_t>(std::floor(bounded_y));
    const auto upper_x = std::min(lower_x + 1U, kGridWidth - 1U);
    const auto upper_y = std::min(lower_y + 1U, kGridHeight - 1U);
    const double fraction_x = bounded_x - static_cast<double>(lower_x);
    const double fraction_y = bounded_y - static_cast<double>(lower_y);
    const auto wet = [&](std::uint32_t cx, std::uint32_t cy) {
        const float depth =
            depths[static_cast<std::size_t>(plane) * kCellCount + cell_index(cx, cy)];
        return depth > wet_threshold ? 1.0 : 0.0;
    };
    const double lower = std::lerp(wet(lower_x, lower_y), wet(upper_x, lower_y), fraction_x);
    const double upper = std::lerp(wet(lower_x, upper_y), wet(upper_x, upper_y), fraction_x);
    return std::lerp(lower, upper, fraction_y);
}

std::size_t append_query(std::vector<BsplineControlQuery>& queries, std::string_view label,
                         BsplineControlKind kind, std::uint32_t plane, float x, float y) {
    queries.push_back({label, {x, y, kBsplineNativeWetThreshold, static_cast<float>(plane)}, kind});
    return queries.size() - 1U;
}

MaskRegion append_region(std::vector<BsplineControlQuery>& queries, std::string_view label,
                         BsplineControlKind kind, std::uint32_t plane, float x0, float y0,
                         std::uint32_t width, std::uint32_t height, float spacing) {
    const std::size_t first = queries.size();
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            append_query(queries, label, kind, plane, x0 + static_cast<float>(x) * spacing,
                         y0 + static_cast<float>(y) * spacing);
        }
    }
    return {label, kind, first, width, height};
}

std::uint32_t connected_components(const std::vector<std::uint8_t>& mask, std::uint32_t width,
                                   std::uint32_t height) {
    require(mask.size() == static_cast<std::size_t>(width) * height, "mask analysis",
            "sampled mask dimensions do not match its query count");
    std::vector<std::uint8_t> visited(mask.size(), 0U);
    std::queue<std::size_t> pending;
    std::uint32_t components = 0U;
    for (std::size_t seed = 0U; seed < mask.size(); ++seed) {
        if (mask[seed] == 0U || visited[seed] != 0U) {
            continue;
        }
        ++components;
        visited[seed] = 1U;
        pending.push(seed);
        while (!pending.empty()) {
            const std::size_t index = pending.front();
            pending.pop();
            const std::uint32_t x = static_cast<std::uint32_t>(index % width);
            const std::uint32_t y = static_cast<std::uint32_t>(index / width);
            const auto visit = [&](std::uint32_t nx, std::uint32_t ny) {
                const std::size_t neighbor = static_cast<std::size_t>(ny) * width + nx;
                if (mask[neighbor] != 0U && visited[neighbor] == 0U) {
                    visited[neighbor] = 1U;
                    pending.push(neighbor);
                }
            };
            if (x > 0U)
                visit(x - 1U, y);
            if (x + 1U < width)
                visit(x + 1U, y);
            if (y > 0U)
                visit(x, y - 1U);
            if (y + 1U < height)
                visit(x, y + 1U);
        }
    }
    return components;
}

std::vector<BankControlCase> make_cases(std::vector<float>& depths) {
    depths.assign(static_cast<std::size_t>(kGridWidth) * kGridHeight, 0.0F);
    std::vector<BankControlCase> cases;
    const auto add = [&](std::string_view label, float x, float y, float threshold, float triangle,
                         float bilinear, float supported, float wet_support) {
        cases.push_back(
            {label, {x, y, threshold, 0.0F}, {triangle, bilinear, supported, wet_support}});
    };

    set_quad(depths, 1U, 1U, {0.05F, 0.05F, 0.05F, 0.05F});
    add("all-wet flat rest", 1.25F, 1.75F, kOrdinaryWetThreshold, 0.05F, 0.05F, 0.05F, 1.0F);

    set_quad(depths, 5U, 1U, {0.01F, 0.03F, 0.02F, 0.04F});
    add("shallow linear slope", 5.25F, 1.75F, kOrdinaryWetThreshold, 0.0225F, 0.0225F, 0.0225F,
        1.0F);

    set_quad(depths, 9U, 1U, {0.1F, 1.1F, 1.1F, 0.1F});
    add("saddle triangle half x>=y", 9.75F, 1.25F, kOrdinaryWetThreshold, 0.6F, 0.725F, 0.725F,
        1.0F);
    add("saddle triangle half x<y", 9.25F, 1.75F, kOrdinaryWetThreshold, 0.6F, 0.725F, 0.725F,
        1.0F);

    set_quad(depths, 13U, 1U, {0.0F, 0.0F, 0.0F, 0.0F});
    add("all-dry quad", 13.5F, 1.5F, kOrdinaryWetThreshold, 0.0F, 0.0F, 0.0F, 0.0F);

    set_quad(depths, 17U, 1U, {0.1F, 0.0F, 0.0F, 0.0F});
    add("single-wet narrow lobe", 17.25F, 1.25F, kOrdinaryWetThreshold, 0.075F, 0.05625F, 0.05625F,
        0.5625F);
    add("single-wet center rejected", 17.5F, 1.5F, kOrdinaryWetThreshold, 0.05F, 0.025F, 0.0F,
        0.25F);

    set_quad(depths, 21U, 1U, {0.1F, 0.0F, 0.0F, 0.1F});
    add("diagonal wet corners center rejected", 21.5F, 1.5F, kOrdinaryWetThreshold, 0.1F, 0.05F,
        0.0F, 0.5F);
    add("diagonal wet corner lower lobe", 21.25F, 1.25F, kOrdinaryWetThreshold, 0.1F, 0.0625F,
        0.0625F, 0.625F);
    add("diagonal wet corner upper lobe", 21.75F, 1.75F, kOrdinaryWetThreshold, 0.1F, 0.0625F,
        0.0625F, 0.625F);

    set_quad(depths, 25U, 1U, {0.0F, 0.02F, 0.02F, 0.12F});
    add("three-way wet junction", 25.5F, 1.5F, 0.01F, 0.06F, 0.04F, 0.04F, 0.75F);

    set_quad(depths, 29U, 1U, {0.0004F, 0.00005F, 0.00005F, 0.00005F});
    add("receding thin-film edge", 29.25F, 1.25F, kOrdinaryWetThreshold, 0.0003125F, 0.000246875F,
        0.000246875F, 0.5625F);
    add("receding center support rejected", 29.5F, 1.5F, kOrdinaryWetThreshold, 0.000225F,
        0.0001375F, 0.0F, 0.25F);

    for (std::uint32_t y = 6U; y <= 7U; ++y) {
        set_vertex(depths, 1U, y, 1.0F);
        set_vertex(depths, 2U, y, 0.0F);
        set_vertex(depths, 3U, y, 1.0F);
    }
    add("dry column left strip", 1.25F, 6.5F, 0.01F, 0.75F, 0.75F, 0.75F, 0.75F);
    add("dry column center gap", 2.0F, 6.5F, 0.01F, 0.0F, 0.0F, 0.0F, 0.0F);
    add("dry column right strip", 2.75F, 6.5F, 0.01F, 0.75F, 0.75F, 0.75F, 0.75F);

    for (std::uint32_t x = 8U; x <= 9U; ++x) {
        set_vertex(depths, x, 6U, 1.0F);
        set_vertex(depths, x, 7U, 0.0F);
        set_vertex(depths, x, 8U, 1.0F);
    }
    add("dry row lower strip", 8.5F, 6.25F, 0.01F, 0.75F, 0.75F, 0.75F, 0.75F);
    add("dry row center gap", 8.5F, 7.0F, 0.01F, 0.0F, 0.0F, 0.0F, 0.0F);
    add("dry row upper strip", 8.5F, 7.75F, 0.01F, 0.75F, 0.75F, 0.75F, 0.75F);

    set_vertex(depths, 0U, 0U, 0.025F);
    add("origin boundary", 0.0F, 0.0F, kOrdinaryWetThreshold, 0.025F, 0.025F, 0.025F, 1.0F);
    set_vertex(depths, 16U, 12U, 0.037F);
    add("exact internal grid boundary", 16.0F, 12.0F, kOrdinaryWetThreshold, 0.037F, 0.037F, 0.037F,
        1.0F);
    set_vertex(depths, kGridWidth - 1U, kGridHeight - 1U, 0.027F);
    add("maximum grid boundary", static_cast<float>(kGridWidth - 1U),
        static_cast<float>(kGridHeight - 1U), kOrdinaryWetThreshold, 0.027F, 0.027F, 0.027F, 1.0F);

    require(cases.size() >= 12U, "fixture construction",
            "bank sampler control matrix must cover at least twelve samples");
    return cases;
}

struct BsplineFixtureSet {
    std::vector<BsplineControlQuery> queries;
    std::vector<MaskRegion> mask_regions;
    std::size_t recession_early_region = 0U;
    std::size_t recession_late_region = 0U;
    std::size_t partial_dry_region = 0U;
    std::uint32_t stream_plane = 0U;
    std::uint32_t gap_plane = 0U;
    std::uint32_t thin_film_gap_plane = 0U;
    std::uint32_t junction_plane = 0U;
    std::uint32_t recession_early_plane = 0U;
    std::uint32_t recession_late_plane = 0U;
    std::uint32_t lake_rest_plane = 0U;
    std::uint32_t partial_dry_plane = 0U;
};

BsplineFixtureSet make_bspline_fixtures(std::vector<float>& depths, std::vector<float>& terrain) {
    require(depths.size() == kCellCount, "fixture construction",
            "legacy depth fixtures must occupy exactly one initial plane");
    terrain.assign(kCellCount, 0.0F);
    BsplineFixtureSet fixtures;

    const std::uint32_t constant_plane = append_fixture_plane(depths, terrain);
    std::fill_n(depths.begin() + static_cast<std::ptrdiff_t>(constant_plane * kCellCount),
                kCellCount, 0.05F);
    static_cast<void>(append_region(fixtures.queries, "constant field",
                                    BsplineControlKind::Constant, constant_plane, 0.0F, 0.0F, 125U,
                                    77U, 0.25F));

    const std::uint32_t affine_plane = append_fixture_plane(depths, terrain);
    for (std::uint32_t y = 0U; y < kGridHeight; ++y) {
        for (std::uint32_t x = 0U; x < kGridWidth; ++x) {
            set_plane_vertex(depths, affine_plane, x, y,
                             0.2F + 0.001F * static_cast<float>(x) -
                                 0.002F * static_cast<float>(y));
        }
    }
    static_cast<void>(append_region(fixtures.queries, "interior affine field",
                                    BsplineControlKind::Affine, affine_plane, 3.0F, 3.0F, 101U, 53U,
                                    0.25F));

    const std::uint32_t extrema_plane = append_fixture_plane(depths, terrain);
    for (std::uint32_t y = 0U; y < kGridHeight; ++y) {
        for (std::uint32_t x = 0U; x < kGridWidth; ++x) {
            set_plane_vertex(depths, extrema_plane, x, y, x < 16U ? 0.005F : 0.04F);
        }
    }
    set_plane_vertex(depths, extrema_plane, 14U, 10U, 0.5F);
    static_cast<void>(append_region(fixtures.queries, "positive step and spike",
                                    BsplineControlKind::Range, extrema_plane, 10.0F, 6.0F, 41U, 33U,
                                    0.25F));

    const std::uint32_t boundary_plane = append_fixture_plane(depths, terrain);
    for (std::uint32_t y = 0U; y < kGridHeight; ++y) {
        for (std::uint32_t x = 0U; x < kGridWidth; ++x) {
            set_plane_vertex(depths, boundary_plane, x, y,
                             1.0F + 0.1F * static_cast<float>(x) + 0.2F * static_cast<float>(y));
        }
    }
    for (const auto [x, y] : std::array<std::pair<float, float>, 8>{
             std::pair{0.0F, 0.0F}, std::pair{0.25F, 0.0F}, std::pair{0.0F, 0.25F},
             std::pair{1.0F, 0.0F},
             std::pair{static_cast<float>(kGridWidth - 1U), static_cast<float>(kGridHeight - 1U)},
             std::pair{static_cast<float>(kGridWidth - 1U) - 0.25F,
                       static_cast<float>(kGridHeight - 1U)},
             std::pair{static_cast<float>(kGridWidth - 1U),
                       static_cast<float>(kGridHeight - 1U) - 0.25F},
             std::pair{static_cast<float>(kGridWidth - 1U) - 1.0F,
                       static_cast<float>(kGridHeight - 1U) - 1.0F}}) {
        append_query(fixtures.queries, "clamped edge extension", BsplineControlKind::Boundary,
                     boundary_plane, x, y);
    }

    const std::uint32_t topology_plane = append_fixture_plane(depths, terrain);
    for (std::uint32_t y = 3U; y <= 16U; ++y) {
        set_plane_vertex(depths, topology_plane, 4U, y, 0.08F);
        set_plane_vertex(depths, topology_plane, 10U, y, 0.08F);
        set_plane_vertex(depths, topology_plane, 12U, y, 0.08F);
    }
    for (std::uint32_t x = 18U; x <= 24U; ++x) {
        set_plane_vertex(depths, topology_plane, x, 10U, 0.08F);
    }
    for (std::uint32_t y = 6U; y <= 10U; ++y) {
        set_plane_vertex(depths, topology_plane, 20U, y, 0.08F);
    }
    fixtures.stream_plane = topology_plane;
    fixtures.gap_plane = topology_plane;
    fixtures.junction_plane = topology_plane;
    fixtures.mask_regions.push_back(append_region(fixtures.queries, "one-cell wet stream",
                                                  BsplineControlKind::Stream, topology_plane, 2.0F,
                                                  2.0F, 41U, 121U, 0.125F));
    fixtures.mask_regions.push_back(append_region(fixtures.queries, "one-cell dry gap",
                                                  BsplineControlKind::Gap, topology_plane, 8.0F,
                                                  2.0F, 49U, 121U, 0.125F));
    fixtures.mask_regions.push_back(append_region(fixtures.queries, "three-way junction",
                                                  BsplineControlKind::Junction, topology_plane,
                                                  16.0F, 5.0F, 65U, 81U, 0.125F));

    fixtures.thin_film_gap_plane = append_fixture_plane(depths, terrain);
    std::fill_n(depths.begin() +
                    static_cast<std::ptrdiff_t>(fixtures.thin_film_gap_plane * kCellCount),
                kCellCount, 0.001F);
    for (std::uint32_t y = 3U; y <= 16U; ++y) {
        set_plane_vertex(depths, fixtures.thin_film_gap_plane, 10U, y, 0.1F);
        set_plane_vertex(depths, fixtures.thin_film_gap_plane, 12U, y, 0.1F);
    }
    fixtures.mask_regions.push_back(
        append_region(fixtures.queries, "thin positive film gap", BsplineControlKind::ThinFilmGap,
                      fixtures.thin_film_gap_plane, 8.0F, 2.0F, 49U, 121U, 0.125F));

    fixtures.recession_early_plane = append_fixture_plane(depths, terrain);
    fixtures.recession_late_plane = append_fixture_plane(depths, terrain);
    for (std::uint32_t y = 6U; y <= 13U; ++y) {
        for (std::uint32_t x = 26U; x <= 29U; ++x) {
            const std::uint32_t edge_distance = std::min({x - 26U, 29U - x, y - 6U, 13U - y});
            const float early = 0.04F + 0.015F * static_cast<float>(edge_distance);
            set_plane_vertex(depths, fixtures.recession_early_plane, x, y, early);
            set_plane_vertex(depths, fixtures.recession_late_plane, x, y, 0.6F * early);
        }
    }
    fixtures.recession_early_region = fixtures.mask_regions.size();
    fixtures.mask_regions.push_back(
        append_region(fixtures.queries, "recession early", BsplineControlKind::RecessionEarly,
                      fixtures.recession_early_plane, 24.0F, 4.0F, 57U, 97U, 0.125F));
    fixtures.recession_late_region = fixtures.mask_regions.size();
    fixtures.mask_regions.push_back(
        append_region(fixtures.queries, "recession late", BsplineControlKind::RecessionLate,
                      fixtures.recession_late_plane, 24.0F, 4.0F, 57U, 97U, 0.125F));

    fixtures.lake_rest_plane = append_fixture_plane(depths, terrain);
    for (std::uint32_t y = 0U; y < kGridHeight; ++y) {
        const double v = 2.0 * static_cast<double>(y) / static_cast<double>(kGridHeight - 1U) - 1.0;
        for (std::uint32_t x = 0U; x < kGridWidth; ++x) {
            const double u =
                2.0 * static_cast<double>(x) / static_cast<double>(kGridWidth - 1U) - 1.0;
            const float bed = static_cast<float>(0.12 * u * u + 0.07 * v * v + 0.025 * u * v);
            set_plane_vertex(terrain, fixtures.lake_rest_plane, x, y, bed);
            set_plane_vertex(depths, fixtures.lake_rest_plane, x, y,
                             static_cast<float>(kLakeStageM) - bed);
        }
    }
    static_cast<void>(append_region(fixtures.queries, "fully wet nonplanar lake at rest",
                                    BsplineControlKind::LakeRest, fixtures.lake_rest_plane, 0.0F,
                                    0.0F, 125U, 77U, 0.25F));

    constexpr double partial_dry_stage = 0.36;
    fixtures.partial_dry_plane = append_fixture_plane(depths, terrain);
    std::size_t wet_source_cells = 0U;
    std::size_t dry_source_cells = 0U;
    for (std::uint32_t y = 0U; y < kGridHeight; ++y) {
        const double v = (static_cast<double>(y) - 9.5) / 9.5;
        for (std::uint32_t x = 0U; x < kGridWidth; ++x) {
            const double bed_value =
                partial_dry_stage + 0.015 * (static_cast<double>(x) - 15.5) + 0.01 * v * v;
            const float bed = static_cast<float>(bed_value);
            const float h = static_cast<float>(std::max(partial_dry_stage - bed_value, 0.0));
            set_plane_vertex(terrain, fixtures.partial_dry_plane, x, y, bed);
            set_plane_vertex(depths, fixtures.partial_dry_plane, x, y, h);
            if (h > kOrdinaryWetThreshold) {
                ++wet_source_cells;
            } else {
                ++dry_source_cells;
            }
        }
    }
    require(wet_source_cells > 0U && dry_source_cells > 0U, "partial-dry lake fixture",
            "shoreline fixture must contain native cells both below and above the lake stage");
    fixtures.partial_dry_region = fixtures.mask_regions.size();
    fixtures.mask_regions.push_back(
        append_region(fixtures.queries, "partial-dry lake shoreline counterexample",
                      BsplineControlKind::PartialDryLake, fixtures.partial_dry_plane, 10.0F, 4.0F,
                      97U, 89U, 0.125F));
    return fixtures;
}

void validate_result(const std::vector<std::uint8_t>& bytes,
                     const std::vector<BankControlCase>& cases, std::size_t query_count) {
    require(bytes.size() == query_count * sizeof(std::array<float, 12>), "result readback",
            "GPU result buffer has an unexpected size");
    constexpr std::array<const char*, 4> kComponentNames{"triangle depth", "bilinear depth",
                                                         "supported depth", "wet support"};
    for (std::size_t index = 0U; index < cases.size(); ++index) {
        std::array<float, 12> actual{};
        std::memcpy(&actual, bytes.data() + index * sizeof(actual), sizeof(actual));
        const BankControlCase& test = cases[index];
        for (std::size_t component = 0U; component < test.expected.size(); ++component) {
            require(std::isfinite(actual[component]), test.label,
                    "sampler returned a non-finite value");
            if (std::abs(actual[component] - test.expected[component]) > kTolerance) {
                char detail[160]{};
                std::snprintf(
                    detail, sizeof(detail), "%s expected %.9g, got %.9g (tolerance %.3g)",
                    kComponentNames[component], static_cast<double>(test.expected[component]),
                    static_cast<double>(actual[component]), static_cast<double>(kTolerance));
                fail(test.label, detail);
            }
        }
        require(actual[0] >= -kTolerance && actual[0] <= 1.1F + kTolerance &&
                    actual[1] >= -kTolerance && actual[1] <= 1.1F + kTolerance &&
                    actual[2] >= -kTolerance && actual[2] <= 1.1F + kTolerance &&
                    actual[3] >= -kTolerance && actual[3] <= 1.0F + kTolerance,
                test.label, "sampler output is outside the bounded synthetic input range");
    }
}

std::array<float, 12> result_row(const std::vector<std::uint8_t>& bytes, std::size_t query_index) {
    std::array<float, 12> result{};
    std::memcpy(&result, bytes.data() + query_index * sizeof(result), sizeof(result));
    return result;
}

const MaskRegion& find_region(const std::vector<MaskRegion>& regions, BsplineControlKind kind) {
    const auto found = std::find_if(regions.begin(), regions.end(),
                                    [&](const MaskRegion& region) { return region.kind == kind; });
    if (found == regions.end()) {
        fail("fixture construction", "a required B-spline mask region is missing");
    }
    return *found;
}

BsplineMetrics validate_bspline_results(const std::vector<std::uint8_t>& bytes,
                                        std::size_t legacy_count, const std::vector<float>& depths,
                                        const std::vector<float>& terrain,
                                        const BsplineFixtureSet& fixtures) {
    BsplineMetrics metrics;
    const std::size_t query_count = legacy_count + fixtures.queries.size();
    require(bytes.size() == query_count * sizeof(std::array<float, 12>), "B-spline result readback",
            "result buffer has an unexpected size");
    std::vector<double> cpu_masked_mesh_depth(fixtures.queries.size(), 0.0);

    const auto maximum_error = [&](double actual, double expected) {
        return std::abs(actual - expected);
    };
    const auto require_float_close = [&](double actual, double expected, std::string_view label,
                                         double& maximum) {
        const double error = maximum_error(actual, expected);
        maximum = std::max(maximum, error);
        if (!std::isfinite(actual) || error > kBsplineToleranceM) {
            char detail[192]{};
            std::snprintf(detail, sizeof(detail), "expected %.12g, got %.12g (error %.4g m)",
                          expected, actual, error);
            fail(label, detail);
        }
    };

    for (std::size_t index = 0U; index < fixtures.queries.size(); ++index) {
        const BsplineControlQuery& test = fixtures.queries[index];
        const std::size_t result_index = legacy_count + index;
        const std::array<float, 12> actual = result_row(bytes, result_index);
        for (const float component : actual) {
            require(std::isfinite(component), test.label,
                    "B-spline sampler returned a non-finite value");
        }

        const double x = static_cast<double>(test.query[0]);
        const double y = static_cast<double>(test.query[1]);
        const double wet_threshold = static_cast<double>(test.query[2]);
        const std::uint32_t plane = static_cast<std::uint32_t>(test.query[3]);
        const CpuBsplineSample depth_sample =
            cpu_bspline_sample(depths, terrain, plane, x, y, CpuField::Depth);
        const CpuBsplineSample bed_sample =
            cpu_bspline_sample(depths, terrain, plane, x, y, CpuField::Terrain);
        const CpuBsplineSample stage_sample =
            cpu_bspline_sample(depths, terrain, plane, x, y, CpuField::Stage);
        const double mesh_depth =
            cpu_mesh_bspline_sample(depths, terrain, plane, x, y, CpuField::Depth);
        const double mesh_bed =
            cpu_mesh_bspline_sample(depths, terrain, plane, x, y, CpuField::Terrain);
        const double support = cpu_wet_support(depths, plane, x, y, wet_threshold);
        const double gated_mesh_depth = support > 0.5 ? mesh_depth : 0.0;
        const double mesh_stage = mesh_bed + mesh_depth;
        cpu_masked_mesh_depth[index] = gated_mesh_depth;

        require_float_close(actual[4], depth_sample.value, test.label,
                            metrics.max_cpu_oracle_error_m);
        require_float_close(actual[5], depth_sample.minimum, test.label,
                            metrics.max_cpu_oracle_error_m);
        require_float_close(actual[6], depth_sample.maximum, test.label,
                            metrics.max_cpu_oracle_error_m);
        require_float_close(actual[7], gated_mesh_depth, test.label,
                            metrics.max_cpu_oracle_error_m);
        require_float_close(actual[8], bed_sample.value, test.label,
                            metrics.max_cpu_oracle_error_m);
        require_float_close(actual[9], stage_sample.value, test.label,
                            metrics.max_cpu_oracle_error_m);
        require_float_close(actual[10], bed_sample.value + depth_sample.value, test.label,
                            metrics.max_cpu_oracle_error_m);
        require_float_close(actual[11], mesh_stage, test.label, metrics.max_cpu_oracle_error_m);
        require_float_close(actual[3], support, test.label, metrics.max_cpu_oracle_error_m);

        const double sampled_depth = static_cast<double>(actual[4]);
        require(sampled_depth >= depth_sample.minimum - kBsplineToleranceM &&
                    sampled_depth <= depth_sample.maximum + kBsplineToleranceM &&
                    sampled_depth >= -kBsplineToleranceM,
                test.label, "positive-weight reconstruction rang outside its local input range");

        switch (test.kind) {
        case BsplineControlKind::Constant:
            ++metrics.constant_queries;
            require(std::abs(sampled_depth - 0.05) <= kBsplineToleranceM, test.label,
                    "constant positive depth was not preserved");
            break;
        case BsplineControlKind::Affine: {
            ++metrics.affine_queries;
            const double expected = 0.2 + 0.001 * x - 0.002 * y;
            const double affine_error = std::abs(static_cast<double>(actual[7]) - expected);
            metrics.max_affine_error_m = std::max(metrics.max_affine_error_m, affine_error);
            require(affine_error <= kBsplineToleranceM, test.label,
                    "interior affine depth was not reproduced by the 4x triangle mesh");
            break;
        }
        case BsplineControlKind::Range:
            ++metrics.range_queries;
            break;
        case BsplineControlKind::Boundary:
            ++metrics.boundary_queries;
            if (x == 0.0 && y == 0.0) {
                require(std::abs(sampled_depth - 1.05) <= kBsplineToleranceM, test.label,
                        "clamped origin extension changed its explicit expected value");
            }
            if (x == static_cast<double>(kGridWidth - 1U) &&
                y == static_cast<double>(kGridHeight - 1U)) {
                require(std::abs(sampled_depth - 7.85) <= kBsplineToleranceM, test.label,
                        "clamped maximum-edge extension changed its explicit expected value");
            }
            break;
        case BsplineControlKind::Stream:
        case BsplineControlKind::Gap:
        case BsplineControlKind::ThinFilmGap:
        case BsplineControlKind::Junction:
        case BsplineControlKind::RecessionEarly:
        case BsplineControlKind::RecessionLate:
            ++metrics.mask_queries;
            break;
        case BsplineControlKind::LakeRest: {
            ++metrics.lake_rest_queries;
            const double combined_error = std::abs(static_cast<double>(actual[9]) - kLakeStageM);
            const double split_error = std::abs(static_cast<double>(actual[10]) - kLakeStageM);
            const double mesh_error = std::abs(static_cast<double>(actual[11]) - kLakeStageM);
            metrics.max_flat_lake_stage_error_m = std::max(
                {metrics.max_flat_lake_stage_error_m, combined_error, split_error, mesh_error});
            require(combined_error <= kLakeStageToleranceM && split_error <= kLakeStageToleranceM &&
                        mesh_error <= kLakeStageToleranceM,
                    test.label,
                    "paired bed/depth reconstruction did not preserve a fully wet level");
            break;
        }
        case BsplineControlKind::PartialDryLake:
            ++metrics.partial_dry_queries;
            ++metrics.mask_queries;
            if (support > 0.5 && support < 1.0 - 1.0e-9 &&
                gated_mesh_depth > kOrdinaryWetThreshold) {
                ++metrics.partial_dry_near_shore_queries;
                metrics.partial_dry_max_stage_deviation_m =
                    std::max(metrics.partial_dry_max_stage_deviation_m,
                             std::abs(static_cast<double>(actual[11]) - 0.36));
            }
            break;
        }
    }

    const auto analyze_region = [&](const MaskRegion& region, bool count_in_delta) {
        std::vector<std::uint8_t> cpu_mask(static_cast<std::size_t>(region.width) * region.height,
                                           0U);
        std::vector<std::uint8_t> gpu_mask(cpu_mask.size(), 0U);
        std::size_t bilinear_count = 0U;
        std::size_t bspline_count = 0U;
        for (std::size_t local = 0U; local < cpu_mask.size(); ++local) {
            const std::size_t bspline_query_index = region.first_query + local;
            const std::array<float, 12> actual =
                result_row(bytes, legacy_count + bspline_query_index);
            const bool cpu_visible = cpu_masked_mesh_depth[bspline_query_index] >
                                     static_cast<double>(kVisibleDepthLevel);
            const bool gpu_visible = actual[7] > kVisibleDepthLevel;
            // Samples within GPU tolerance of the display level are counted in
            // both reports but are not treated as a CPU/GPU topology failure.
            const double margin = std::abs(cpu_masked_mesh_depth[bspline_query_index] -
                                           static_cast<double>(kVisibleDepthLevel));
            if (margin > kBsplineToleranceM) {
                require(cpu_visible == gpu_visible, region.label,
                        "GPU and CPU visible masks disagree away from the depth threshold");
            }
            cpu_mask[local] = static_cast<std::uint8_t>(cpu_visible);
            gpu_mask[local] = static_cast<std::uint8_t>(gpu_visible);
            bspline_count += gpu_visible ? 1U : 0U;
            bilinear_count += actual[2] > kVisibleDepthLevel ? 1U : 0U;
            if (region.label == "thin positive film gap") {
                require(actual[3] >= 1.0F - kTolerance, region.label,
                        "positive thin-film background should make the native wet-support fence "
                        "inactive");
            }
        }
        require(cpu_mask == gpu_mask, region.label,
                "GPU B-spline mask differs from the independent CPU mesh oracle");
        if (count_in_delta) {
            metrics.bilinear_visible_count += bilinear_count;
            metrics.bspline_visible_count += bspline_count;
        }
        return std::tuple{std::move(gpu_mask), bspline_count,
                          connected_components(cpu_mask, region.width, region.height)};
    };

    const MaskRegion& stream_region =
        find_region(fixtures.mask_regions, BsplineControlKind::Stream);
    const auto [stream_mask, stream_count, stream_components] = analyze_region(stream_region, true);
    static_cast<void>(stream_mask);
    metrics.stream_components = stream_components;
    require(stream_count > 0U && stream_components == 1U, stream_region.label,
            "one-cell wet stream must remain one nonempty connected display component");

    const MaskRegion& gap_region = find_region(fixtures.mask_regions, BsplineControlKind::Gap);
    const auto [gap_mask, gap_count, gap_components] = analyze_region(gap_region, true);
    static_cast<void>(gap_mask);
    metrics.gap_components = gap_components;
    require(gap_count > 0U && gap_components == 2U, gap_region.label,
            "zero-depth one-cell gap must keep its two streams disconnected");

    const MaskRegion& thin_film_region =
        find_region(fixtures.mask_regions, BsplineControlKind::ThinFilmGap);
    const auto [thin_film_mask, thin_film_count, thin_film_components] =
        analyze_region(thin_film_region, true);
    static_cast<void>(thin_film_mask);
    require(thin_film_count > 0U, thin_film_region.label,
            "thin-film control should contain visible stream samples");
    metrics.thin_film_gap_components = thin_film_components;
    metrics.positive_film_gap_bridged = thin_film_components < 2U;

    const MaskRegion& junction_region =
        find_region(fixtures.mask_regions, BsplineControlKind::Junction);
    const auto [junction_mask, junction_count, junction_components] =
        analyze_region(junction_region, true);
    static_cast<void>(junction_mask);
    metrics.junction_components = junction_components;
    require(junction_count > 0U && junction_components == 1U, junction_region.label,
            "three-way wet junction must remain one connected display component");

    const MaskRegion& early_region =
        find_region(fixtures.mask_regions, BsplineControlKind::RecessionEarly);
    const MaskRegion& late_region =
        find_region(fixtures.mask_regions, BsplineControlKind::RecessionLate);
    const auto [early_mask, early_count, unused_early_components] =
        analyze_region(early_region, true);
    const auto [late_mask, late_count, unused_late_components] = analyze_region(late_region, true);
    static_cast<void>(unused_early_components);
    static_cast<void>(unused_late_components);
    require(early_region.width == late_region.width && early_region.height == late_region.height,
            "recession mask", "early and late masks must use identical dense sample grids");
    metrics.recession_early_visible_count = early_count;
    metrics.recession_late_visible_count = late_count;
    for (std::size_t index = 0U; index < early_mask.size(); ++index) {
        metrics.recession_subset_violations +=
            late_mask[index] != 0U && early_mask[index] == 0U ? 1U : 0U;
    }
    require(metrics.recession_subset_violations == 0U && late_count < early_count, "recession mask",
            "later monotone recession must be a strict subset of earlier wetness");

    const MaskRegion& partial_dry_region =
        find_region(fixtures.mask_regions, BsplineControlKind::PartialDryLake);
    static_cast<void>(analyze_region(partial_dry_region, true));
    require(metrics.partial_dry_near_shore_queries > 0U, "partial-dry lake counterexample",
            "no near-shore sample passed the native support fence for stage-error reporting");
    metrics.visible_level_count_delta = static_cast<std::int64_t>(metrics.bspline_visible_count) -
                                        static_cast<std::int64_t>(metrics.bilinear_visible_count);
    return metrics;
}

} // namespace

void validate_fluid_25d_bank_gpu_controls(cubey::vulkan::Device& device,
                                          cubey::ProjectGpuServices& gpu) {
    std::vector<float> depth_values;
    const std::vector<BankControlCase> cases = make_cases(depth_values);
    std::vector<float> terrain_values;
    const BsplineFixtureSet bspline_fixtures = make_bspline_fixtures(depth_values, terrain_values);
    std::vector<std::array<float, 4>> query_values;
    query_values.reserve(cases.size() + bspline_fixtures.queries.size());
    for (const BankControlCase& test : cases) {
        query_values.push_back(test.query);
    }
    for (const BsplineControlQuery& test : bspline_fixtures.queries) {
        query_values.push_back(test.query);
    }
    std::vector<std::array<float, 12>> empty_results(query_values.size(), std::array<float, 12>{});

    auto depth_buffer = gpu.upload_device_buffer(
        depth_values.data(), depth_values.size() * sizeof(depth_values.front()),
        kScratchBufferUsage, "fluid_25d bank sampler scratch depths");
    auto terrain_buffer = gpu.upload_device_buffer(
        terrain_values.data(), terrain_values.size() * sizeof(terrain_values.front()),
        kScratchBufferUsage, "fluid_25d bank sampler scratch terrain");
    auto query_buffer = gpu.upload_device_buffer(
        query_values.data(), query_values.size() * sizeof(query_values.front()),
        kScratchBufferUsage, "fluid_25d bank sampler scratch queries");
    auto result_buffer = gpu.upload_device_buffer(
        empty_results.data(), empty_results.size() * sizeof(empty_results.front()),
        kScratchBufferUsage, "fluid_25d bank sampler scratch results");

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> bindings{{
        {.binding = 0U,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1U,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2U,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3U,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    const cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0U, depth_buffer.handle(), depth_buffer.size())
        .storage_buffer(descriptors.set(), 1U, query_buffer.handle(), query_buffer.size())
        .storage_buffer(descriptors.set(), 2U, result_buffer.handle(), result_buffer.size())
        .storage_buffer(descriptors.set(), 3U, terrain_buffer.handle(), terrain_buffer.size());
    writes.update(device);

    std::optional<cubey::render::ComputePipelineResource> pipeline;
    const std::array<VkPushConstantRange, 1> push_constants{{
        {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .offset = 0U,
         .size = sizeof(BankControlPushConstants)},
    }};
    cubey::render::emplace_single_set_compute_pipeline_resource(
        pipeline, device,
        cubey::render::compute_shader_file(shader_path("fluid_25d_bank_controls.comp.spv")),
        descriptors.layout(), push_constants);

    const auto dispatch_controls = [&] {
        static_cast<void>(gpu.submit_and_wait(
            {.label = "fluid_25d bank sampler GPU controls",
             .work = [&](cubey::vulkan::GpuOwnerContext& context) {
                 cubey::vulkan::ImmediateCommands commands(context);
                 const VkCommandBuffer command_buffer = commands.command_buffer();
                 const cubey::vulkan::CommandRecorder recorder(command_buffer);
                 const BankControlPushConstants params{
                     .grid_query = {kGridWidth, kGridHeight,
                                    static_cast<std::uint32_t>(query_values.size()),
                                    kMeshSubdivision},
                 };
                 const auto groups = cubey::render::ceil_dispatch_groups(
                     static_cast<std::uint32_t>(query_values.size()), 1U, kWorkgroupSize);
                 cubey::render::record_compute_pipeline_dispatch(
                     recorder,
                     cubey::render::compute_pipeline_dispatch_info(*pipeline, descriptors.set(),
                                                                   groups),
                     VK_SHADER_STAGE_COMPUTE_BIT, params);
                 cubey::vulkan::record_memory_barrier(
                     command_buffer, {.src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      .src_access = VK_ACCESS_SHADER_WRITE_BIT,
                                      .dst_access = VK_ACCESS_TRANSFER_READ_BIT});
                 commands.submit_and_wait();
             }}));
    };

    const std::vector<std::uint8_t> expected_depth_bytes = bytes_of(depth_values);
    const std::vector<std::uint8_t> expected_terrain_bytes = bytes_of(terrain_values);
    const std::vector<std::uint8_t> expected_query_bytes = bytes_of(query_values);
    require(read_buffer(gpu, depth_buffer, "bank sampler input depth preflight") ==
                expected_depth_bytes,
            "scratch input immutability", "uploaded depth bytes do not match the fixture");
    require(read_buffer(gpu, terrain_buffer, "bank sampler input terrain preflight") ==
                expected_terrain_bytes,
            "scratch input immutability", "uploaded terrain bytes do not match the fixture");
    require(read_buffer(gpu, query_buffer, "bank sampler input query preflight") ==
                expected_query_bytes,
            "scratch input immutability", "uploaded query bytes do not match the fixture");

    dispatch_controls();
    const std::vector<std::uint8_t> first_results =
        read_buffer(gpu, result_buffer, "bank sampler result readback");
    validate_result(first_results, cases, query_values.size());
    const BsplineMetrics metrics = validate_bspline_results(
        first_results, cases.size(), depth_values, terrain_values, bspline_fixtures);
    require(read_buffer(gpu, depth_buffer, "bank sampler depth postflight") == expected_depth_bytes,
            "scratch input immutability", "sampler dispatch changed depth fixture bytes");
    require(read_buffer(gpu, terrain_buffer, "bank sampler terrain postflight") ==
                expected_terrain_bytes,
            "scratch input immutability", "sampler dispatch changed terrain fixture bytes");
    require(read_buffer(gpu, query_buffer, "bank sampler query postflight") == expected_query_bytes,
            "scratch input immutability", "sampler dispatch changed query fixture bytes");

    dispatch_controls();
    const std::vector<std::uint8_t> repeated_results =
        read_buffer(gpu, result_buffer, "bank sampler repeated result readback");
    require(first_results == repeated_results, "deterministic repeat",
            "same GPU sampler inputs produced different result bytes");
    require(read_buffer(gpu, depth_buffer, "bank sampler repeated depth postflight") ==
                    expected_depth_bytes &&
                read_buffer(gpu, terrain_buffer, "bank sampler repeated terrain postflight") ==
                    expected_terrain_bytes &&
                read_buffer(gpu, query_buffer, "bank sampler repeated query postflight") ==
                    expected_query_bytes,
            "scratch input immutability", "repeated sampler dispatch changed input bytes");

    std::printf("fluid_25d_bank_controls: PASS\n");
    std::printf("fluid_25d_bank_controls_fixtures: PASS %zu analytic samples: "
                "flat/slope/saddle, boundaries, dry/single/diagonal support, one-cell "
                "row/column gaps, junction, receding; scratch inputs unchanged; repeat exact\n",
                cases.size());
    std::printf(
        "fluid_25d_bank_controls_bspline: PASS queries=%zu constant=%zu affine=%zu range=%zu "
        "boundary=%zu topology=%zu fully-wet-lake=%zu partial-dry=%zu\n",
        bspline_fixtures.queries.size(), metrics.constant_queries, metrics.affine_queries,
        metrics.range_queries, metrics.boundary_queries, metrics.mask_queries,
        metrics.lake_rest_queries, metrics.partial_dry_queries);
    std::printf(
        "fluid_25d_bank_controls_report: {\"schema\":\"cubey.fluid25d.bank_controls.v1\","
        "\"bspline\":{\"pass\":true,\"compute_only\":true,"
        "\"checked\":{\"queries\":%zu,\"constant\":%zu,\"affine\":%zu,"
        "\"range\":%zu,\"boundary\":%zu,\"mask\":%zu,\"fully_wet_lake\":%zu},"
        "\"max_cpu_oracle_error_m\":%.9g,\"max_affine_error_m\":%.9g,"
        "\"max_fully_wet_lake_stage_error_m\":%.9g,"
        "\"visible_level_counts\":{\"bilinear\":%zu,\"bspline\":%zu,"
        "\"delta\":%lld,\"threshold_m\":%.6g},"
        "\"components\":{\"one_cell_stream\":%zu,\"zero_depth_gap\":%zu,"
        "\"junction\":%zu,\"recession_early_visible_samples\":%zu,\"recession_late_visible_samples\":%zu,"
        "\"recession_subset_violations\":%zu},"
        "\"positive_film_gap_counterexample\":{\"components\":%zu,"
        "\"bridge_observed\":%s,\"status\":\"%s\","
        "\"native_fence_inactive\":true,"
        "\"acceptance_gate\":false},"
        "\"partial_dry_lake_counterexample\":{\"near_shore_samples\":%zu,"
        "\"max_stage_deviation_m\":%.9g,\"acceptance_gate\":false},"
        "\"rasterized_geometry_visibility_test\":false}}\n",
        bspline_fixtures.queries.size(), metrics.constant_queries, metrics.affine_queries,
        metrics.range_queries, metrics.boundary_queries, metrics.mask_queries,
        metrics.lake_rest_queries, metrics.max_cpu_oracle_error_m, metrics.max_affine_error_m,
        metrics.max_flat_lake_stage_error_m, metrics.bilinear_visible_count,
        metrics.bspline_visible_count, static_cast<long long>(metrics.visible_level_count_delta),
        static_cast<double>(kVisibleDepthLevel), metrics.stream_components, metrics.gap_components,
        metrics.junction_components, metrics.recession_early_visible_count,
        metrics.recession_late_visible_count, metrics.recession_subset_violations,
        metrics.thin_film_gap_components, metrics.positive_film_gap_bridged ? "true" : "false",
        metrics.positive_film_gap_bridged ? "known limitation observed"
                                          : "no bridge in this sampled fixture",
        metrics.partial_dry_near_shore_queries, metrics.partial_dry_max_stage_deviation_m);
}

} // namespace cubey::projects::fluid::fluid_25d
