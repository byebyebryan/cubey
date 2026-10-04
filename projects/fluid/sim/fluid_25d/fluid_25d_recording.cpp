#include "fluid_25d_recording.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <list>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kSchema = "cubey.fluid25d.recording.v1";
constexpr std::string_view kStreamSchema = "cubey.fluid25d.stream.v1";
constexpr std::string_view kEncoding = "float32-little-endian";
constexpr std::size_t kMaximumCells = 4'194'304U;
constexpr std::uint32_t kMaximumDimension = 16'384U;
constexpr std::size_t kMaximumManifestBytes = 8U * 1024U * 1024U;
constexpr std::size_t kMaximumBedBytes = kMaximumCells * sizeof(float);
constexpr std::size_t kMaximumFrameBytes = kMaximumCells * 3U * sizeof(float);
constexpr std::size_t kMaximumCachedFrames = 3U;
constexpr double kMinimumPlaybackRate = 0.125;
constexpr double kMaximumPlaybackRate = 300.0;

[[noreturn]] void invalid(std::string message) {
    throw std::runtime_error("fluid 2.5D recording: " + std::move(message));
}

[[nodiscard]] const Json& required_member(const Json& object, std::string_view key,
                                          std::string_view context) {
    if (!object.is_object() || !object.contains(std::string(key))) {
        invalid(std::string(context) + " is missing " + std::string(key));
    }
    return object.at(std::string(key));
}

[[nodiscard]] std::string required_string(const Json& value, std::string_view label) {
    if (!value.is_string()) {
        invalid(std::string(label) + " must be a string");
    }
    return value.get<std::string>();
}

[[nodiscard]] std::uint64_t required_unsigned(const Json& value, std::string_view label) {
    std::uint64_t parsed = 0U;
    if (value.is_number_unsigned()) {
        parsed = value.get<std::uint64_t>();
    } else if (value.is_number_integer()) {
        const std::int64_t signed_value = value.get<std::int64_t>();
        if (signed_value < 0) {
            invalid(std::string(label) + " must be nonnegative");
        }
        parsed = static_cast<std::uint64_t>(signed_value);
    } else {
        invalid(std::string(label) + " must be an integer");
    }
    return parsed;
}

[[nodiscard]] double required_finite_number(const Json& value, std::string_view label) {
    if (!value.is_number()) {
        invalid(std::string(label) + " must be numeric");
    }
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed)) {
        invalid(std::string(label) + " must be finite");
    }
    return parsed;
}

[[nodiscard]] float required_finite_float(const Json& value, std::string_view label) {
    const double parsed = required_finite_number(value, label);
    if (std::abs(parsed) > static_cast<double>(std::numeric_limits<float>::max())) {
        invalid(std::string(label) + " is outside float32 range");
    }
    const float result = static_cast<float>(parsed);
    if (!std::isfinite(result)) {
        invalid(std::string(label) + " is not representable as float32");
    }
    return result;
}

[[nodiscard]] std::uint32_t required_dimension(const Json& value, std::string_view label) {
    const std::uint64_t parsed = required_unsigned(value, label);
    if (parsed < 2U || parsed > kMaximumDimension) {
        invalid(std::string(label) + " is outside the supported dimension range");
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] bool safe_relative_path(std::string_view value) {
    if (value.empty() || value.find('\0') != std::string_view::npos ||
        value.find('\\') != std::string_view::npos) {
        return false;
    }
    const std::filesystem::path path{value};
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const std::filesystem::path& part : path) {
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool path_is_within(const std::filesystem::path& root,
                                  const std::filesystem::path& child) {
    auto root_part = root.begin();
    auto child_part = child.begin();
    for (; root_part != root.end() && child_part != child.end(); ++root_part, ++child_part) {
        if (*root_part != *child_part) {
            return false;
        }
    }
    return root_part == root.end();
}

[[nodiscard]] std::filesystem::path safe_file_path(const std::filesystem::path& root,
                                                   std::string_view relative) {
    if (!safe_relative_path(relative)) {
        invalid("payload path must be relative and cannot contain dot or parent components");
    }
    std::filesystem::path result = root;
    for (const std::filesystem::path& part : std::filesystem::path{relative}) {
        result /= part;
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::symlink_status(result, error);
        if (error || status.type() == std::filesystem::file_type::not_found) {
            invalid("payload file does not exist: " + result.string());
        }
        if (std::filesystem::is_symlink(status)) {
            invalid("payload paths cannot use symbolic links: " + result.string());
        }
    }
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::canonical(result, error);
    if (error || !path_is_within(root, canonical) || !std::filesystem::is_regular_file(canonical)) {
        invalid("payload file is not a regular file inside the recording directory");
    }
    return canonical;
}

[[nodiscard]] std::vector<std::uint8_t>
read_file(const std::filesystem::path& path, std::size_t expected_size, std::size_t maximum_size) {
    std::error_code error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, error);
    if (error || file_size > maximum_size || file_size != expected_size ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        invalid("payload byte count does not match the recording manifest: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()))) {
        invalid("failed to read recording payload: " + path.string());
    }
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> read_manifest_bytes(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, error);
    if (error || file_size == 0U || file_size > kMaximumManifestBytes ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        invalid("manifest is empty or exceeds the supported size");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()))) {
        invalid("failed to read recording manifest: " + path.string());
    }
    return bytes;
}

