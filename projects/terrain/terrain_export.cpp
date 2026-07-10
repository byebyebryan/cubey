#include "terrain_export.h"

#include "terrain_visualization.h"

#include <cubey/core/jobs.h>
#include <cubey/procedural/operators.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace cubey::projects::terrain {
namespace {

inline constexpr std::size_t kEncodeWorkerCount = 2U;
inline constexpr std::size_t kEncodeBacklog = 4U;

struct Rgb {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

struct GradientOrientationMetrics {
    std::array<double, 16U> bins{};
    double anisotropy = 0.0;
};

[[nodiscard]] std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(std::round(cubey::procedural::saturate(value) * 255.0F));
}

[[nodiscard]] Rgb diverging_color(float unit_value) {
    const float normalized = (unit_value * 2.0F) - 1.0F;
    if (normalized < 0.0F) {
        const float t = -normalized;
        return {
            .r = cubey::procedural::lerp(0.92F, 0.12F, t),
            .g = cubey::procedural::lerp(0.92F, 0.38F, t),
            .b = cubey::procedural::lerp(0.92F, 0.84F, t),
        };
    }
    return {
        .r = cubey::procedural::lerp(0.92F, 0.88F, normalized),
        .g = cubey::procedural::lerp(0.92F, 0.24F, normalized),
        .b = cubey::procedural::lerp(0.92F, 0.12F, normalized),
    };
}

[[nodiscard]] std::string hex_u64(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] nlohmann::json stats_json(const cubey::procedural::ScalarFieldStats& stats) {
    return {
        {"sample_count", stats.sample_count},
        {"min", stats.min},
        {"max", stats.max},
        {"span", stats.span},
        {"mean", stats.mean},
    };
}

[[nodiscard]] nlohmann::json
distribution_json(const cubey::procedural::ScalarFieldDistribution& distribution) {
    nlohmann::json result = stats_json(distribution.stats);
    result["p01"] = distribution.p01;
    result["p05"] = distribution.p05;
    result["p50"] = distribution.p50;
    result["p95"] = distribution.p95;
    result["p99"] = distribution.p99;
    return result;
}

[[nodiscard]] nlohmann::json display_json(const TerrainFieldDisplaySpec& display) {
    return {
        {"low", display.low},
        {"high", display.high},
        {"scale", terrain_field_display_scale_name(display.scale)},
        {"range_scope", display.patch_relative ? "patch" : "fixed"},
    };
}

[[nodiscard]] double coverage_above(const cubey::procedural::ScalarField2D& field,
                                    float threshold) {
    const std::size_t count = static_cast<std::size_t>(
        std::count_if(field.values().begin(), field.values().end(),
                      [threshold](float value) { return value > threshold; }));
    return field.sample_count() > 0U
               ? static_cast<double>(count) / static_cast<double>(field.sample_count())
               : 0.0;
}

[[nodiscard]] GradientOrientationMetrics
gradient_orientation_metrics(const cubey::procedural::ScalarField2D& field) {
    constexpr std::size_t kOrientationBinCount = 16U;
    constexpr float kPi = 3.14159265358979323846F;
    GradientOrientationMetrics result;
    double orientation_weight = 0.0;
    for (std::uint32_t y = 1U; y + 1U < field.desc().height; ++y) {
        for (std::uint32_t x = 1U; x + 1U < field.desc().width; ++x) {
            const float dx =
                (field.at(x + 1U, y) - field.at(x - 1U, y)) / (2.0F * field.desc().cell_size);
            const float dy =
                (field.at(x, y + 1U) - field.at(x, y - 1U)) / (2.0F * field.desc().cell_size);
            const float magnitude = std::hypot(dx, dy);
            if (magnitude < 0.01F) {
                continue;
            }
            float angle = std::atan2(dy, dx);
            if (angle < 0.0F) {
                angle += kPi;
            }
            if (angle >= kPi) {
                angle -= kPi;
            }
            const std::size_t bin =
                std::min(static_cast<std::size_t>((angle / kPi) * kOrientationBinCount),
                         kOrientationBinCount - 1U);
            const double weight = std::min(static_cast<double>(magnitude), 2.0);
            result.bins[bin] += weight;
            orientation_weight += weight;
        }
    }
    if (orientation_weight > 0.0) {
        for (double& value : result.bins) {
            value /= orientation_weight;
        }
    }
    result.anisotropy =
        *std::max_element(result.bins.begin(), result.bins.end()) * kOrientationBinCount;
    return result;
}

[[nodiscard]] nlohmann::json process_graph_metrics_json(const TerrainPatchProduct& product) {
    if (!product.fields.has_field(kTerrainFieldProcessFlowDirectionX) ||
        !product.fields.has_field(kTerrainFieldProcessFlowDirectionZ)) {
        return nlohmann::json::object();
    }
    const cubey::procedural::ScalarField2D& height = product.fields.field(kTerrainFieldHeightM);
    const cubey::procedural::ScalarField2D& flow_x =
        product.fields.field(kTerrainFieldProcessFlowDirectionX);
    const cubey::procedural::ScalarField2D& flow_z =
        product.fields.field(kTerrainFieldProcessFlowDirectionZ);
    std::size_t unresolved = 0U;
    std::size_t severe_discontinuities = 0U;
    std::size_t interior_count = 0U;
    for (std::uint32_t y = 1U; y + 1U < height.desc().height; ++y) {
        for (std::uint32_t x = 1U; x + 1U < height.desc().width; ++x) {
            ++interior_count;
            const int dx = static_cast<int>(std::lround(flow_x.at(x, y)));
            const int dz = static_cast<int>(std::lround(flow_z.at(x, y)));
            if (dx == 0 && dz == 0) {
                ++unresolved;
                continue;
            }
            const auto receiver_x = static_cast<std::uint32_t>(static_cast<int>(x) + dx);
            const auto receiver_y = static_cast<std::uint32_t>(static_cast<int>(y) + dz);
            const float receiver_drop =
                std::max(height.at(x, y) - height.at(receiver_x, receiver_y), 0.0F);
            bool severe = false;
            constexpr std::array<int, 4U> offsets_x{1, 0, -1, 0};
            constexpr std::array<int, 4U> offsets_y{0, 1, 0, -1};
            for (std::size_t direction = 0U; direction < offsets_x.size(); ++direction) {
                const int neighbor_dx = offsets_x[direction];
                const int neighbor_dz = offsets_y[direction];
                if (neighbor_dx == dx && neighbor_dz == dz) {
                    continue;
                }
                const auto nx = static_cast<std::uint32_t>(static_cast<int>(x) + neighbor_dx);
                const auto ny = static_cast<std::uint32_t>(static_cast<int>(y) + neighbor_dz);
                const int neighbor_flow_x = static_cast<int>(std::lround(flow_x.at(nx, ny)));
                const int neighbor_flow_z = static_cast<int>(std::lround(flow_z.at(nx, ny)));
                if (static_cast<int>(nx) + neighbor_flow_x == static_cast<int>(x) &&
                    static_cast<int>(ny) + neighbor_flow_z == static_cast<int>(y)) {
                    continue;
                }
                if (height.at(x, y) - height.at(nx, ny) - receiver_drop > 100.0F) {
                    severe = true;
                    break;
                }
            }
            severe_discontinuities += static_cast<std::size_t>(severe);
        }
    }
    const double denominator = static_cast<double>(std::max(interior_count, std::size_t{1U}));
    nlohmann::json result{
        {"process_unresolved_sink_count", unresolved},
        {"process_unresolved_sink_coverage", static_cast<double>(unresolved) / denominator},
        {"process_basin_discontinuity_coverage_gt_100m",
         static_cast<double>(severe_discontinuities) / denominator},
    };
    if (product.fields.has_field(kTerrainFieldThermalActiveMask)) {
        result["thermal_active_coverage"] =
            coverage_above(product.fields.field(kTerrainFieldThermalActiveMask), 0.5F);
    }
    return result;
}

[[nodiscard]] nlohmann::json review_metrics_json(const TerrainPatchProduct& product) {
    const cubey::procedural::ScalarField2D& fill =
        product.fields.field(kTerrainFieldRoutingFillDeltaM);
    const cubey::procedural::ScalarField2D& source =
        product.fields.field(kTerrainFieldSourceHeightM);
    const double cell_area = static_cast<double>(fill.desc().cell_size) * fill.desc().cell_size;
    double fill_volume = 0.0;
    for (const float value : fill.values()) {
        fill_volume += static_cast<double>(std::max(value, 0.0F)) * cell_area;
    }

    const GradientOrientationMetrics source_orientation = gradient_orientation_metrics(source);
    const GradientOrientationMetrics final_orientation =
        gradient_orientation_metrics(product.fields.field(kTerrainFieldHeightM));
    nlohmann::json result{
        {"routing_fill_coverage_gt_1m", coverage_above(fill, 1.0F)},
        {"routing_fill_coverage_gt_10m", coverage_above(fill, 10.0F)},
        {"routing_fill_coverage_gt_50m", coverage_above(fill, 50.0F)},
        {"routing_fill_volume_m3", fill_volume},
        {"source_gradient_orientation_bins", source_orientation.bins},
        {"source_gradient_anisotropy", source_orientation.anisotropy},
        {"final_gradient_orientation_bins", final_orientation.bins},
        {"final_gradient_anisotropy", final_orientation.anisotropy},
    };
    result.update(process_graph_metrics_json(product));
    return result;
}

[[nodiscard]] nlohmann::json manifest_json(const TerrainPatchProduct& product,
                                           TerrainExportOptions options) {
    const cubey::procedural::PatchDomain2D& domain = product.request.domain;
    nlohmann::json fields = nlohmann::json::object();
    for (const TerrainFieldSummary& field : product.summary.fields) {
        const cubey::procedural::ScalarField2D& values = product.fields.field(field.name);
        nlohmann::json entry =
            distribution_json(cubey::procedural::summarize_scalar_field_distribution(values));
        entry["file"] = field.name + ".png";
        entry["display"] = display_json(terrain_field_display_spec(field.name, values));
        if (options.write_raw_float32) {
            entry["raw_file"] = field.name + ".f32";
            entry["raw_encoding"] = "float32-le-row-major";
            entry["raw_byte_count"] = values.sample_count() * sizeof(float);
        }
        if (field.name == kTerrainFieldDischargeProxy) {
            entry["semantic_scope"] = "patch-relative-legacy";
        }
        fields[field.name] = std::move(entry);
    }
    const bool landscape = product.request.recipe_id == kTerrainRecipeUplandLandscapeEvolutionV1;
    nlohmann::json result{
        {"schema", "cubey.terrain.patch.v3"},
        {"recipe_id", product.request.recipe_id},
        {"generator_revision", product.request.generator_revision},
        {"seed", domain.seed},
        {"seed_hex", hex_u64(domain.seed)},
        {"patch_address",
         {
             {"x", domain.address.x},
             {"y", domain.address.y},
             {"level", domain.address.level},
         }},
        {"interior_grid",
         {
             {"width", domain.interior_grid.width},
             {"height", domain.interior_grid.height},
             {"cell_size_m", domain.interior_grid.cell_size},
             {"origin_x_m", domain.interior_grid.origin_x},
             {"origin_z_m", domain.interior_grid.origin_y},
         }},
        {"process_halo_samples",
         landscape ? kTerrainLandscapeProcessHaloSamples : domain.border_samples},
        {"generation_scope", landscape ? "regional-not-seam-safe" : "bounded-patch"},
        {"hydrology_boundary_policy", "open-boundary-with-halo"},
        {"field_count", product.fields.field_count()},
        {"content_hash", product.summary.content_hash},
        {"content_hash_hex", hex_u64(product.summary.content_hash)},
        {"review_metrics", review_metrics_json(product)},
        {"fields", std::move(fields)},
    };
    if (landscape) {
        result["process_model"] = {
            {"name", "transient-analytical-landscape-evolution"},
            {"age_years", 1.6e6},
            {"stream_power_coefficient", 2.0e-5},
            {"stream_power_area_exponent", 0.4},
            {"stream_power_slope_exponent", 1.0},
            {"maximum_uplift_m_per_year", 1.0e-3},
            {"hillslope_coefficient", 0.1},
            {"hack_constant", 1.5},
            {"hack_exponent", 0.6},
            {"thermal_coefficient", 1.0e-3},
            {"critical_slope_degrees", 30.0},
            {"multigrid_levels", 4},
            {"iterations_per_level", 6},
            {"relaxation", 0.25},
            {"altitude_correction_iterations", 50},
            {"boundary_policy", "open-outer-guard"},
        };
    }
    return result;
}

void write_binary_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open terrain raw field: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed to write terrain raw field: " + path.string());
    }
}

} // namespace

