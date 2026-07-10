#include "terrain_export.h"

#include <cubey/core/jobs.h>
#include <cubey/procedural/operators.h>

#include <nlohmann/json.hpp>

#include <algorithm>
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

[[nodiscard]] float normalized_value(float value, float low, float high) {
    if (!(high > low)) {
        return 0.5F;
    }
    return cubey::procedural::saturate((value - low) / (high - low));
}

[[nodiscard]] bool is_unit_field(std::string_view name) {
    return name == kTerrainFieldMountainSupport || name.ends_with("_mask") ||
           name == "discharge_proxy";
}

[[nodiscard]] bool is_signed_field(std::string_view name) {
    return name == kTerrainFieldCurvature || name == "flow_direction_x" ||
           name == "flow_direction_z";
}

[[nodiscard]] Rgb signed_color(float value, float extent) {
    const float normalized = extent > 0.0F ? std::clamp(value / extent, -1.0F, 1.0F) : 0.0F;
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

[[nodiscard]] nlohmann::json manifest_json(const TerrainPatchProduct& product) {
    const cubey::procedural::PatchDomain2D& domain = product.request.domain;
    nlohmann::json fields = nlohmann::json::object();
    for (const TerrainFieldSummary& field : product.summary.fields) {
        nlohmann::json entry = stats_json(field.stats);
        entry["file"] = field.name + ".png";
        fields[field.name] = std::move(entry);
    }
    return {
        {"schema", "cubey.terrain.patch.v1"},
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
        {"fields", std::move(fields)},
    };
}

} // namespace

std::vector<std::uint8_t>
render_terrain_scalar_field_rgba8(const cubey::procedural::ScalarField2D& field,
                                  std::string_view field_name) {
    const cubey::procedural::ScalarFieldStats stats = field.summarize();
    const float signed_extent = std::max(std::abs(stats.min), std::abs(stats.max));
    std::vector<std::uint8_t> pixels(field.sample_count() * 4U, 255U);
    for (std::uint32_t y = 0; y < field.desc().height; ++y) {
        const std::uint32_t source_y = field.desc().height - 1U - y;
        for (std::uint32_t x = 0; x < field.desc().width; ++x) {
            const float value = field.at(x, source_y);
            Rgb color{};
            if (is_signed_field(field_name)) {
                color = signed_color(value, signed_extent);
            } else {
                const float t = is_unit_field(field_name)
                                    ? cubey::procedural::saturate(value)
                                    : normalized_value(value, stats.min, stats.max);
                color = {.r = t, .g = t, .b = t};
            }
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
