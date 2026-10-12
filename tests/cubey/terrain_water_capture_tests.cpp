#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_water_capture.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>
#include <span>
#include <stdexcept>

namespace {
using namespace cubey::asset;
using Json = nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F>
void rejects(F f, std::source_location caller = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid water asset was accepted at test line " +
                             std::to_string(caller.line()));
}
struct Fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("cubey-water-capture-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        std::filesystem::create_directory(root);
    }
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
void replace_manifest(const std::filesystem::path& directory, const Json& doc) {
    std::ofstream output(directory / "water-capture.json");
    output << doc.dump();
}
Json manifest(const std::filesystem::path& directory) {
    std::ifstream input(directory / "water-capture.json");
    return Json::parse(input);
}
TerrainWaterCapture capture(const TerrainRasterHeightSource& source) {
    TerrainWaterCapture c;
    // Use the fixture's sloped region, not its deliberately flat stage floor.
    c.grid = {4, 4, 128, {4096, 0}};
    c.source_elevation_sha256 = source.provenance().elevation_sha256;
    c.recording_sha256 = std::string(64, 'a');
    c.frame_sha256 = std::string(64, 'b');
    c.capture_time_s = 60;
    c.rain_mm_per_hour = 512;
    for (unsigned z = 0; z < 4; ++z)
        for (unsigned x = 0; x < 4; ++x) {
            c.bed_m.push_back(source.sample_height(
                {.world_xz = c.grid.sample_origin_xz_m +
                             cubey::math::Vec2{float(x), float(z)} * c.grid.cell_size_m}));
            c.depth_m.push_back(x == 0 && z == 0 ? 0.0F : .25F);
            c.velocity_xz_m_per_s.push_back(x == 0 && z == 0 ? cubey::math::Vec2{0}
                                                             : cubey::math::Vec2{.5F, -.25F});
        }
    return c;
}
void run() {
    Fixture fixture;
    TerrainRasterHeightSource source(std::filesystem::path(CUBEY_SOURCE_DIR) /
                                     "tests/assets/terrain/backdrop-smoke/heightfield.json");
    const auto good = capture(source);
    validate_terrain_water_capture_source(good, source);
    const auto base = fixture.root / "valid";
    write_terrain_water_capture(base, good);
    const auto loaded = load_terrain_water_capture(base);
    require(std::memcmp(loaded.bed_m.data(), good.bed_m.data(),
                        good.bed_m.size() * sizeof(float)) == 0 &&
                std::memcmp(loaded.depth_m.data(), good.depth_m.data(),
                            good.depth_m.size() * sizeof(float)) == 0,
            "bed and depth must roundtrip bit-exactly");
    for (std::size_t i = 0; i < good.bed_m.size(); ++i) {
        require(loaded.velocity_xz_m_per_s[i] == good.velocity_xz_m_per_s[i],
                "velocity roundtrip differs");
    }
    validate_terrain_water_capture_source(loaded, source);
    rejects([&] { write_terrain_water_capture(base, good); });
    for (unsigned variant = 0; variant < 7; ++variant) {
        auto bad = good;
        if (variant == 0)
            bad.depth_m[1] = -1;
        if (variant == 1)
            bad.depth_m[1] = std::numeric_limits<float>::quiet_NaN();
        if (variant == 2)
            bad.velocity_xz_m_per_s[0].x = 1;
        if (variant == 3)
            bad.bed_m.pop_back();
        if (variant == 4)
            bad.grid.width = std::numeric_limits<unsigned>::max();
        if (variant == 5)
            bad.rain_mm_per_hour = 0;
        if (variant == 6)
            bad.grid.cell_size_m = std::numeric_limits<float>::infinity();
        rejects([&] { validate_terrain_water_capture(bad); });
    }
    auto wrong = good;
    wrong.source_elevation_sha256 = std::string(64, 'c');
    rejects([&] { validate_terrain_water_capture_source(wrong, source); });
    wrong = good;
    wrong.grid.sample_origin_xz_m.x += 100;
    rejects([&] { validate_terrain_water_capture_source(wrong, source); });
    wrong = good;
    wrong.grid.sample_origin_xz_m.x = source.bounds().minimum_xz.x - 100;
    rejects([&] { validate_terrain_water_capture_source(wrong, source); });
    for (unsigned variant = 0; variant < 8; ++variant) {
        const auto directory = fixture.root / ("malformed-" + std::to_string(variant));
        write_terrain_water_capture(directory, good);
        auto doc = manifest(directory);
        if (variant == 0)
            doc["fields"]["bed_m"]["path"] = "../valid/bed.f32";
        if (variant == 1)
            doc["fields"]["bed_m"]["sha256"] = std::string(64, 'f');
        if (variant == 2)
            doc["fields"]["depth_m"]["byte_count"] = 1;
        if (variant == 3)
            doc["grid"]["width"] = 2.5;
        if (variant == 4)
            doc["grid"]["row_direction"] = "world-z-negative";
        if (variant == 5)
            doc["grid"]["sample_origin_xz_m"] = {1, 2, 3};
        if (variant == 6)
            doc["fields"]["velocity_xz_m_per_s"]["layout"] = "interleaved";
        if (variant == 7) {
            std::filesystem::create_symlink(base / "bed.f32", directory / "linked.f32");
            doc["fields"]["bed_m"]["path"] = "linked.f32";
        }
        replace_manifest(directory, doc);
        rejects([&] { static_cast<void>(load_terrain_water_capture(directory)); });
    }
    const auto tamper = fixture.root / "tampered";
    write_terrain_water_capture(tamper, good);
    {
        std::ofstream output(tamper / "bed.f32", std::ios::binary);
        output << "bad";
    }
    rejects([&] { static_cast<void>(load_terrain_water_capture(tamper)); });
}
} // namespace
int main() {
    try {
        run();
        std::cout
            << "terrain_water_capture: strict contract, source placement and roundtrip PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