[[nodiscard]] std::vector<float> decode_float32_le(std::span<const std::uint8_t> bytes,
                                                   std::size_t count) {
    if (bytes.size() != count * sizeof(float)) {
        invalid("float32 payload has an invalid byte count");
    }
    std::vector<float> values(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::size_t offset = index * sizeof(float);
        const std::uint32_t bits = static_cast<std::uint32_t>(bytes[offset]) |
                                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
        values[index] = std::bit_cast<float>(bits);
        if (!std::isfinite(values[index])) {
            invalid("float32 payload contains a non-finite value");
        }
    }
    return values;
}

[[nodiscard]] std::vector<float> read_float32_file(const std::filesystem::path& root,
                                                   const Json& file_descriptor, std::size_t count,
                                                   std::unordered_set<std::string>& paths,
                                                   std::string_view label) {
    if (!file_descriptor.is_object()) {
        invalid(std::string(label) + " descriptor must be an object");
    }
    const std::string relative = required_string(required_member(file_descriptor, "path", label),
                                                 std::string(label) + " path");
    const std::string digest = required_string(required_member(file_descriptor, "sha256", label),
                                               std::string(label) + " sha256");
    if (!cubey::asset::is_sha256_hex(digest)) {
        invalid(std::string(label) + " has an invalid SHA-256 digest");
    }
    if (!paths.insert(relative).second) {
        invalid("recording manifest reuses a payload path");
    }
    const std::filesystem::path path = safe_file_path(root, relative);
    const std::size_t byte_count = count * sizeof(float);
    std::vector<std::uint8_t> bytes = read_file(path, byte_count, kMaximumBedBytes);
    if (cubey::asset::sha256_hex(std::as_bytes(std::span{bytes})) != digest) {
        invalid(std::string(label) + " SHA-256 does not match its payload");
    }
    return decode_float32_le(bytes, count);
}

[[nodiscard]] bool nearly_equal(double left, double right, double relative_tolerance,
                                double absolute_tolerance) noexcept {
    return std::abs(left - right) <=
           std::max(absolute_tolerance,
                    relative_tolerance * std::max(std::abs(left), std::abs(right)));
}

} // namespace

struct Fluid25DRecording::Impl {
    struct FrameDescriptor {
        double time_s = 0.0;
        std::string path{};
        std::string sha256{};
        Fluid25DRecordedFrameStats declared_stats{};
        double published_unix_s = 0.0;
    };

