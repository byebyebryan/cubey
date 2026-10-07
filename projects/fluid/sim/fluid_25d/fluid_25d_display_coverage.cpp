#include "fluid_25d_display_coverage.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {
[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("fluid 2.5D display coverage: " + message);
}

std::vector<std::byte> read_file(const std::filesystem::path& path, std::size_t limit) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        invalid("missing, non-regular or symbolic-link file: " + path.string());
    const auto size = std::filesystem::file_size(path);
    if (size == 0U || size > limit)
        invalid("file size exceeds the bounded sidecar contract");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof())
        invalid("truncated or changing file");
    return bytes;
}

std::filesystem::path safe_payload(const std::filesystem::path& root, const std::string& name) {
    const std::filesystem::path relative(name);
    if (name.empty() || name.find('\0') != std::string::npos ||
        name.find('\\') != std::string::npos || relative.is_absolute() || relative.has_root_path())
        invalid("mask payload path must be relative");
    auto result = root;
    for (const auto& component : relative) {
        if (component == "." || component == ".." || component.empty())
            invalid("mask payload path cannot contain dot components");
        result /= component;
        if (std::filesystem::is_symlink(result))
            invalid("mask payload cannot traverse symbolic links");
    }
    return result;
}
} // namespace

Fluid25DDisplayCoverage::Fluid25DDisplayCoverage(const std::filesystem::path& path,
                                                 const Fluid25DRecording& recording) {
    if (recording.is_live_stream())
        invalid("prebaked coverage supports completed recordings only");
    const auto manifest = read_file(path, 2U * 1024U * 1024U);
    const auto* manifest_begin = reinterpret_cast<const char*>(manifest.data());
    const auto document = nlohmann::json::parse(manifest_begin, manifest_begin + manifest.size());
    if (document.at("schema") != "cubey.fluid25d.display_coverage.v1" ||
        document.at("encoding") != "float32-little-endian" ||
        document.at("temporal_interpolation") != false)
        invalid("unsupported schema, encoding or interpolation policy");
    const auto source_bytes = read_file(recording.manifest_path(), 8U * 1024U * 1024U);
    if (document.at("source_manifest_sha256") != asset::sha256_hex(source_bytes))
        invalid("sidecar does not match the recording manifest");
    const auto* source_begin = reinterpret_cast<const char*>(source_bytes.data());
    const auto source = nlohmann::json::parse(source_begin, source_begin + source_bytes.size());
    const auto& grid = document.at("grid");
    if (grid.at("width") != recording.grid_width() ||
        grid.at("height") != recording.grid_height() ||
        grid.at("cell_size_m").get<double>() != recording.cell_size_m())
        invalid("sidecar grid differs from recording");
    if (!document.at("subdivision").is_number_integer() ||
        (document.at("subdivision") != 2 && document.at("subdivision") != 4))
        invalid("sidecar subdivision must be integer 2 or 4");
    subdivision_ = document.at("subdivision").get<std::uint32_t>();
    if (subdivision_ != 2U && subdivision_ != 4U)
        invalid("sidecar subdivision must be 2 or 4");
    const std::uint64_t width = (recording.grid_width() - 1U) * subdivision_ + 1U;
    const std::uint64_t height = (recording.grid_height() - 1U) * subdivision_ + 1U;
    if (width * height > 16'777'216U)
        invalid("sidecar mask exceeds the sample budget");
    samples_ = static_cast<std::size_t>(width * height);
    label_ = document.at("variant").get<std::string>();
    if (label_.empty() || label_.size() > 64U)
        invalid("invalid variant label");
    std::map<double, std::string> source_hashes;
    for (const auto& row : source.at("frames"))
        source_hashes.emplace(row.at("time_s").get<double>(), row.at("sha256").get<std::string>());
    const auto& frames = document.at("frames");
    if (!frames.is_array() || frames.empty() || frames.size() > recording.frame_count())
        invalid("empty or oversized mask timeline");
    double previous = -1.0;
    const auto root = std::filesystem::absolute(path).parent_path();
    for (const auto& row : frames) {
        const double time = row.at("time_s").get<double>();
        const auto source_hash = source_hashes.find(time);
        const auto digest = row.at("sha256").get<std::string>();
        if (!std::isfinite(time) || time <= previous || source_hash == source_hashes.end() ||
            row.at("source_frame_sha256") != source_hash->second || !asset::is_sha256_hex(digest) ||
            row.at("bytes").get<std::uint64_t>() != samples_ * sizeof(float))
            invalid("invalid or mismatched mask timeline entry");
        entries_.push_back({time, safe_payload(root, row.at("path").get<std::string>()), digest});
        previous = time;
    }
}

bool Fluid25DDisplayCoverage::has_frame(double time) const noexcept {
    if (!std::isfinite(time))
        return false;
    const auto row =
        std::lower_bound(entries_.begin(), entries_.end(), time,
                         [](const Entry& entry, double value) { return entry.time_s < value; });
    return row != entries_.end() && row->time_s == time;
}

std::vector<double> Fluid25DDisplayCoverage::times_s() const {
    std::vector<double> times;
    times.reserve(entries_.size());
    for (const auto& entry : entries_)
        times.push_back(entry.time_s);
    return times;
}

Fluid25DDisplayCoverageCache::Fluid25DDisplayCoverageCache(
    const Fluid25DDisplayCoverage& source, std::span<const double> times,
    const std::function<void(std::size_t)>& progress) {
    if (times.empty() || times.size() > 16U || source.frame_bytes() > kByteBudget / times.size())
        invalid("comparison cache exceeds the 16-frame / 256 MiB budget");
    const auto started = std::chrono::steady_clock::now();
    double previous = -1.0;
    for (double time : times) {
        if (!std::isfinite(time) || time <= previous)
            invalid("invalid comparison cache timeline");
        frames_.emplace(time, source.frame(time));
        bytes_ += source.frame_bytes();
        previous = time;
        if (progress)
            progress(frames_.size());
    }
    preparation_ms_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
}

std::span<const float> Fluid25DDisplayCoverageCache::frame(double time) const {
    if (!std::isfinite(time))
        invalid("cached mask time must be finite");
    const auto found = frames_.find(time);
    if (found == frames_.end())
        invalid("no cached mask for saved time " + std::to_string(time));
    return found->second;
}

std::vector<float> Fluid25DDisplayCoverage::frame(double saved_time_s) const {
    if (std::endian::native != std::endian::little)
        invalid("this experimental reader requires a little-endian host");
    const auto found =
        std::lower_bound(entries_.begin(), entries_.end(), saved_time_s,
                         [](const Entry& entry, double time) { return entry.time_s < time; });
    if (found == entries_.end() || found->time_s != saved_time_s)
        invalid("no mask for saved time " + std::to_string(saved_time_s) +
                "; use a baked time or disable the sidecar");
    const auto bytes = read_file(found->path, samples_ * sizeof(float));
    if (bytes.size() != samples_ * sizeof(float) || asset::sha256_hex(bytes) != found->sha256)
        invalid("mask size or SHA-256 mismatch");
    std::vector<float> values(samples_);
    std::memcpy(values.data(), bytes.data(), bytes.size());
    if (std::any_of(values.begin(), values.end(), [](float value) {
            return !std::isfinite(value) || value < 0.0F || value > 1.0F;
        }))
        invalid("mask values must be finite and in [0,1]");
    return values;
}

} // namespace cubey::projects::fluid::fluid_25d
