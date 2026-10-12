#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_water_capture.h>

#include <nlohmann/json.hpp>

#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>

namespace cubey::asset {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kMaximumCells = 4U * 1024U * 1024U;
constexpr std::uint64_t kMaximumManifestBytes = 1024U * 1024U;
constexpr auto kSchema = "cubey.terrain.water-capture.v1";

[[noreturn]] void invalid(const char* message) {
    throw std::runtime_error(std::string("terrain water capture: ") + message);
}

std::uint32_t dimension(const Json& value) {
    if (!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned() &&
                                       value.get<std::int64_t>() < 0)) {
        invalid("dimensions must be nonnegative integers");
    }
    const auto n = value.get<std::uint64_t>();
    if (n < 2 || n > 16'384U)
        invalid("dimension outside supported range");
    return static_cast<std::uint32_t>(n);
}

void host_encoding() {
    if constexpr (std::endian::native != std::endian::little || sizeof(float) != 4 ||
                  !std::numeric_limits<float>::is_iec559) {
        invalid("float32 little-endian host required");
    }
}

std::filesystem::path payload_path(const std::filesystem::path& root, const Json& entry) {
    const std::filesystem::path relative = entry.at("path").get<std::string>();
    if (relative.empty() || relative.is_absolute())
        invalid("payload path must be relative");
    auto current = root;
    for (const auto& component : relative) {
        if (component == ".." || component == ".")
            invalid("unsafe payload path component");
        current /= component;
        if (std::filesystem::is_symlink(current))
            invalid("symlink payload path rejected");
    }
    if (!std::filesystem::is_regular_file(current))
        invalid("payload is not a regular file");
    return current;
}

std::vector<float> read_plane(const std::filesystem::path& root, const Json& entry,
                              std::size_t count) {
    host_encoding();
    const auto path = payload_path(root, entry);
    const auto bytes = count * sizeof(float);
    if (!entry.at("byte_count").is_number_integer() || entry.at("byte_count") != bytes ||
        std::filesystem::file_size(path) != bytes ||
        !is_sha256_hex(entry.at("sha256").get<std::string>()))
        invalid("invalid payload size or digest");
    std::vector<float> result(count);
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(bytes))) {
        invalid("short payload read");
    }
    if (sha256_hex(std::as_bytes(std::span{result})) != entry.at("sha256").get<std::string>())
        invalid("payload digest mismatch");
    return result;
}

Json write_plane(const std::filesystem::path& root, const char* name,
                 std::span<const float> values) {
    host_encoding();
    std::ofstream output(root / name, std::ios::binary);
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size_bytes()));
    if (!output)
        invalid("failed payload write");
    return {{"path", name},
            {"byte_count", values.size_bytes()},
            {"sha256", sha256_hex(std::as_bytes(values))}};
}
} // namespace

math::Vec2 TerrainWaterCaptureGrid::center_xz_m() const noexcept {
    return sample_origin_xz_m +
           math::Vec2{static_cast<float>(width) - 1.0F, static_cast<float>(height) - 1.0F} *
               (cell_size_m * 0.5F);
}

void validate_terrain_water_capture(const TerrainWaterCapture& capture) {
    const auto& g = capture.grid;
    const auto count = static_cast<std::size_t>(g.width) * g.height;
    const auto corner = g.sample_origin_xz_m + math::Vec2{static_cast<float>(g.width) - 1.0F,
                                                          static_cast<float>(g.height) - 1.0F} *
                                                   g.cell_size_m;
    if (g.width < 2 || g.height < 2 || g.width > 16'384U || g.height > 16'384U ||
        count > kMaximumCells || !std::isfinite(g.cell_size_m) || g.cell_size_m <= 0 ||
        !std::isfinite(g.sample_origin_xz_m.x) || !std::isfinite(g.sample_origin_xz_m.y) ||
        !std::isfinite(g.center_xz_m().x) || !std::isfinite(g.center_xz_m().y) ||
        !std::isfinite(corner.x) || !std::isfinite(corner.y))
        invalid("invalid grid");
    if (!is_sha256_hex(capture.source_elevation_sha256) ||
        !is_sha256_hex(capture.recording_sha256) || !is_sha256_hex(capture.frame_sha256) ||
        !std::isfinite(capture.capture_time_s) || capture.capture_time_s < 0 ||
        !std::isfinite(capture.rain_mm_per_hour) || capture.rain_mm_per_hour <= 0)
        invalid("invalid source or rain-fed provenance");
    if (capture.bed_m.size() != count || capture.depth_m.size() != count ||
        capture.velocity_xz_m_per_s.size() != count)
        invalid("field count mismatch");
    for (std::size_t i = 0; i < count; ++i) {
        const auto v = capture.velocity_xz_m_per_s[i];
        if (!std::isfinite(capture.bed_m[i]) || !std::isfinite(capture.depth_m[i]) ||
            !std::isfinite(capture.bed_m[i] + capture.depth_m[i]) || capture.depth_m[i] < 0 ||
            !std::isfinite(v.x) || !std::isfinite(v.y) ||
            std::hypot(static_cast<double>(v.x), static_cast<double>(v.y)) >
                std::numeric_limits<float>::max() ||
            (capture.depth_m[i] == 0 && (v.x != 0 || v.y != 0)))
            invalid("invalid bed, depth or dry-cell velocity");
    }
}