    std::filesystem::path root{};
    std::filesystem::path manifest{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    float cell_size_m = 0.0F;
    double rain_rate_mm_per_hour = 0.0;
    std::vector<Fluid25DRecordedRainKnot> rainfall_history{};
    std::vector<float> bed{};
    std::vector<float> source_bed{};
    std::vector<double> times{};
    std::vector<FrameDescriptor> descriptors{};
    Json protocol_json{};
    Json provenance_json{};
    Json document_json{};
    bool live_stream = false;
    std::string session{};
    std::uint64_t stream_revision = 0U;
    std::string state{};
    std::string message{};
    double native_time_s = 0.0;
    bool native_running = false;
    mutable std::unordered_map<std::size_t, std::shared_ptr<const Fluid25DRecordedFrame>> cache{};
    mutable std::list<std::size_t> recency{};

    explicit Impl(const std::filesystem::path& recording_path, bool live) : live_stream(live) {
        std::error_code error;
        const std::filesystem::path input = std::filesystem::absolute(recording_path, error);
        if (error) {
            invalid("recording path cannot be made absolute");
        }
        if (std::filesystem::is_directory(input)) {
            root = std::filesystem::canonical(input, error);
            manifest = root / (live_stream ? "stream.json" : "recording.json");
        } else {
            manifest = std::filesystem::canonical(input, error);
            if (!error) {
                root = manifest.parent_path();
            }
        }
        if (error || root.empty() || !std::filesystem::is_directory(root)) {
            invalid("recording path must be a directory or its recording.json manifest");
        }
        const std::filesystem::file_status manifest_status =
            std::filesystem::symlink_status(manifest, error);
        if (error || std::filesystem::is_symlink(manifest_status) ||
            !std::filesystem::is_regular_file(manifest_status)) {
            invalid("recording manifest must be a regular file and cannot be a symbolic link");
        }
        const std::vector<std::uint8_t> manifest_bytes = read_manifest_bytes(manifest);
        try {
            protocol_from_manifest(manifest_bytes);
        } catch (const Json::exception& exception) {
            invalid("manifest JSON is malformed: " + std::string(exception.what()));
        }
    }

    void protocol_from_manifest(std::span<const std::uint8_t> bytes) {
        Json document = Json::parse(bytes.begin(), bytes.end());
        if (!document.is_object() ||
            required_string(required_member(document, "schema", "manifest"), "schema") !=
                (live_stream ? kStreamSchema : kSchema)) {
            invalid("unsupported manifest schema");
        }
        document_json = document;
        if (live_stream) {
            session =
                required_string(required_member(document, "session_id", "stream"), "session_id");
            if (session.size() != 32U ||
                session.find_first_not_of("0123456789abcdef") != std::string::npos)
                invalid("stream session_id must be a 32-character lowercase hex identity");
            stream_revision =
                required_unsigned(required_member(document, "revision", "stream"), "revision");
            const Json& producer = required_member(document, "producer", "stream");
            state =
                required_string(required_member(producer, "state", "producer"), "producer state");
            if (state != "running" && state != "completed" && state != "failed" &&
                state != "cancelled")
                invalid("invalid producer lifecycle state");
            (void)required_unsigned(required_member(producer, "pid", "producer"), "producer pid");
            message = required_string(required_member(producer, "message", "producer"),
                                      "producer message");
            native_time_s = required_finite_number(
                required_member(producer, "latest_native_time_s", "producer"), "native time");
            const Json& computing = required_member(producer, "native_running", "producer");
            if (!computing.is_boolean())
                invalid("native_running must be boolean");
            native_running = computing.get<bool>();
            if (state != "running" && native_running)
                invalid("terminated producer cannot claim native computation");
        }
        if (required_string(required_member(document, "encoding", "manifest"), "encoding") !=
                kEncoding ||
            required_string(required_member(document, "field_layout", "manifest"),
                            "field_layout") != "planar") {
            invalid("unsupported encoding or field layout");
        }
        const Json& fields = required_member(document, "fields", "manifest");
        if (fields != Json::array({"h_m", "qx_m2_per_s", "qz_m2_per_s"})) {
            invalid("unsupported field order or units");
        }

        const Json& grid = required_member(document, "grid", "manifest");
        width = required_dimension(required_member(grid, "width", "grid"), "grid width");
        height = required_dimension(required_member(grid, "height", "grid"), "grid height");
        const std::size_t cell_count = static_cast<std::size_t>(width) * height;
        if (cell_count > kMaximumCells) {
            invalid("grid exceeds the supported cell limit");
        }
        const double cell_size = required_finite_number(
            required_member(grid, "cell_size_m", "grid"), "grid cell_size_m");
        if (cell_size <= 0.0 ||
            cell_size > static_cast<double>(std::numeric_limits<float>::max())) {
            invalid("grid cell_size_m must be a positive float32 value");
        }
        cell_size_m = static_cast<float>(cell_size);
        if (!std::isfinite(cell_size_m) || cell_size_m <= 0.0F ||
            required_string(required_member(grid, "storage_order", "grid"), "storage_order") !=
                "row-major" ||
            required_string(required_member(grid, "row_direction", "grid"), "row_direction") !=
                "world-z-positive" ||
            required_string(required_member(grid, "sample_location", "grid"), "sample_location") !=
                "cell-center") {
            invalid("grid geometry or coordinate convention is unsupported");
        }

        protocol_json = required_member(document, "protocol", "manifest");
        if (!protocol_json.is_object()) {
            invalid("protocol must be an object");
        }
        const std::uint64_t protocol_width =
            required_unsigned(required_member(protocol_json, "cols", "protocol"), "protocol cols");
        const std::uint64_t protocol_height =
            required_unsigned(required_member(protocol_json, "rows", "protocol"), "protocol rows");
        const double protocol_cell = required_finite_number(
            required_member(protocol_json, "dx_m", "protocol"), "protocol dx_m");
        const double duration = required_finite_number(
            required_member(protocol_json, "duration_s", "protocol"), "protocol duration_s");
        const double output_interval =
            required_finite_number(required_member(protocol_json, "output_interval_s", "protocol"),
                                   "protocol output_interval_s");
        const double rain_rate_m_per_s =
            required_finite_number(required_member(protocol_json, "rain_rate_m_per_s", "protocol"),
                                   "protocol rain_rate_m_per_s");
        if (protocol_width != width || protocol_height != height ||
            !nearly_equal(protocol_cell, cell_size_m, 1.0e-7, 1.0e-7) || duration <= 0.0 ||
            output_interval <= 0.0 || rain_rate_m_per_s < 0.0) {
            invalid("protocol geometry, duration, or rainfall does not match recording");
        }
        rain_rate_mm_per_hour = rain_rate_m_per_s * 3'600'000.0;
        if (!std::isfinite(rain_rate_mm_per_hour)) {
            invalid("protocol rainfall is outside the supported range");
        }
        if (protocol_json.contains("rainfall_history")) {
            const Json& history = protocol_json.at("rainfall_history");
            if (!history.is_array() || history.size() < 2U) {
                invalid("protocol rainfall_history must contain at least two time/rate pairs");
            }
            rainfall_history.reserve(history.size());
            for (std::size_t index = 0U; index < history.size(); ++index) {
                const Json& pair = history.at(index);
                if (!pair.is_array() || pair.size() != 2U) {
                    invalid("protocol rainfall_history entries must be [time_s, rate_m_per_s]");
                }
                const double time = required_finite_number(pair.at(0), "rainfall_history time_s");
                const double rate =
                    required_finite_number(pair.at(1), "rainfall_history rate_m_per_s");
                if (time < 0.0 || rate < 0.0 || (index == 0U && time != 0.0) ||
                    (index > 0U && time <= rainfall_history.back().time_s) ||
                    !std::isfinite(rate * 3'600'000.0)) {
                    invalid("rainfall_history must use nonnegative rates and strictly increasing "
                            "times");
                }
                rainfall_history.push_back({time, rate});
            }
            if (rainfall_history.back().time_s != duration ||
                !nearly_equal(rainfall_history.front().rate_m_per_s, rain_rate_m_per_s, 2.0e-8,
                              1.0e-14)) {
                invalid("rainfall_history endpoints or initial rate do not match protocol");
            }
            double scheduled_depth_m = 0.0;
            for (std::size_t index = 0U; index + 1U < rainfall_history.size(); ++index) {
                const auto& start = rainfall_history[index];
                const auto& end = rainfall_history[index + 1U];
                scheduled_depth_m +=
                    (start.rate_m_per_s + end.rate_m_per_s) * 0.5 * (end.time_s - start.time_s);
                if (!std::isfinite(scheduled_depth_m) ||
                    !std::isfinite(scheduled_depth_m * 1000.0)) {
                    invalid("rainfall_history cumulative depth is outside the supported range");
                }
            }
        } else {
            // Older recordings specified only a constant nominal rain rate.
            rainfall_history = {{0.0, rain_rate_m_per_s}, {duration, rain_rate_m_per_s}};
        }

        provenance_json = required_member(document, "provenance", "manifest");
        if (!provenance_json.is_object()) {
            invalid("provenance must be an object");
        }
        validate_provenance(provenance_json);

        std::unordered_set<std::string> payload_paths;
        bed = read_float32_file(root, required_member(document, "bed", "manifest"), cell_count,
                                payload_paths, "native bed");
        source_bed = read_float32_file(root, required_member(document, "source_bed", "manifest"),
                                       cell_count, payload_paths, "source bed");

        const Json& frame_json = required_member(document, "frames", "manifest");
        if (!frame_json.is_array() || frame_json.empty() || frame_json.size() > 100'000U) {
            invalid("frames must be a nonempty array within the supported count limit");
        }
        times.reserve(frame_json.size());
        descriptors.reserve(frame_json.size());
        double previous_time = -1.0;
        for (std::size_t index = 0U; index < frame_json.size(); ++index) {
            const Json& descriptor_json = frame_json.at(index);
            const double time = required_finite_number(
                required_member(descriptor_json, "time_s", "frame"), "frame time_s");
            if (time < 0.0 || time > duration || (index == 0U && time != 0.0) ||
                (index > 0U && time <= previous_time)) {
                invalid("frame timestamps must start at zero and be strictly increasing");
            }
            previous_time = time;
            const std::string relative =
                required_string(required_member(descriptor_json, "path", "frame"), "frame path");
            const std::string digest = required_string(
                required_member(descriptor_json, "sha256", "frame"), "frame sha256");
            if (!cubey::asset::is_sha256_hex(digest) || !payload_paths.insert(relative).second) {
                invalid("frame digest is invalid or a payload path is reused");
            }
            (void)safe_file_path(root, relative);
            Fluid25DRecordedFrameStats stats{};
            stats.water_volume_m3 =
                required_finite_number(required_member(descriptor_json, "water_volume_m3", "frame"),
                                       "frame water_volume_m3");
            stats.max_depth_m = required_finite_float(
                required_member(descriptor_json, "max_depth_m", "frame"), "frame max_depth_m");
            stats.max_speed_m_per_s = required_finite_float(
                required_member(descriptor_json, "max_speed_m_per_s", "frame"),
                "frame max_speed_m_per_s");
            stats.dry_nonzero_momentum_cells = required_unsigned(
                required_member(descriptor_json, "dry_nonzero_momentum_cells", "frame"),
                "frame dry_nonzero_momentum_cells");
            if (stats.water_volume_m3 < 0.0 || stats.max_depth_m < 0.0F ||
                stats.max_speed_m_per_s < 0.0F || stats.dry_nonzero_momentum_cells != 0U) {
                invalid("frame statistics violate the nonnegative dry-state contract");
            }
            times.push_back(time);
            double published = 0.0;
            if (live_stream) {
                published = required_finite_number(
                    required_member(descriptor_json, "published_unix_s", "frame"),
                    "published_unix_s");
                if (published <= 0.0)
                    invalid("live frame publication time must be positive");
            }
            descriptors.push_back({time, relative, digest, stats, published});
        }
        if ((!live_stream || state == "completed") &&
            !nearly_equal(times.back(), duration, 0.0, 1.0e-9)) {
            invalid("the final saved timestamp does not match protocol duration");
        }
        const double interval_count = duration / output_interval;
        if (!std::isfinite(interval_count) || interval_count < 1.0 ||
            std::floor(interval_count) != interval_count || interval_count + 1.0 > 100'000.0 ||
            (live_stream ? static_cast<double>(times.size()) > interval_count + 1.0
                         : interval_count + 1.0 != static_cast<double>(times.size()))) {
            invalid("frame timestamps do not match the protocol output cadence");
        }
        for (std::size_t index = 0U; index < times.size(); ++index) {
            const double expected_time = static_cast<double>(index) * output_interval;
            if (!nearly_equal(times[index], expected_time, 0.0, 1.0e-9)) {
                invalid("frame timestamps do not match the protocol output cadence");
            }
        }
        if (live_stream && (native_time_s < times.back() || native_time_s > duration))
            invalid("native time is outside the published prefix and finite protocol horizon");
        validate_raw_asc_hashes(required_member(provenance_json, "raw_asc_sha256", "provenance"));
    }

    void validate_provenance(const Json& provenance) {
        const Json& source_hashes = required_member(provenance, "source_sha256", "provenance");
        if (!source_hashes.is_object()) {
            invalid("source_sha256 must be an object");
        }
        for (const std::string_view name :
             {"case_spec", "case_protocol", "dem_asc", "native_numerical_bed"}) {
            const std::string digest =
                required_string(required_member(source_hashes, name, "source_sha256"), name);
            if (!cubey::asset::is_sha256_hex(digest)) {
                invalid("provenance contains an invalid source SHA-256 digest");
            }
        }
        const std::string_view audit_key = live_stream ? "pre_solver_audit" : "case_result";
        if (!cubey::asset::is_sha256_hex(required_string(
                required_member(source_hashes, audit_key, "source_sha256"), audit_key)))
            invalid("invalid source audit digest");
        if (live_stream && state == "completed" &&
            !cubey::asset::is_sha256_hex(required_string(
                required_member(source_hashes, "case_result", "source_sha256"), "case_result")))
            invalid("completed stream requires its own completed case audit");
        const Json& raw_hashes = required_member(provenance, "raw_asc_sha256", "provenance");
        if (!raw_hashes.is_array()) {
            invalid("raw_asc_sha256 must be an array");
        }
        const Json& identity = required_member(provenance, "terrain_source_identity", "provenance");
        const Json& precision = required_member(provenance, "precision", "provenance");
        if (!identity.is_object() || !precision.is_object()) {
            invalid("terrain source identity and precision report must be objects");
        }
        for (const std::string_view name : {"elevation_sha256", "fixture_sha256", "manifest_sha256",
                                            "transformed_crop_sha256", "transformed_halo_sha256"}) {
            const std::string digest =
                required_string(required_member(identity, name, "terrain_source_identity"), name);
            if (!cubey::asset::is_sha256_hex(digest)) {
                invalid("terrain source identity contains an invalid SHA-256 digest");
            }
        }
        if (required_string(required_member(precision, "native_numerical_bed_format", "precision"),
                            "native_numerical_bed_format") != "%g (six significant digits)" ||
            required_unsigned(
                required_member(precision, "native_output_ascii_decimal_places", "precision"),
                "native_output_ascii_decimal_places") != 6U ||
            !nearly_equal(required_finite_number(
                              required_member(precision, "native_output_ascii_rounding_half_step",
                                              "precision"),
                              "native_output_ascii_rounding_half_step"),
                          0.0000005, 0.0, 1.0e-15)) {
            invalid("precision report does not match the native serialization limits");
        }
        const Json& sign = required_member(precision, "native_world_z_momentum_sign", "precision");
        if (!sign.is_number_integer() || sign.get<std::int64_t>() != -1) {
            invalid("precision report does not preserve native world-Z momentum sign");
        }
        for (const std::string_view key :
             {"native_bed_vs_source_bed_max_abs_error_m", "native_bed_vs_source_bed_rms_error_m"}) {
            if (required_finite_number(required_member(precision, key, "precision"), key) < 0.0) {
                invalid("precision report contains a negative error bound");
            }
        }
    }

    void validate_raw_asc_hashes(const Json& raw_hashes) const {
        if (!raw_hashes.is_array() || raw_hashes.size() != descriptors.size()) {
            invalid("raw ASC hash entries do not match the saved frame count");
        }
        for (std::size_t index = 0U; index < descriptors.size(); ++index) {
            const Json& entry = raw_hashes.at(index);
            if (!nearly_equal(
                    required_finite_number(required_member(entry, "time_s", "raw ASC hashes"),
                                           "raw ASC timestamp"),
                    descriptors[index].time_s, 0.0, 0.0)) {
                invalid("raw ASC timestamps do not match saved frame timestamps");
            }
            for (const std::string_view key : {"h", "hUx", "hUy"}) {
                const std::string digest =
                    required_string(required_member(entry, key, "raw ASC hashes"), key);
                if (!cubey::asset::is_sha256_hex(digest)) {
                    invalid("raw ASC provenance contains an invalid SHA-256 digest");
                }
            }
        }
    }

    [[nodiscard]] std::shared_ptr<const Fluid25DRecordedFrame> load_frame(std::size_t index) const {
        const auto found = cache.find(index);
        if (found != cache.end()) {
            recency.remove(index);
            recency.push_front(index);
            return found->second;
        }
        if (index >= descriptors.size()) {
            throw std::out_of_range("fluid 2.5D recording frame index is out of range");
        }
        const FrameDescriptor& descriptor = descriptors[index];
        const std::filesystem::path path = safe_file_path(root, descriptor.path);
        const std::size_t cell_count = static_cast<std::size_t>(width) * height;
        const std::size_t byte_count = cell_count * 3U * sizeof(float);
        const std::vector<std::uint8_t> bytes = read_file(path, byte_count, kMaximumFrameBytes);
        if (cubey::asset::sha256_hex(std::as_bytes(std::span{bytes})) != descriptor.sha256) {
            invalid("frame SHA-256 does not match its payload");
        }

        const std::span<const std::uint8_t> payload(bytes);
        const std::size_t plane_bytes = cell_count * sizeof(float);
        const std::vector<float> depth = decode_float32_le(payload.first(plane_bytes), cell_count);
        const std::vector<float> qx =
            decode_float32_le(payload.subspan(plane_bytes, plane_bytes), cell_count);
        const std::vector<float> qz =
            decode_float32_le(payload.subspan(plane_bytes * 2U, plane_bytes), cell_count);
        auto result = std::make_shared<Fluid25DRecordedFrame>();
        result->time_s = descriptor.time_s;
        result->published_unix_s = descriptor.published_unix_s;
        result->depth_m = depth;
        result->momentum.resize(cell_count);
        result->velocity.resize(cell_count);
        result->stats.max_depth_m = 0.0F;
        result->stats.max_speed_m_per_s = 0.0F;
        result->stats.dry_nonzero_momentum_cells = 0U;
        const double cell_area = static_cast<double>(cell_size_m) * cell_size_m;
        double water_volume = 0.0;
        for (std::size_t cell = 0U; cell < cell_count; ++cell) {
            const float h = depth[cell];
            const float momentum_x = qx[cell];
            const float momentum_z = qz[cell];
            if (h < 0.0F) {
                invalid("frame contains negative water depth");
            }
            if (h == 0.0F && (momentum_x != 0.0F || momentum_z != 0.0F)) {
                invalid("frame has zero depth with nonzero momentum");
            }
            result->momentum[cell] = {momentum_x, momentum_z};
            result->stats.max_depth_m = std::max(result->stats.max_depth_m, h);
            water_volume += static_cast<double>(h) * cell_area;
            if (h == 0.0F) {
                result->velocity[cell] = {};
                continue;
            }
            const double velocity_x = static_cast<double>(momentum_x) / h;
            const double velocity_z = static_cast<double>(momentum_z) / h;
            const double speed = std::hypot(velocity_x, velocity_z);
            const double float_limit = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(velocity_x) || !std::isfinite(velocity_z) || !std::isfinite(speed) ||
                std::abs(velocity_x) > float_limit || std::abs(velocity_z) > float_limit ||
                speed > float_limit) {
                invalid("derived velocity is outside float32 representation");
            }
            result->velocity[cell] = {static_cast<float>(velocity_x),
                                      static_cast<float>(velocity_z)};
            result->stats.max_speed_m_per_s =
                std::max(result->stats.max_speed_m_per_s, static_cast<float>(speed));
        }
        if (!std::isfinite(water_volume)) {
            invalid("frame water volume is not finite");
        }
        result->stats.water_volume_m3 = water_volume;
        if (!nearly_equal(water_volume, descriptor.declared_stats.water_volume_m3, 1.0e-6,
                          1.0e-6) ||
            !nearly_equal(result->stats.max_depth_m, descriptor.declared_stats.max_depth_m, 1.0e-6,
                          1.0e-6) ||
            !nearly_equal(result->stats.max_speed_m_per_s,
                          descriptor.declared_stats.max_speed_m_per_s, 1.0e-5, 1.0e-6) ||
            result->stats.dry_nonzero_momentum_cells !=
                descriptor.declared_stats.dry_nonzero_momentum_cells) {
            invalid("frame statistics do not match the saved fields");
        }

        const std::shared_ptr<const Fluid25DRecordedFrame> immutable = result;
        recency.push_front(index);
        cache.emplace(index, immutable);
        while (cache.size() > kMaximumCachedFrames) {
            const std::size_t evicted = recency.back();
            recency.pop_back();
            cache.erase(evicted);
        }
        return immutable;
    }
};

