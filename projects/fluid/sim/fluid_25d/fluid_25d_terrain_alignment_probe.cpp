#include "fluid_25d_recording.h"
#include "fluid_25d_terrain_alignment.h"

#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_raster_height_source.h>
#include <nlohmann/json.hpp>

#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {
static_assert(std::endian::native == std::endian::little,
              "diagnostic f32 output requires little-endian host");
namespace probe = cubey::projects::fluid::fluid_25d::alignment;
using Json = nlohmann::json;

std::string digest(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() < 0) {
        throw std::runtime_error("cannot hash input " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("short input read");
    }
    return cubey::asset::sha256_hex(bytes);
}

class LodZero final : public cubey::asset::TerrainHeightSource {
  public:
    explicit LodZero(const cubey::asset::TerrainHeightSource& source) : source_(source) {}
    cubey::asset::TerrainHeightSourceMetadata metadata() const noexcept override {
        return source_.metadata();
    }
    float sample_height(const cubey::asset::TerrainQuery& query) const override {
        return source_.sample_height({.world_xz = query.world_xz, .footprint_m = 0});
    }

  private:
    const cubey::asset::TerrainHeightSource& source_;
};

struct Summary {
    std::vector<double> bed_error{};
    std::uint64_t buried = 0;
    std::uint64_t large_error = 0;

    void add(double ground, double bed, double depth) {
        bed_error.push_back(ground - bed);
        buried += ground > bed + depth + 0.02 ? 1U : 0U;
        large_error += std::abs(ground - bed) > std::max(0.1, 0.25 * depth) ? 1U : 0U;
    }

    Json json() const {
        if (bed_error.empty()) {
            return {{"samples", 0},
                    {"buried_fraction", nullptr},
                    {"large_bed_mismatch_fraction", nullptr}};
        }
        auto sorted = bed_error;
        std::ranges::sort(sorted);
        const auto quantile = [](const std::vector<double>& values, double q) {
            const auto index =
                static_cast<std::size_t>(std::ceil(q * static_cast<double>(values.size()))) - 1U;
            return values[std::min(index, values.size() - 1U)];
        };
        std::vector<double> absolute;
        absolute.reserve(sorted.size());
        for (double value : sorted) {
            absolute.push_back(std::abs(value));
        }
        std::ranges::sort(absolute);
        const double count = static_cast<double>(sorted.size());
        return {{"samples", sorted.size()},
                {"buried_samples", buried},
                {"buried_fraction", static_cast<double>(buried) / count},
                {"large_bed_mismatch_samples", large_error},
                {"large_bed_mismatch_fraction", static_cast<double>(large_error) / count},
                {"ground_minus_reference_bed_min_m", sorted.front()},
                {"ground_minus_reference_bed_p50_m", quantile(sorted, 0.5)},
                {"ground_minus_reference_bed_max_m", sorted.back()},
                {"absolute_bed_error_p95_m", quantile(absolute, 0.95)}};
    }
};

