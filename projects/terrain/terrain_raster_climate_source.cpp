#include "terrain_raster_climate_source.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::terrain {
namespace {

constexpr std::string_view kSchema = "cubey.terrain.surface-fields.study.v1";
constexpr std::uint32_t kChannelCount = 4U;
constexpr std::uint32_t kMaximumDimension = 16'384U;
constexpr std::array<std::pair<std::string_view, std::string_view>, kChannelCount> kChannels{
    std::pair{"temperature_mean", "deg_c"},
    std::pair{"temperature_stddev", "deg_c"},
    std::pair{"precipitation_annual", "mm_per_year"},
    std::pair{"precipitation_cv", "fraction"},
};

[[nodiscard]] bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    return std::none_of(path.begin(), path.end(),
                        [](const std::filesystem::path& component) { return component == ".."; });
}

[[nodiscard]] std::filesystem::path manifest_path(const std::filesystem::path& field_path) {
    return std::filesystem::is_directory(field_path) ? field_path / "surface-fields.json"
                                                     : field_path;
}

[[nodiscard]] nlohmann::json read_manifest(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("failed to open terrain climate manifest: " + path.string());
    }
    try {
        return nlohmann::json::parse(stream);
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("invalid terrain climate manifest " + path.string() + ": " +
                                 error.what());
    }
}

[[nodiscard]] bool valid_sha256(std::string_view value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char digit) {
               return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
           });
}

[[nodiscard]] std::vector<float> read_climate(const std::filesystem::path& path,
                                              std::size_t count,
                                              std::uint64_t declared_byte_count) {
    if constexpr (std::endian::native != std::endian::little) {
        throw std::runtime_error("terrain climate source requires a little-endian host");
    }
    const std::uint64_t expected_bytes = static_cast<std::uint64_t>(count) * sizeof(float);
    std::error_code error;
    const std::uint64_t actual_bytes = std::filesystem::file_size(path, error);
    if (error || declared_byte_count != expected_bytes || actual_bytes != expected_bytes) {
        throw std::runtime_error("terrain climate byte count does not match manifest");
    }
    std::vector<float> values(count);
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char*>(values.data()),
                                static_cast<std::streamsize>(expected_bytes))) {
        throw std::runtime_error("failed to read terrain climate field: " + path.string());
    }
    if (!std::all_of(values.begin(), values.end(),
                     [](float value) { return std::isfinite(value); })) {
        throw std::runtime_error("terrain climate field contains non-finite values");
    }
    return values;
}

} // namespace

TerrainRasterClimateSource::TerrainRasterClimateSource(
    const std::filesystem::path& field_path) {
    const std::filesystem::path manifest = manifest_path(field_path);
    metadata_.manifest_path = std::filesystem::absolute(manifest).lexically_normal();
    const nlohmann::json document = read_manifest(manifest);
    try {
        if (document.at("schema").get<std::string_view>() != kSchema) {
            throw std::runtime_error("unsupported terrain climate manifest schema");
        }
        const nlohmann::json& source = document.at("source");
        metadata_.generator = source.value("generator", std::string{});
        metadata_.model_id = source.value("model_id", std::string{});
        metadata_.seed = document.at("seed").get<std::uint64_t>();
        metadata_.elevation_sha256 =
            document.at("heightfield").at("elevation_sha256").get<std::string>();
        if (!valid_sha256(metadata_.elevation_sha256)) {
            throw std::runtime_error("invalid terrain climate elevation binding");
        }

        const nlohmann::json& grid = document.at("grid");
        metadata_.width = grid.at("width").get<std::uint32_t>();
        metadata_.height = grid.at("height").get<std::uint32_t>();
        metadata_.sample_spacing_m = grid.at("sample_spacing_m").get<float>();
        origin_x_m_ = grid.at("sample_origin_x_m").get<float>();
        origin_z_m_ = grid.at("sample_origin_z_m").get<float>();
        if (metadata_.width < 2U || metadata_.height < 2U ||
            metadata_.width > kMaximumDimension || metadata_.height > kMaximumDimension ||
            !std::isfinite(metadata_.sample_spacing_m) || metadata_.sample_spacing_m <= 0.0F ||
            !std::isfinite(origin_x_m_) || !std::isfinite(origin_z_m_)) {
            throw std::runtime_error("invalid terrain climate grid metadata");
        }

        const nlohmann::json& climate = document.at("files").at("climate");
        metadata_.climate_sha256 = climate.at("sha256").get<std::string>();
        if (climate.at("dtype").get<std::string_view>() != "float32-le" ||
            climate.at("layout").get<std::string_view>() != "channel-major-zx" ||
            climate.at("shape") != nlohmann::json::array(
                                       {kChannelCount, metadata_.height, metadata_.width}) ||
            !valid_sha256(metadata_.climate_sha256)) {
            throw std::runtime_error("invalid terrain climate payload contract");
        }
        const nlohmann::json& channels = climate.at("channels");
        if (!channels.is_array() || channels.size() != kChannels.size()) {
            throw std::runtime_error("invalid terrain climate channel contract");
        }
        for (std::size_t index = 0U; index < kChannels.size(); ++index) {
            if (channels.at(index).at("name").get<std::string_view>() != kChannels[index].first ||
                channels.at(index).at("unit").get<std::string_view>() != kChannels[index].second) {
                throw std::runtime_error("unsupported terrain climate channel order or unit");
            }
        }
        const std::filesystem::path relative_path = climate.at("path").get<std::string>();
        if (!safe_relative_path(relative_path)) {
            throw std::runtime_error("terrain climate path must remain inside field");
        }
        const std::size_t plane = static_cast<std::size_t>(metadata_.width) * metadata_.height;
        values_ = read_climate(manifest.parent_path() / relative_path, kChannelCount * plane,
                               climate.at("byte_count").get<std::uint64_t>());

        const auto plane_values = [this, plane](std::uint32_t channel) {
            return std::span<const float>(values_).subspan(static_cast<std::size_t>(channel) * plane,
                                                           plane);
        };
        if (std::any_of(plane_values(1U).begin(), plane_values(1U).end(),
                        [](float value) { return value < 0.0F; }) ||
            std::any_of(plane_values(2U).begin(), plane_values(2U).end(),
                        [](float value) { return value < 0.0F; }) ||
            std::any_of(plane_values(3U).begin(), plane_values(3U).end(),
                        [](float value) { return value < 0.0F || value > 1.0F; })) {
            throw std::runtime_error("terrain climate values violate declared physical ranges");
        }
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("invalid terrain climate manifest contract: " +
                                 std::string(error.what()));
    }
}