void validate_terrain_water_capture_source(const TerrainWaterCapture& capture,
                                           const TerrainRasterHeightSource& source) {
    validate_terrain_water_capture(capture);
    if (capture.source_elevation_sha256 != source.provenance().elevation_sha256)
        invalid("source identity mismatch");
    const auto bounds = source.bounds();
    const auto lo = capture.grid.sample_origin_xz_m;
    const auto hi = lo + math::Vec2{static_cast<float>(capture.grid.width - 1U),
                                    static_cast<float>(capture.grid.height - 1U)} *
                             capture.grid.cell_size_m;
    if (lo.x < bounds.minimum_xz.x || lo.y < bounds.minimum_xz.y || hi.x > bounds.maximum_xz.x ||
        hi.y > bounds.maximum_xz.y)
        invalid("capture footprint outside source");
    // The retained native text bed is rounded to centimetres. Validate its
    // placement against unfiltered source samples without replacing that bed.
    for (std::uint32_t z = 0; z < capture.grid.height; ++z) {
        for (std::uint32_t x = 0; x < capture.grid.width; ++x) {
            const auto point = lo + math::Vec2{static_cast<float>(x), static_cast<float>(z)} *
                                        capture.grid.cell_size_m;
            const auto sampled = source.sample_height({.world_xz = point});
            if (std::abs(static_cast<double>(sampled) -
                         capture.bed_m[static_cast<std::size_t>(z) * capture.grid.width + x]) >
                0.01) {
                invalid("numerical bed does not match bound source placement");
            }
        }
    }
}

TerrainWaterCapture load_terrain_water_capture(const std::filesystem::path& input_path) {
    const auto path =
        std::filesystem::is_directory(input_path) ? input_path / "water-capture.json" : input_path;
    if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path) ||
        std::filesystem::file_size(path) > kMaximumManifestBytes)
        invalid("unsafe or oversized manifest");
    std::ifstream input(path);
    try {
        const auto doc = Json::parse(input);
        if (doc.at("schema") != kSchema || doc.at("encoding") != "float32-little-endian" ||
            doc.at("state").at("kind") != "rain-fed")
            invalid("unsupported schema, encoding or state");
        const auto& grid = doc.at("grid");
        if (grid.at("sample_location") != "cell-center" ||
            grid.at("storage_order") != "row-major" ||
            grid.at("row_direction") != "world-z-positive" ||
            grid.at("axes") != Json::array({"world-x", "world-z"})) {
            invalid("unsupported grid convention");
        }
        if (!grid.at("sample_origin_xz_m").is_array() ||
            grid.at("sample_origin_xz_m").size() != 2U) {
            invalid("sample origin must contain exactly X and Z");
        }
        TerrainWaterCapture result;
        result.grid = {dimension(grid.at("width")),
                       dimension(grid.at("height")),
                       grid.at("cell_size_m").get<float>(),
                       {grid.at("sample_origin_xz_m").at(0).get<float>(),
                        grid.at("sample_origin_xz_m").at(1).get<float>()}};
        const auto count = static_cast<std::size_t>(result.grid.width) * result.grid.height;
        if (count > kMaximumCells)
            invalid("grid cell limit exceeded");
        result.source_elevation_sha256 = doc.at("source_elevation_sha256").get<std::string>();
        result.capture_time_s = doc.at("state").at("physical_time_s").get<double>();
        result.rain_mm_per_hour = doc.at("state").at("rain_mm_per_hour").get<double>();
        result.recording_sha256 = doc.at("provenance").at("recording_sha256").get<std::string>();
        result.frame_sha256 = doc.at("provenance").at("frame_sha256").get<std::string>();
        const auto& fields = doc.at("fields");
        if (fields.at("bed_m").at("layout") != "scalar" ||
            fields.at("depth_m").at("layout") != "scalar" ||
            fields.at("velocity_xz_m_per_s").at("layout") != "planar-xz" ||
            fields.at("bed_m").at("unit") != "m" || fields.at("depth_m").at("unit") != "m" ||
            fields.at("velocity_xz_m_per_s").at("unit") != "m/s")
            invalid("unsupported field layout");
        const auto root = path.parent_path();
        result.bed_m = read_plane(root, fields.at("bed_m"), count);
        result.depth_m = read_plane(root, fields.at("depth_m"), count);
        const auto velocity = read_plane(root, fields.at("velocity_xz_m_per_s"), count * 2U);
        result.velocity_xz_m_per_s.resize(count);
        for (std::size_t i = 0; i < count; ++i)
            result.velocity_xz_m_per_s[i] = {velocity[i], velocity[count + i]};
        validate_terrain_water_capture(result);
        return result;
    } catch (const Json::exception& error) {
        throw std::runtime_error(std::string("terrain water capture: invalid manifest: ") +
                                 error.what());
    }
}

