#include "terrain_export.h"

#include "terrain_visualization.h"

#include <cubey/core/jobs.h>
#include <cubey/procedural/operators.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
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

[[nodiscard]] nlohmann::json distribution_json(
    const cubey::procedural::ScalarFieldDistribution& distribution) {
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
    const std::size_t count = static_cast<std::size_t>(std::count_if(
        field.values().begin(), field.values().end(),
        [threshold](float value) { return value > threshold; }));
    return field.sample_count() > 0U
               ? static_cast<double>(count) / static_cast<double>(field.sample_count())
               : 0.0;
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

    constexpr std::size_t kOrientationBinCount = 16U;
    constexpr float kPi = 3.14159265358979323846F;
    std::array<double, kOrientationBinCount> orientation_bins{};
    double orientation_weight = 0.0;
    for (std::uint32_t y = 1U; y + 1U < source.desc().height; ++y) {
        for (std::uint32_t x = 1U; x + 1U < source.desc().width; ++x) {
            const float dx = (source.at(x + 1U, y) - source.at(x - 1U, y)) /
                             (2.0F * source.desc().cell_size);
            const float dy = (source.at(x, y + 1U) - source.at(x, y - 1U)) /
                             (2.0F * source.desc().cell_size);
            const float magnitude = std::sqrt((dx * dx) + (dy * dy));
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
            const std::size_t bin = std::min(
                static_cast<std::size_t>((angle / kPi) * kOrientationBinCount),
                kOrientationBinCount - 1U);
            const double weight = std::min(static_cast<double>(magnitude), 2.0);
            orientation_bins[bin] += weight;
            orientation_weight += weight;
        }
    }
    if (orientation_weight > 0.0) {
        for (double& value : orientation_bins) {
            value /= orientation_weight;
        }
    }
    const double max_orientation =
        *std::max_element(orientation_bins.begin(), orientation_bins.end());

    return {
        {"routing_fill_coverage_gt_1m", coverage_above(fill, 1.0F)},
        {"routing_fill_coverage_gt_10m", coverage_above(fill, 10.0F)},
        {"routing_fill_coverage_gt_50m", coverage_above(fill, 50.0F)},
        {"routing_fill_volume_m3", fill_volume},
        {"source_gradient_orientation_bins", orientation_bins},
        {"source_gradient_anisotropy", max_orientation * kOrientationBinCount},
    };
}

[[nodiscard]] nlohmann::json manifest_json(const TerrainPatchProduct& product) {
    const cubey::procedural::PatchDomain2D& domain = product.request.domain;
    nlohmann::json fields = nlohmann::json::object();
    for (const TerrainFieldSummary& field : product.summary.fields) {
        const cubey::procedural::ScalarField2D& values = product.fields.field(field.name);
        nlohmann::json entry =
            distribution_json(cubey::procedural::summarize_scalar_field_distribution(values));
        entry["file"] = field.name + ".png";
        entry["display"] = display_json(terrain_field_display_spec(field.name, values));
        if (field.name == kTerrainFieldDischargeProxy) {
            entry["semantic_scope"] = "patch-relative-legacy";
        }
        fields[field.name] = std::move(entry);
    }
    return {
        {"schema", "cubey.terrain.patch.v2"},
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
        {"process_halo_samples", domain.border_samples},
        {"hydrology_boundary_policy", "open-boundary-with-halo"},
        {"field_count", product.fields.field_count()},
        {"content_hash", product.summary.content_hash},
        {"content_hash_hex", hex_u64(product.summary.content_hash)},
        {"review_metrics", review_metrics_json(product)},
        {"fields", std::move(fields)},
    };
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
                            const std::filesystem::path& output_dir) {
    if (output_dir.empty()) {
        throw std::runtime_error("terrain manifest output directory must be non-empty");
    }
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path path = output_dir / "manifest.json";
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to open terrain manifest: " + path.string());
    }
    output << manifest_json(product).dump(2) << '\n';
}

void write_terrain_field_exports(const TerrainPatchProduct& product,
                                 const std::filesystem::path& output_dir) {
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
    }
    backlog.finish_all();
    write_terrain_manifest(product, output_dir);
}

} // namespace cubey::projects::terrain
