#include "terrain_raster_height_source.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain {
namespace {

constexpr std::string_view kSchema = "cubey.terrain.heightfield.v1";
constexpr std::uint32_t kMaximumDimension = 16'384U;

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const std::filesystem::path& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path manifest_path(const std::filesystem::path& field_path) {
    return std::filesystem::is_directory(field_path) ? field_path / "heightfield.json" : field_path;
}

[[nodiscard]] nlohmann::json read_manifest(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("failed to open terrain raster manifest: " + path.string());
    }
    try {
        return nlohmann::json::parse(stream);
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("invalid terrain raster manifest " + path.string() + ": " +
                                 error.what());
    }
}

[[nodiscard]] bool valid_sha256(std::string_view value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char digit) {
               return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
           });
}

[[nodiscard]] std::vector<float> read_elevation(const std::filesystem::path& path,
                                                std::size_t count,
                                                std::uint64_t declared_byte_count) {
    if constexpr (std::endian::native != std::endian::little) {
        throw std::runtime_error("terrain heightfield requires a little-endian host");
    }
    const std::uint64_t expected_bytes = static_cast<std::uint64_t>(count) * sizeof(float);
    std::error_code error;
    const std::uint64_t actual_bytes = std::filesystem::file_size(path, error);
    if (error || declared_byte_count != expected_bytes || actual_bytes != expected_bytes) {
        throw std::runtime_error("terrain raster elevation byte count does not match manifest");
    }
    std::vector<float> values(count);
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char*>(values.data()),
                                static_cast<std::streamsize>(expected_bytes))) {
        throw std::runtime_error("failed to read terrain raster elevation: " + path.string());
    }
    if (!std::all_of(values.begin(), values.end(),
                     [](float value) { return std::isfinite(value); })) {
        throw std::runtime_error("terrain raster elevation contains non-finite values");
    }
    return values;
}

} // namespace

TerrainRasterHeightSource::TerrainRasterHeightSource(const std::filesystem::path& field_path) {
    const std::filesystem::path manifest = manifest_path(field_path);
    provenance_.manifest_path = std::filesystem::absolute(manifest).lexically_normal();
    const nlohmann::json document = read_manifest(manifest);
    try {
        if (document.at("schema").get<std::string_view>() != kSchema) {
            throw std::runtime_error("unsupported terrain raster manifest schema");
        }
        const nlohmann::json& source = document.at("source");
        source_id_ = source.at("id").get<std::string>();
        provenance_.generator = source.value("generator", source_id_);
        provenance_.code_revision = source.value("code_revision", std::string{});
        provenance_.model_id = source.value("model_id", std::string{});
        provenance_.model_revision = source.value("model_revision", std::string{});
        if (const auto provenance = document.find("provenance"); provenance != document.end()) {
            provenance_.purpose = provenance->value("purpose", std::string{});
            provenance_.selection = provenance->value("selection", std::string{});
        }
        seed_ = document.at("seed").get<std::uint64_t>();
        const nlohmann::json& grid = document.at("grid");
        const std::uint32_t width = grid.at("width").get<std::uint32_t>();
        const std::uint32_t height = grid.at("height").get<std::uint32_t>();
        const float spacing = grid.at("sample_spacing_m").get<float>();
        const float origin_x = grid.at("sample_origin_x_m").get<float>();
        const float origin_z = grid.at("sample_origin_z_m").get<float>();
        if (source_id_.empty() || width < 2U || height < 2U || width > kMaximumDimension ||
            height > kMaximumDimension || !std::isfinite(spacing) || spacing <= 0.0F ||
            !std::isfinite(origin_x) || !std::isfinite(origin_z)) {
            throw std::runtime_error("invalid terrain raster grid metadata");
        }
        const nlohmann::json& height_contract = document.at("height");
        height_offset_m_ = height_contract.at("offset_m").get<float>();
        height_scale_ = height_contract.at("scale").get<float>();
        relief_scale_m_ = height_contract.at("relief_scale_m").get<float>();
        if (!std::isfinite(height_offset_m_) || !std::isfinite(height_scale_) ||
            height_scale_ <= 0.0F || !std::isfinite(relief_scale_m_) || relief_scale_m_ <= 0.0F) {
            throw std::runtime_error("invalid terrain heightfield transform");
        }

        const nlohmann::json& elevation = document.at("files").at("elevation");
        provenance_.elevation_sha256 = elevation.at("sha256").get<std::string>();
        if (elevation.at("dtype").get<std::string_view>() != "float32-le" ||
            elevation.at("layout").get<std::string_view>() != "row-major-zx" ||
            elevation.at("shape") != nlohmann::json::array({height, width}) ||
            !valid_sha256(elevation.at("sha256").get<std::string_view>())) {
            throw std::runtime_error("invalid terrain raster elevation contract");
        }
        const std::filesystem::path relative_path = elevation.at("path").get<std::string>();
        if (!safe_relative_path(relative_path)) {
            throw std::runtime_error("terrain raster elevation path must remain inside field");
        }
        const std::size_t count = static_cast<std::size_t>(width) * height;
        Level base{
            .width = width,
            .height = height,
            .spacing_m = spacing,
            .origin_x_m = origin_x,
            .origin_z_m = origin_z,
            .values = read_elevation(manifest.parent_path() / relative_path, count,
                                     elevation.at("byte_count").get<std::uint64_t>()),
        };
        levels_.push_back(std::move(base));
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("invalid terrain raster manifest contract: " +
                                 std::string(error.what()));
    }

    while (levels_.back().width > 1U || levels_.back().height > 1U) {
        const Level& previous = levels_.back();
        Level next{
            .width = (previous.width + 1U) / 2U,
            .height = (previous.height + 1U) / 2U,
            .spacing_m = previous.spacing_m * 2.0F,
            .origin_x_m =
                previous.origin_x_m + (previous.width > 1U ? previous.spacing_m * 0.5F : 0.0F),
            .origin_z_m =
                previous.origin_z_m + (previous.height > 1U ? previous.spacing_m * 0.5F : 0.0F),
        };
        next.values.resize(static_cast<std::size_t>(next.width) * next.height);
        for (std::uint32_t z = 0U; z < next.height; ++z) {
            for (std::uint32_t x = 0U; x < next.width; ++x) {
                float sum = 0.0F;
                std::uint32_t sample_count = 0U;
                for (std::uint32_t dz = 0U; dz < 2U; ++dz) {
                    for (std::uint32_t dx = 0U; dx < 2U; ++dx) {
                        const std::uint32_t source_x = x * 2U + dx;
                        const std::uint32_t source_z = z * 2U + dz;
                        if (source_x < previous.width && source_z < previous.height) {
                            sum += previous
                                       .values[static_cast<std::size_t>(source_z) * previous.width +
                                               source_x];
                            ++sample_count;
                        }
                    }
                }
                next.values[static_cast<std::size_t>(z) * next.width + x] =
                    sum / static_cast<float>(sample_count);
            }
        }
        levels_.push_back(std::move(next));
    }
}

