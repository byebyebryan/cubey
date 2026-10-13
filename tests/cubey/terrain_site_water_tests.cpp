#include <chrono>
#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_site_water.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
void write(const std::filesystem::path& p, const std::string& text) {
    std::ofstream(p) << text;
}
std::string hash(const std::string& value) {
    return cubey::asset::sha256_hex(std::as_bytes(std::span{value}));
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid site-water binding accepted");
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("cubey-site-water-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        using namespace cubey;
        std::filesystem::create_directory(root);
        asset::TerrainSite site;
        site.id = "test";
        site.world_id = std::string(64, 'a');
        site.heightfield = root / "heightfield.json";
        site.center_xz_m = {100, 200};
        site.level_spans_m = {16, 32, 64, 128};
        write(root / "site.json", "site");
        write(root / "world-recipe.json", "world");
        write(site.heightfield, "source");
        write(root / "water-capture.json", "capture");
        nlohmann::json doc{
            {"schema", "cubey.terrain.site-water-binding.v1"},
            {"site_id", "test"},
            {"world_id", site.world_id},
            {"site_manifest_sha256", hash("site")},
            {"world_recipe_sha256", hash("world")},
            {"heightfield_manifest_sha256", hash("source")},
            {"capture", {{"path", "water-capture.json"}, {"sha256", hash("capture")}}}};
        const auto binding = root / "water-binding.json";
        write(binding, doc.dump());
        if (asset::load_terrain_site_water_binding(binding, root / "site.json", site) !=
            root / "water-capture.json")
            throw std::runtime_error("wrong bound capture path");
        for (const std::string key : {"site_id", "world_id", "site_manifest_sha256",
                                      "world_recipe_sha256", "heightfield_manifest_sha256"}) {
            auto bad = doc;
            bad[key] = "changed";
            write(binding, bad.dump());
            rejects([&] {
                (void)asset::load_terrain_site_water_binding(binding, root / "site.json", site);
            });
        }
        for (const std::string name :
             {"../water-capture.json", "/tmp/water-capture.json", "./water-capture.json"}) {
            auto bad = doc;
            bad["capture"]["path"] = name;
            write(binding, bad.dump());
            rejects([&] {
                (void)asset::load_terrain_site_water_binding(binding, root / "site.json", site);
            });
        }
        auto bad = doc;
        bad["extra"] = true;
        write(binding, bad.dump());
        rejects([&] {
            (void)asset::load_terrain_site_water_binding(binding, root / "site.json", site);
        });
        write(binding, doc.dump().insert(1, "\"schema\":\"duplicate\","));
        rejects([&] {
            (void)asset::load_terrain_site_water_binding(binding, root / "site.json", site);
        });
        write(binding, doc.dump());
        write(root / "water-capture.json", "changed");
        rejects([&] {
            (void)asset::load_terrain_site_water_binding(binding, root / "site.json", site);
        });
        asset::TerrainWaterCapture capture;
        capture.grid = {3, 3, 32, {68, 168}};
        capture.source_elevation_sha256 = capture.recording_sha256 = capture.frame_sha256 =
            std::string(64, 'b');
        capture.capture_time_s = 43200;
        capture.rain_mm_per_hour = 512;
        capture.bed_m.assign(9, 0);
        capture.depth_m.assign(9, 1);
        capture.velocity_xz_m_per_s.assign(9, math::Vec2{0});
        asset::validate_terrain_site_water_capture(capture, site);
        capture.grid.sample_origin_xz_m.x += 1;
        rejects([&] { asset::validate_terrain_site_water_capture(capture, site); });
        capture.grid = {3, 3, 128, {-28, 72}};
        rejects([&] { asset::validate_terrain_site_water_capture(capture, site); });
        std::filesystem::remove_all(root);
        std::cout << "terrain_site_water: PASS identities, paths, duplicate keys, source pin and "
                     "coverage\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