std::vector<std::uint8_t>
render_terrain_scalar_field_rgba8(const cubey::procedural::ScalarField2D& field,
                                  std::string_view field_name) {
    const TerrainFieldDisplaySpec display = terrain_field_display_spec(field_name, field);
    std::vector<std::uint8_t> pixels(field.sample_count() * 4U, 255U);
    for (std::uint32_t y = 0; y < field.desc().height; ++y) {
        const std::uint32_t source_y = field.desc().height - 1U - y;
        for (std::uint32_t x = 0; x < field.desc().width; ++x) {
            const float value = field.at(x, source_y);
            const float t = terrain_field_display_value(value, display);
            const Rgb color = display.palette == TerrainFieldDisplayPalette::Diverging
                                  ? diverging_color(t)
                                  : Rgb{.r = t, .g = t, .b = t};
            const std::size_t index = (static_cast<std::size_t>(y) * field.desc().width + x) * 4U;
            pixels[index + 0U] = byte(color.r);
            pixels[index + 1U] = byte(color.g);
            pixels[index + 2U] = byte(color.b);
        }
    }
    return pixels;
}

std::vector<std::uint8_t>
encode_terrain_scalar_field_f32_le(const cubey::procedural::ScalarField2D& field) {
    std::vector<std::uint8_t> bytes(field.sample_count() * sizeof(float));
    for (std::size_t index = 0U; index < field.sample_count(); ++index) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(field.values()[index]);
        const std::size_t offset = index * sizeof(float);
        bytes[offset + 0U] = static_cast<std::uint8_t>(bits & 0xffU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((bits >> 8U) & 0xffU);
        bytes[offset + 2U] = static_cast<std::uint8_t>((bits >> 16U) & 0xffU);
        bytes[offset + 3U] = static_cast<std::uint8_t>((bits >> 24U) & 0xffU);
    }
    return bytes;
}