Fluid25DRecording::Fluid25DRecording(const std::filesystem::path& recording_path, bool live_stream)
    : impl_(std::make_unique<Impl>(recording_path, live_stream)) {}

Fluid25DRecording::~Fluid25DRecording() = default;
Fluid25DRecording::Fluid25DRecording(Fluid25DRecording&&) noexcept = default;
Fluid25DRecording& Fluid25DRecording::operator=(Fluid25DRecording&&) noexcept = default;

std::uint32_t Fluid25DRecording::grid_width() const noexcept {
    return impl_->width;
}
std::uint32_t Fluid25DRecording::grid_height() const noexcept {
    return impl_->height;
}
float Fluid25DRecording::cell_size_m() const noexcept {
    return impl_->cell_size_m;
}
std::span<const float> Fluid25DRecording::bed() const noexcept {
    return impl_->bed;
}
std::span<const float> Fluid25DRecording::source_bed() const noexcept {
    return impl_->source_bed;
}
std::span<const double> Fluid25DRecording::times_s() const noexcept {
    return impl_->times;
}
std::size_t Fluid25DRecording::frame_count() const noexcept {
    return impl_->descriptors.size();
}

std::size_t Fluid25DRecording::frame_index_at(double time_s) const {
    if (!std::isfinite(time_s)) {
        throw std::invalid_argument("fluid 2.5D recording time must be finite");
    }
    const auto after = std::upper_bound(impl_->times.begin(), impl_->times.end(), time_s);
    if (after == impl_->times.begin()) {
        return 0U;
    }
    return static_cast<std::size_t>(std::distance(impl_->times.begin(), after) - 1);
}

