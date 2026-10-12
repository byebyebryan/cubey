#include "fluid_25d_recording.h"
#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_water_capture.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <span>

namespace {
std::string digest(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0)
        throw std::runtime_error("missing export input");
    std::vector<std::byte> bytes(static_cast<std::size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("short export input");
    return cubey::asset::sha256_hex(bytes);
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("usage: export RECORDING SOURCE NEW_OUTPUT");
        using namespace cubey::projects::fluid::fluid_25d;
        Fluid25DRecording recording(argv[1]);
        const auto frame = recording.frame(recording.frame_count() - 1U);
        const auto rain = recording.rain_status_at(frame->time_s);
        if (rain.phase != Fluid25DRecordedRainPhase::On)
            throw std::runtime_error("export requires a rain-on state");
        const auto& identity = recording.protocol().at("terrain_source_identity");
        const auto& grid = identity.at("target_grid");
        if (identity.at("terrain_modified") != false || grid.at("sampling") != "bilinear-lod0" ||
            grid.at("orientation") != "row-major-x-fastest-z-rows" ||
            grid.at("width") != recording.grid_width() ||
            grid.at("height") != recording.grid_height() ||
            grid.at("spacing_m") != recording.cell_size_m())
            throw std::runtime_error("unsupported source grid binding");
        cubey::asset::TerrainWaterCapture output;
        output.grid = {recording.grid_width(),
                       recording.grid_height(),
                       recording.cell_size_m(),
                       {grid.at("sample_origin_xz_m").at(0).get<float>(),
                        grid.at("sample_origin_xz_m").at(1).get<float>()}};
        output.source_elevation_sha256 = identity.at("elevation_sha256").get<std::string>();
        output.capture_time_s = frame->time_s;
        output.rain_mm_per_hour = rain.rate_mm_per_hour;
        output.recording_sha256 = digest(recording.manifest_path());
        std::ifstream manifest_input(recording.manifest_path());
        const auto manifest = nlohmann::json::parse(manifest_input);
        output.frame_sha256 = manifest.at("frames").back().at("sha256").get<std::string>();
        output.bed_m.assign(recording.bed().begin(), recording.bed().end());
        output.depth_m = frame->depth_m;
        for (const auto v : frame->velocity)
            output.velocity_xz_m_per_s.push_back({v.x_m_per_s, v.y_m_per_s});
        cubey::asset::TerrainRasterHeightSource source(argv[2]);
        cubey::asset::validate_terrain_water_capture_source(output, source);
        cubey::asset::write_terrain_water_capture(argv[3], output);
        const auto restored = cubey::asset::load_terrain_water_capture(argv[3]);
        if (cubey::asset::sha256_hex(std::as_bytes(std::span{restored.bed_m})) !=
                cubey::asset::sha256_hex(std::as_bytes(std::span{output.bed_m})) ||
            cubey::asset::sha256_hex(std::as_bytes(std::span{restored.depth_m})) !=
                cubey::asset::sha256_hex(std::as_bytes(std::span{output.depth_m})))
            throw std::runtime_error("export roundtrip differs");
        for (std::size_t i = 0; i < restored.velocity_xz_m_per_s.size(); ++i)
            if (restored.velocity_xz_m_per_s[i] != output.velocity_xz_m_per_s[i])
                throw std::runtime_error("velocity roundtrip differs");
        std::cout << "terrain_water_capture_export: PASS bit-exact bed/depth and checked decoded "
                     "velocity; cells="
                  << output.bed_m.size() << " physical_time_s=" << output.capture_time_s
                  << " hydraulic_dispatches=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