cubey::CaptureTicket enqueue_terrain_scalar_field_png(cubey::CaptureQueue& captures,
                                                      const cubey::procedural::ScalarField2D& field,
                                                      std::string_view field_name,
                                                      const std::filesystem::path& output_path) {
    if (output_path.empty()) {
        throw std::runtime_error("terrain scalar export output path must be non-empty");
    }
    return captures.enqueue_png({
        .output_path = output_path,
        .width = field.desc().width,
        .height = field.desc().height,
        .rgba8 = render_terrain_scalar_field_rgba8(field, field_name),
    });
}

void write_terrain_manifest(const TerrainPatchProduct& product,
                            const std::filesystem::path& output_dir, TerrainExportOptions options) {
    if (output_dir.empty()) {
        throw std::runtime_error("terrain manifest output directory must be non-empty");
    }
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path path = output_dir / "manifest.json";
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to open terrain manifest: " + path.string());
    }
    output << manifest_json(product, options).dump(2) << '\n';
}

void write_terrain_field_exports(const TerrainPatchProduct& product,
                                 const std::filesystem::path& output_dir,
                                 TerrainExportOptions options) {
    if (output_dir.empty()) {
        throw std::runtime_error("terrain field output directory must be non-empty");
    }
    std::filesystem::create_directories(output_dir);
    cubey::jobs::JobSystem jobs(kEncodeWorkerCount);
    cubey::CaptureQueue captures(jobs);
    cubey::CaptureBacklog backlog(kEncodeBacklog);
    for (const std::string& name : product.fields.field_names()) {
        backlog.enqueue(enqueue_terrain_scalar_field_png(captures, product.fields.field(name), name,
                                                         output_dir / (name + ".png")));
        if (options.write_raw_float32) {
            write_binary_file(output_dir / (name + ".f32"),
                              encode_terrain_scalar_field_f32_le(product.fields.field(name)));
        }
    }
    backlog.finish_all();
    write_terrain_manifest(product, output_dir, options);
}

} // namespace cubey::projects::terrain