std::shared_ptr<const Fluid25DRecordedFrame> Fluid25DRecording::frame(std::size_t index) const {
    return impl_->load_frame(index);
}

std::size_t Fluid25DRecording::cached_frame_count() const noexcept {
    return impl_->cache.size();
}

double Fluid25DRecording::rain_rate_mm_per_hour() const noexcept {
    return impl_->rain_rate_mm_per_hour;
}

std::span<const Fluid25DRecordedRainKnot> Fluid25DRecording::rainfall_history() const noexcept {
    return impl_->rainfall_history;
}

Fluid25DRecordedRainStatus Fluid25DRecording::rain_status_at(double time_s) const {
    if (!std::isfinite(time_s)) {
        throw std::invalid_argument("fluid 2.5D recorded-rain time must be finite");
    }
    const auto& history = impl_->rainfall_history;
    const double clamped_time = std::clamp(time_s, history.front().time_s, history.back().time_s);
    const auto after = std::upper_bound(
        history.begin(), history.end(), clamped_time,
        [](double value, const Fluid25DRecordedRainKnot& knot) { return value < knot.time_s; });
    const std::size_t left_index =
        after == history.begin()
            ? 0U
            : static_cast<std::size_t>(std::distance(history.begin(), after) - 1);
    const std::size_t right_index = std::min(left_index + 1U, history.size() - 1U);
    const auto& left = history[left_index];
    const auto& right = history[right_index];
    const double segment_length = right.time_s - left.time_s;
    const double fraction =
        segment_length > 0.0 ? (clamped_time - left.time_s) / segment_length : 0.0;
    const double rate_m_per_s =
        left.rate_m_per_s + (right.rate_m_per_s - left.rate_m_per_s) * fraction;

    double cumulative_depth_m = 0.0;
    for (std::size_t index = 0U; index + 1U < history.size(); ++index) {
        const auto& start = history[index];
        const auto& end = history[index + 1U];
        if (clamped_time <= start.time_s) {
            break;
        }
        const double segment_end_time = std::min(clamped_time, end.time_s);
        const double elapsed = segment_end_time - start.time_s;
        const double fraction_end = elapsed / (end.time_s - start.time_s);
        const double ending_rate =
            start.rate_m_per_s + (end.rate_m_per_s - start.rate_m_per_s) * fraction_end;
        cumulative_depth_m += (start.rate_m_per_s + ending_rate) * 0.5 * elapsed;
        if (clamped_time <= end.time_s) {
            break;
        }
    }

    Fluid25DRecordedRainPhase phase = Fluid25DRecordedRainPhase::On;
    if (rate_m_per_s <= 0.0) {
        phase = Fluid25DRecordedRainPhase::Off;
    } else {
        const std::size_t phase_left_index =
            after == history.end() && history.size() > 1U ? history.size() - 2U : left_index;
        if (history[phase_left_index + 1U].rate_m_per_s < history[phase_left_index].rate_m_per_s) {
            phase = Fluid25DRecordedRainPhase::Tapering;
        }
    }
    return {clamped_time, rate_m_per_s * 3'600'000.0, cumulative_depth_m * 1000.0, phase};
}