void write_field(const std::filesystem::path& path, std::span<const float> values) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size_bytes()));
    if (!output) {
        throw std::runtime_error("failed diagnostic field output");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 6) {
            throw std::runtime_error(
                "usage: probe SOURCE RECORDING NEW_OUTPUT STRIDE runtime|lod0");
        }
        const std::filesystem::path source_path = argv[1], recording_path = argv[2],
                                    output = argv[3];
        const std::string stride_text = argv[4], sampling = argv[5];
        if ((stride_text != "1" && stride_text != "3") ||
            (sampling != "runtime" && sampling != "lod0") || std::filesystem::exists(output)) {
            throw std::runtime_error("invalid probe mode or output already exists");
        }
        const std::string recording_hash = digest(recording_path);
        const std::string source_hash = digest(source_path);
        cubey::projects::fluid::fluid_25d::Fluid25DRecording recording(recording_path);
        const auto& identity = recording.protocol().at("terrain_source_identity");
        cubey::asset::TerrainRasterHeightSource source(source_path);
        if (identity.at("manifest_sha256") != source_hash ||
            identity.at("elevation_sha256") != source.provenance().elevation_sha256 ||
            identity.at("terrain_modified") != false) {
            throw std::runtime_error("captured water does not bind this unchanged source");
        }
        const auto& target = identity.at("target_grid");
        const std::uint32_t width = recording.grid_width(), height = recording.grid_height();
        if (recording.source_bed().size() != static_cast<std::size_t>(width) * height) {
            throw std::runtime_error("alignment requires a complete bound sampled source bed");
        }
        const float spacing = recording.cell_size_m();
        const float center_x = target.at("center_xz_m").at(0).get<float>();
        const float center_z = target.at("center_xz_m").at(1).get<float>();
        if (target.at("width") != width || target.at("height") != height ||
            target.at("spacing_m") != spacing ||
            target.at("orientation") != "row-major-x-fastest-z-rows" ||
            target.at("sampling") != "bilinear-lod0") {
            throw std::runtime_error("captured grid semantics do not match reader");
        }
        const float origin_x = target.at("sample_origin_xz_m").at(0).get<float>();
        const float origin_z = target.at("sample_origin_xz_m").at(1).get<float>();
        if (std::abs(origin_x + 0.5F * static_cast<float>(width - 1U) * spacing - center_x) >
                0.001F ||
            std::abs(origin_z + 0.5F * static_cast<float>(height - 1U) * spacing - center_z) >
                0.001F) {
            throw std::runtime_error("captured cell-center origin is inconsistent");
        }
        const float radius = 0.5F * static_cast<float>(std::min(width, height)) * spacing;
        if (!source.contains_disk({center_x, center_z}, radius)) {
            throw std::runtime_error("matched ground disk does not fit source");
        }
        const auto frame = recording.frame(recording.frame_count() - 1U);
        cubey::terrain::TerrainBackdropProductRequest request{
            .source_focus_xz = {center_x, center_z},
            .center_mode = cubey::terrain::TerrainBackdropCenterMode::Continuous,
            .center_sampling = cubey::terrain::TerrainBackdropCenterSampling::SeamMatched,
            .render_stride = static_cast<std::uint32_t>(std::stoi(stride_text)),
            .outer_radius_m = radius,
        };
        const LodZero lod_zero(source);
        const cubey::asset::TerrainHeightSource& selected =
            sampling == "lod0" ? static_cast<const cubey::asset::TerrainHeightSource&>(lod_zero)
                               : source;
        const auto product = cubey::terrain::make_terrain_backdrop_product(request, selected);
        probe::TriangleSampler triangles(radius);
        if (product.center) {
            triangles.add(*product.center);
        }
        for (const auto& sector : product.sectors) {
            triangles.add(sector);
        }
        const auto count = static_cast<std::size_t>(width) * height;
        std::vector<float> ground_map(count, std::numeric_limits<float>::quiet_NaN());
        std::vector<float> bed_map(count), depth_map(count);
        Summary wet, readable, shallow, deep;
        std::uint64_t expected_inside = 0, covered_inside = 0;
        double source_bed_max_error = 0.0;
        for (std::uint32_t row = 0; row < height; ++row) {
            for (std::uint32_t column = 0; column < width; ++column) {
                const auto index = static_cast<std::size_t>(row) * width + column;
                const float x = origin_x + static_cast<float>(column) * spacing - center_x;
                const float z = origin_z + static_cast<float>(row) * spacing - center_z;
                const float bed =
                    probe::bspline(recording.bed(), width, height, static_cast<float>(column),
                                   static_cast<float>(row));
                const float depth =
                    probe::bspline(frame->depth_m, width, height, static_cast<float>(column),
                                   static_cast<float>(row));
                bed_map[index] = bed;
                depth_map[index] = depth;
                const double sampled =
                    source.sample_height({.world_xz = {center_x + x, center_z + z}});
                source_bed_max_error = std::max(source_bed_max_error,
                                                std::abs(sampled - recording.source_bed()[index]));
                if (std::hypot(x, z) >= radius - 0.001F) {
                    continue;
                }
                ++expected_inside;
                const auto ground = triangles.height(x, z);
                if (!ground) {
                    continue;
                }
                ++covered_inside;
                ground_map[index] = static_cast<float>(*ground);
                if (depth > 0.0001F) {
                    wet.add(*ground, bed, depth);
                }
                if (depth >= 0.05F) {
                    readable.add(*ground, bed, depth);
                    (depth < 2.0F ? shallow : deep).add(*ground, bed, depth);
                }
            }
        }
        const Json readability = readable.json();
        const double coverage =
            static_cast<double>(covered_inside) / static_cast<double>(expected_inside);
        const bool rejects = readable.bed_error.empty() || coverage < 0.9999 ||
                             source_bed_max_error > 0.001 ||
                             readability.at("buried_fraction").get<double>() > 0.01 ||
                             readability.at("large_bed_mismatch_fraction").get<double>() > 0.05;
        if (digest(recording_path) != recording_hash || digest(source_path) != source_hash) {
            throw std::runtime_error("input manifest changed during probe");
        }
        if (!std::filesystem::create_directory(output)) {
            throw std::runtime_error("cannot create fresh diagnostic output");
        }
        write_field(output / "ground.f32", ground_map);
        write_field(output / "reference-bed.f32", bed_map);
        write_field(output / "reference-depth.f32", depth_map);
        Json result{{"schema", "cubey.terrain.captured-water.alignment.v1"},
                    {"probe_sha256", digest(argv[0])},
                    {"recording_sha256", recording_hash},
                    {"source_sha256", source_hash},
                    {"source_elevation_sha256", source.provenance().elevation_sha256},
                    {"saved_time_s", frame->time_s},
                    {"width", width},
                    {"height", height},
                    {"spacing_m", spacing},
                    {"source_focus_xz_m", {center_x, center_z}},
                    {"render_stride", request.render_stride},
                    {"source_footprint", sampling},
                    {"outer_radius_m", radius},
                    {"triangles", product.diagnostics.render_triangle_count},
                    {"geometry_hash", product.diagnostics.geometry_hash},
                    {"inside_disk_samples", expected_inside},
                    {"covered_inside_samples", covered_inside},
                    {"inside_disk_coverage_fraction", coverage},
                    {"lod0_vs_saved_source_bed_max_error_m", source_bed_max_error},
                    {"wet_depth_above_0_0001m", wet.json()},
                    {"depth_at_least_0_05m", readability},
                    {"depth_0_05_to_2m", shallow.json()},
                    {"depth_at_least_2m", deep.json()},
                    {"early_geometry_rejection", rejects},
                    {"evidence", "CPU sampling of exact shared mesh triangles at native water mesh "
                                 "vertices; not a rendered acceptance"},
                    {"numerical_fields_modified", false},
                    {"new_solver_run", false}};
        result["diagnostic_fields"] = Json::object();
        for (const auto* name : {"ground.f32", "reference-bed.f32", "reference-depth.f32"}) {
            result["diagnostic_fields"][name] = digest(output / name);
        }
        std::ofstream receipt(output / "alignment.json");
        receipt << result.dump(2) << '\n';
        if (!receipt) {
            throw std::runtime_error("failed diagnostic receipt output");
        }
        std::cout << result.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain captured water alignment: " << error.what() << '\n';
        return 1;
    }
}