TerrainHeightSourceMetadata TerrainRasterHeightSource::metadata() const noexcept {
    return {
        .id = source_id_,
        .seed = seed_,
        .base_height_m = 0.0F,
        .relief_scale_m = relief_scale_m_,
        .gradient_step_m = levels_.front().spacing_m,
    };
}

float TerrainRasterHeightSource::sample_level(const Level& level,
                                              cubey::math::Vec2 world_xz) const {
    const float sample_x = (world_xz.x - level.origin_x_m) / level.spacing_m;
    const float sample_z = (world_xz.y - level.origin_z_m) / level.spacing_m;
    const float clamped_x = std::clamp(sample_x, 0.0F, static_cast<float>(level.width - 1U));
    const float clamped_z = std::clamp(sample_z, 0.0F, static_cast<float>(level.height - 1U));
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
    const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(clamped_z));
    const std::uint32_t x1 = std::min(x0 + 1U, level.width - 1U);
    const std::uint32_t z1 = std::min(z0 + 1U, level.height - 1U);
    const float tx = clamped_x - static_cast<float>(x0);
    const float tz = clamped_z - static_cast<float>(z0);
    const auto at = [&level](std::uint32_t x, std::uint32_t z) {
        return level.values[static_cast<std::size_t>(z) * level.width + x];
    };
    return std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx), std::lerp(at(x0, z1), at(x1, z1), tx),
                     tz);
}

float TerrainRasterHeightSource::sample_height(const TerrainQuery& query) const {
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain raster query");
    }
    if (!contains_disk(query.world_xz, 0.0F)) {
        throw std::runtime_error("terrain raster query is outside the baked field");
    }
    const float footprint_ratio = std::max(query.footprint_m / levels_.front().spacing_m, 1.0F);
    const float lod =
        std::clamp(std::log2(footprint_ratio), 0.0F, static_cast<float>(levels_.size() - 1U));
    const std::size_t first = static_cast<std::size_t>(std::floor(lod));
    const std::size_t second = std::min(first + 1U, levels_.size() - 1U);
    const float blend = lod - static_cast<float>(first);
    const float raw = std::lerp(sample_level(levels_[first], query.world_xz),
                                sample_level(levels_[second], query.world_xz), blend);
    return (raw + height_offset_m_) * height_scale_;
}

bool TerrainRasterHeightSource::contains_disk(cubey::math::Vec2 center_xz,
                                              float radius_m) const noexcept {
    if (levels_.empty() || !std::isfinite(center_xz.x) || !std::isfinite(center_xz.y) ||
        !std::isfinite(radius_m) || radius_m < 0.0F) {
        return false;
    }
    const Level& base = levels_.front();
    const float maximum_x = base.origin_x_m + static_cast<float>(base.width - 1U) * base.spacing_m;
    const float maximum_z = base.origin_z_m + static_cast<float>(base.height - 1U) * base.spacing_m;
    return center_xz.x - radius_m >= base.origin_x_m && center_xz.x + radius_m <= maximum_x &&
           center_xz.y - radius_m >= base.origin_z_m && center_xz.y + radius_m <= maximum_z;
}

std::uint32_t TerrainRasterHeightSource::width() const noexcept {
    return levels_.front().width;
}

std::uint32_t TerrainRasterHeightSource::height() const noexcept {
    return levels_.front().height;
}

float TerrainRasterHeightSource::sample_spacing_m() const noexcept {
    return levels_.front().spacing_m;
}

const TerrainRasterProvenance& TerrainRasterHeightSource::provenance() const noexcept {
    return provenance_;
}

} // namespace cubey::projects::terrain