const char* fluid_25d_recorded_rain_phase_name(Fluid25DRecordedRainPhase phase) noexcept {
    switch (phase) {
    case Fluid25DRecordedRainPhase::On:
        return "On";
    case Fluid25DRecordedRainPhase::Tapering:
        return "Tapering";
    case Fluid25DRecordedRainPhase::Off:
        return "Off";
    }
    return "Unknown";
}

const nlohmann::json& Fluid25DRecording::protocol() const noexcept {
    return impl_->protocol_json;
}
const nlohmann::json& Fluid25DRecording::provenance() const noexcept {
    return impl_->provenance_json;
}
const std::filesystem::path& Fluid25DRecording::manifest_path() const noexcept {
    return impl_->manifest;
}

bool Fluid25DRecording::is_live_stream() const noexcept {
    return impl_->live_stream;
}
const std::string& Fluid25DRecording::session_id() const noexcept {
    return impl_->session;
}
std::uint64_t Fluid25DRecording::revision() const noexcept {
    return impl_->stream_revision;
}
const std::string& Fluid25DRecording::producer_state() const noexcept {
    return impl_->state;
}
const std::string& Fluid25DRecording::producer_message() const noexcept {
    return impl_->message;
}
double Fluid25DRecording::latest_native_time_s() const noexcept {
    return impl_->native_time_s;
}
bool Fluid25DRecording::native_running() const noexcept {
    return impl_->native_running;
}