void write_terrain_water_capture(const std::filesystem::path& directory,
                                 const TerrainWaterCapture& capture) {
    validate_terrain_water_capture(capture);
    if (std::filesystem::exists(directory))
        invalid("export directory already exists");
    if (!directory.parent_path().empty())
        std::filesystem::create_directories(directory.parent_path());
    if (!std::filesystem::create_directory(directory))
        invalid("cannot create export directory");
    const auto& g = capture.grid;
    Json fields{{"bed_m", write_plane(directory, "bed.f32", capture.bed_m)},
                {"depth_m", write_plane(directory, "depth.f32", capture.depth_m)}};
    std::vector<float> velocity(capture.velocity_xz_m_per_s.size() * 2U);
    for (std::size_t i = 0; i < capture.velocity_xz_m_per_s.size(); ++i) {
        velocity[i] = capture.velocity_xz_m_per_s[i].x;
        velocity[capture.velocity_xz_m_per_s.size() + i] = capture.velocity_xz_m_per_s[i].y;
    }
    fields["velocity_xz_m_per_s"] = write_plane(directory, "velocity.f32", velocity);
    fields["bed_m"]["layout"] = fields["depth_m"]["layout"] = "scalar";
    fields["velocity_xz_m_per_s"]["layout"] = "planar-xz";
    fields["bed_m"]["unit"] = fields["depth_m"]["unit"] = "m";
    fields["velocity_xz_m_per_s"]["unit"] = "m/s";
    const Json doc{{"schema", kSchema},
                   {"encoding", "float32-little-endian"},
                   {"grid",
                    {{"width", g.width},
                     {"height", g.height},
                     {"cell_size_m", g.cell_size_m},
                     {"sample_origin_xz_m", {g.sample_origin_xz_m.x, g.sample_origin_xz_m.y}},
                     {"sample_location", "cell-center"},
                     {"storage_order", "row-major"},
                     {"row_direction", "world-z-positive"},
                     {"axes", {"world-x", "world-z"}}}},
                   {"source_elevation_sha256", capture.source_elevation_sha256},
                   {"state",
                    {{"kind", "rain-fed"},
                     {"physical_time_s", capture.capture_time_s},
                     {"rain_mm_per_hour", capture.rain_mm_per_hour}}},
                   {"provenance",
                    {{"recording_sha256", capture.recording_sha256},
                     {"frame_sha256", capture.frame_sha256},
                     {"velocity_fields",
                      "caller supplied decoded source-world XZ velocities; zero when dry"}}},
                   {"fields", fields}};
    std::ofstream manifest(directory / "water-capture.json");
    manifest << doc.dump(2) << '\n';
    if (!manifest)
        invalid("manifest publication failed");
}
} // namespace cubey::asset