float TerrainRasterClimateSource::sample_channel(std::uint32_t channel,
                                                 cubey::math::Vec2 world_xz) const {
    const float sample_x = (world_xz.x - origin_x_m_) / metadata_.sample_spacing_m;
    const float sample_z = (world_xz.y - origin_z_m_) / metadata_.sample_spacing_m;
    const float clamped_x =
        std::clamp(sample_x, 0.0F, static_cast<float>(metadata_.width - 1U));
    const float clamped_z =
        std::clamp(sample_z, 0.0F, static_cast<float>(metadata_.height - 1U));
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(clamped_x));
    const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(clamped_z));
    const std::uint32_t x1 = std::min(x0 + 1U, metadata_.width - 1U);
    const std::uint32_t z1 = std::min(z0 + 1U, metadata_.height - 1U);
    const float tx = clamped_x - static_cast<float>(x0);
    const float tz = clamped_z - static_cast<float>(z0);
    const std::size_t plane = static_cast<std::size_t>(metadata_.width) * metadata_.height;
    const auto at = [this, channel, plane](std::uint32_t x, std::uint32_t z) {
        return values_[static_cast<std::size_t>(channel) * plane +
                       static_cast<std::size_t>(z) * metadata_.width + x];
    };
    return std::lerp(std::lerp(at(x0, z0), at(x1, z0), tx),
                     std::lerp(at(x0, z1), at(x1, z1), tx), tz);
}

TerrainClimateSample TerrainRasterClimateSource::sample(cubey::math::Vec2 world_xz) const {
    if (!std::isfinite(world_xz.x) || !std::isfinite(world_xz.y) || !contains(world_xz)) {
        throw std::runtime_error("terrain climate query is outside the baked field");
    }
    return {
        .temperature_mean_c = sample_channel(0U, world_xz),
        .temperature_stddev_c = sample_channel(1U, world_xz),
        .precipitation_annual_mm = sample_channel(2U, world_xz),
        .precipitation_cv = sample_channel(3U, world_xz),
    };
}

TerrainHeightSourceBounds TerrainRasterClimateSource::bounds() const noexcept {
    return {
        .minimum_xz = {origin_x_m_, origin_z_m_},
        .maximum_xz =
            {
                origin_x_m_ + static_cast<float>(metadata_.width - 1U) *
                                  metadata_.sample_spacing_m,
                origin_z_m_ + static_cast<float>(metadata_.height - 1U) *
                                  metadata_.sample_spacing_m,
            },
    };
}

bool TerrainRasterClimateSource::contains(cubey::math::Vec2 world_xz) const noexcept {
    const TerrainHeightSourceBounds field = bounds();
    const float half_cell = metadata_.sample_spacing_m * 0.5F;
    return world_xz.x >= field.minimum_xz.x - half_cell &&
           world_xz.x <= field.maximum_xz.x + half_cell &&
           world_xz.y >= field.minimum_xz.y - half_cell &&
           world_xz.y <= field.maximum_xz.y + half_cell;
}

const TerrainRasterClimateMetadata& TerrainRasterClimateSource::metadata() const noexcept {
    return metadata_;
}

void validate_terrain_climate_binding(const TerrainRasterHeightSource& height,
                                      const TerrainRasterClimateSource& climate) {
    if (height.metadata().seed != climate.metadata().seed ||
        height.provenance().elevation_sha256 != climate.metadata().elevation_sha256) {
        throw std::runtime_error("terrain climate source does not match the active heightfield");
    }
    const TerrainHeightSourceBounds height_bounds = height.bounds();
    const TerrainHeightSourceBounds climate_bounds = climate.bounds();
    if (climate_bounds.minimum_xz.x < height_bounds.minimum_xz.x ||
        climate_bounds.minimum_xz.y < height_bounds.minimum_xz.y ||
        climate_bounds.maximum_xz.x > height_bounds.maximum_xz.x ||
        climate_bounds.maximum_xz.y > height_bounds.maximum_xz.y) {
        throw std::runtime_error("terrain climate source extends outside the active heightfield");
    }
}

} // namespace cubey::projects::terrain