void Fluid25DRecording::validate_successor_of(const Fluid25DRecording& previous) const {
    const auto& old = *previous.impl_;
    const auto& next = *impl_;
    if (!old.live_stream || !next.live_stream || old.root != next.root ||
        old.session != next.session || next.stream_revision < old.stream_revision ||
        next.times.size() < old.times.size() || next.native_time_s < old.native_time_s ||
        (old.state != "running" && next.state != old.state))
        invalid("live session changed identity, regressed, or resumed after termination");
    if (next.stream_revision == old.stream_revision && next.document_json != old.document_json)
        invalid("stream changed without advancing its revision");
    for (const char* key :
         {"encoding", "fields", "field_layout", "grid", "bed", "source_bed", "protocol"})
        if (next.document_json.at(key) != old.document_json.at(key))
            invalid("stream mutated immutable geometry/protocol");
    for (const char* key : {"terrain_source_identity", "precision"})
        if (next.provenance_json.at(key) != old.provenance_json.at(key))
            invalid("stream mutated immutable provenance");
    for (const char* key :
         {"case_spec", "case_protocol", "dem_asc", "native_numerical_bed", "pre_solver_audit"})
        if (next.provenance_json.at("source_sha256").at(key) !=
            old.provenance_json.at("source_sha256").at(key))
            invalid("stream mutated input audit");
    if (next.provenance_json.value("rainfall_history_proof", Json{}) !=
        old.provenance_json.value("rainfall_history_proof", Json{}))
        invalid("stream mutated rainfall proof");
    for (std::size_t index = 0U; index < old.times.size(); ++index)
        if (next.document_json.at("frames").at(index) != old.document_json.at("frames").at(index) ||
            next.provenance_json.at("raw_asc_sha256").at(index) !=
                old.provenance_json.at("raw_asc_sha256").at(index))
            invalid("stream mutated an already published frame");
}

Fluid25DRecordingPlayback::Fluid25DRecordingPlayback(std::span<const double> saved_times_s)
    : times_s_(saved_times_s.begin(), saved_times_s.end()) {
    if (times_s_.empty() || times_s_.front() != 0.0) {
        throw std::invalid_argument("recording playback requires saved times starting at zero");
    }
    for (std::size_t index = 0U; index < times_s_.size(); ++index) {
        if (!std::isfinite(times_s_[index]) || times_s_[index] < 0.0 ||
            (index > 0U && times_s_[index] <= times_s_[index - 1U])) {
            throw std::invalid_argument(
                "recording playback times must be finite and strictly increasing");
        }
    }
    time_s_ = times_s_.front();
}

void Fluid25DRecordingPlayback::advance(double wall_delta_s) {
    if (!std::isfinite(wall_delta_s) || wall_delta_s < 0.0) {
        throw std::invalid_argument("recording playback delta must be finite and nonnegative");
    }
    if (paused_ || ended_ || wall_delta_s == 0.0) {
        return;
    }
    const double remaining = times_s_.back() - time_s_;
    const double scaled_delta = wall_delta_s * rate_;
    if (!std::isfinite(scaled_delta) || scaled_delta >= remaining) {
        time_s_ = times_s_.back();
        ended_ = true;
        paused_ = true;
        return;
    }
    time_s_ += scaled_delta;
    if (time_s_ >= times_s_.back()) {
        time_s_ = times_s_.back();
        ended_ = true;
        paused_ = true;
    }
}

void Fluid25DRecordingPlayback::seek(double time_s) {
    if (!std::isfinite(time_s)) {
        throw std::invalid_argument("recording playback seek time must be finite");
    }
    time_s_ = std::clamp(time_s, times_s_.front(), times_s_.back());
    paused_ = true;
    ended_ = time_s_ == times_s_.back();
}

void Fluid25DRecordingPlayback::restart() noexcept {
    time_s_ = times_s_.front();
    paused_ = true;
    ended_ = false;
}

void Fluid25DRecordingPlayback::previous() {
    const auto next_or_exact = std::lower_bound(times_s_.begin(), times_s_.end(), time_s_);
    if (next_or_exact == times_s_.begin()) {
        seek(times_s_.front());
        return;
    }
    std::size_t target = static_cast<std::size_t>(std::distance(times_s_.begin(), next_or_exact));
    --target;
    seek(times_s_[target]);
}

void Fluid25DRecordingPlayback::next() {
    const auto next_saved = std::upper_bound(times_s_.begin(), times_s_.end(), time_s_);
    seek(next_saved == times_s_.end() ? times_s_.back() : *next_saved);
}

void Fluid25DRecordingPlayback::set_paused(bool paused) noexcept {
    paused_ = paused;
}

void Fluid25DRecordingPlayback::set_rate(double rate) {
    if (!std::isfinite(rate) || rate < kMinimumPlaybackRate || rate > kMaximumPlaybackRate) {
        throw std::invalid_argument("recording playback rate must be within 0.125..300");
    }
    rate_ = rate;
}

void Fluid25DRecordingPlayback::extend(std::span<const double> saved_times_s) {
    Fluid25DRecordingPlayback checked(saved_times_s);
    if (checked.times_s_.size() < times_s_.size() ||
        !std::equal(times_s_.begin(), times_s_.end(), checked.times_s_.begin()))
        throw std::invalid_argument("live playback cannot mutate or shrink its saved prefix");
    if (checked.times_s_.size() > times_s_.size())
        ended_ = false;
    times_s_ = std::move(checked.times_s_);
}

std::size_t Fluid25DRecordingPlayback::frame_index() const noexcept {
    const auto after = std::upper_bound(times_s_.begin(), times_s_.end(), time_s_);
    if (after == times_s_.begin()) {
        return 0U;
    }
    return static_cast<std::size_t>(std::distance(times_s_.begin(), after) - 1);
}

double Fluid25DRecordingPlayback::time_s() const noexcept {
    return time_s_;
}
double Fluid25DRecordingPlayback::rate() const noexcept {
    return rate_;
}
bool Fluid25DRecordingPlayback::paused() const noexcept {
    return paused_;
}
bool Fluid25DRecordingPlayback::ended() const noexcept {
    return ended_;
}
std::span<const double> Fluid25DRecordingPlayback::saved_times_s() const noexcept {
    return times_s_;
}

} // namespace cubey::projects::fluid::fluid_25d
